package org.freetunnel.android

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.VpnService
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import androidx.core.app.NotificationCompat
import com.adguard.trusttunnel.CertificateVerificator
import com.adguard.trusttunnel.VpnClient
import com.adguard.trusttunnel.VpnClientListener
import com.adguard.trusttunnel.VpnServiceConfig
import com.adguard.trusttunnel.VpnState
import java.io.File
import java.util.concurrent.Executors

/** Owns the real Android TUN interface and the TrustTunnel native client. */
class FreeTunnelVpnService : VpnService() {
    companion object {
        const val ACTION_START = "org.freetunnel.android.START_VPN"
        const val ACTION_RESTART = "org.freetunnel.android.RESTART_VPN"
        const val ACTION_STOP = "org.freetunnel.android.STOP_VPN"
        const val ACTION_STATE = "org.freetunnel.android.VPN_STATE"
        const val EXTRA_CONFIG = "config"
        const val EXTRA_STATE = "state"
        const val STATE_KEY = "vpn_state"
        private const val CHANNEL_ID = "freetunnel_vpn"
        private const val NOTIFICATION_ID = 47
        private const val CONFIG_KEY = "service_config_encrypted"
        private const val PREFS = "freetunnel"
        private val worker = Executors.newSingleThreadExecutor()

        @JvmStatic
        fun start(context: Context, config: String) {
            val intent = Intent(context, FreeTunnelVpnService::class.java)
                .setAction(ACTION_START)
                .putExtra(EXTRA_CONFIG, config)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) context.startForegroundService(intent)
            else context.startService(intent)
        }

        @JvmStatic
        fun stop(context: Context) {
            val intent = Intent(context, FreeTunnelVpnService::class.java).setAction(ACTION_STOP)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) context.startForegroundService(intent)
            else context.startService(intent)
        }

        @JvmStatic
        fun restart(context: Context, config: String) {
            val intent = Intent(context, FreeTunnelVpnService::class.java)
                .setAction(ACTION_RESTART)
                .putExtra(EXTRA_CONFIG, config)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) context.startForegroundService(intent)
            else context.startService(intent)
        }

        @JvmStatic
        fun state(context: Context): Int =
            context.getSharedPreferences(PREFS, MODE_PRIVATE).getInt(STATE_KEY, VpnState.DISCONNECTED.code)
    }

    private val main = Handler(Looper.getMainLooper())
    private val prefs by lazy { getSharedPreferences(PREFS, MODE_PRIVATE) }
    private var client: VpnClient? = null
    private var certificateVerificator: CertificateVerificator? = null
    private var connectivity: ConnectivityManager? = null
    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    @Volatile private var active = false
    @Volatile private var clientGeneration = 0

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, notification("Подготовка VPN"))
        connectivity = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> worker.execute { stopTunnel(startId) }
            ACTION_RESTART -> {
                val config = intent.getStringExtra(EXTRA_CONFIG)
                if (config.isNullOrBlank()) stopTunnel(startId)
                else {
                    val sealed = ConfigStore.encrypt(config)
                    if (sealed != null) prefs.edit().putString(CONFIG_KEY, sealed).apply()
                    worker.execute {
                        active = false
                        disposeClient(stop = true)
                        startTunnel(config, startId)
                    }
                }
            }
            ACTION_START -> {
                val config = intent.getStringExtra(EXTRA_CONFIG)
                if (config.isNullOrBlank()) {
                    publishState(VpnState.DISCONNECTED.code)
                    stopSelf(startId)
                } else {
                    // Persist the effective profile encrypted so Android can recreate the service.
                    val sealed = ConfigStore.encrypt(config)
                    if (sealed != null) prefs.edit().putString(CONFIG_KEY, sealed).apply()
                    worker.execute { startTunnel(config, startId) }
                }
            }
            else -> {
                val sealed = prefs.getString(CONFIG_KEY, null)
                val config = sealed?.let(ConfigStore::decrypt)
                if (config == null) stopSelf(startId)
                else worker.execute { startTunnel(config, startId) }
            }
        }
        return START_STICKY
    }

    private fun startTunnel(configText: String, startId: Int) {
        if (active) return
        publishState(VpnState.CONNECTING.code)
        val config = VpnServiceConfig.parseToml(configText)
        if (config == null) return fail(startId)

        try {
            certificateVerificator = CertificateVerificator()
            val tun = createTun(config) ?: return fail(startId)
            val generation = ++clientGeneration
            val listener = object : VpnClientListener {
                override fun protectSocket(socket: Int): Boolean = this@FreeTunnelVpnService.protect(socket)
                override fun verifyCertificate(certificate: ByteArray?, rawChain: List<ByteArray?>?): Boolean =
                    certificateVerificator?.verifyCertificate(certificate, rawChain) ?: false
                override fun onStateChanged(state: Int) = handleNativeState(generation, state)
                override fun onConnectionInfo(info: String) = handleConnectionInfo(generation, info)
            }
            val nextClient = VpnClient(configText, listener)
            client = nextClient
            registerNetworkCallback(nextClient)
            if (!nextClient.start(tun)) return fail(startId)
            active = true
            publishState(VpnState.CONNECTING.code)
            logEvent("TrustTunnel: запуск соединения")
        } catch (error: Exception) {
            logEvent("Ошибка запуска VPN: ${error.javaClass.simpleName}")
            fail(startId)
        }
    }

    private fun createTun(config: VpnServiceConfig): android.os.ParcelFileDescriptor? {
        val tun = config.listener.tun
        val builder = Builder()
            .setSession("FreeTunnel")
            .setMtu(tun.mtuSize.toInt().coerceIn(576, 9000))
            .addAddress("172.20.2.13", 32)
            .addAddress("fdfd:29::2", 64)

        if (config.endpoint.dnsUpstreams.isEmpty()) {
            listOf("46.243.231.30", "46.243.231.31", "2a10:50c0::2:ff", "2a10:50c0::1:ff")
                .forEach(builder::addDnsServer)
        } else {
            builder.addDnsServer("198.18.53.53")
        }

        val excluded = tun.excludedRoutes + listOf("0.0.0.0/8", "224.0.0.0/3")
        val routes = VpnClient.excludeCidr(tun.includedRoutes, excluded) ?: return null
        for (route in routes) {
            val parts = route.split('/')
            if (parts.size != 2) return null
            builder.addRoute(parts[0], parts[1].toInt())
        }

        val apps = (if (prefs.getBoolean("split_enabled", false)) prefs.getStringSet("apps", emptySet()).orEmpty() else emptySet())
            .filter { it != packageName }
        val selective = prefs.getBoolean("split_enabled", false) && prefs.getBoolean("through", false)
        if (selective && apps.isEmpty()) builder.addAllowedApplication(packageName)
        for (app in apps) {
            try {
                if (selective) builder.addAllowedApplication(app)
                else builder.addDisallowedApplication(app)
            } catch (_: Exception) {
                // Removed apps can remain in saved rules; skip them when rebuilding the VPN.
            }
        }
        if (!selective) builder.addDisallowedApplication(packageName)
        return builder.establish()
    }

    private fun registerNetworkCallback(vpnClient: VpnClient) {
        val manager = connectivity ?: return
        val request = NetworkRequest.Builder()
            .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN)
            .build()
        val callback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                vpnClient.notifyNetworkChange(true)
                val props = manager.getLinkProperties(network)
                val servers = props?.dnsServers?.mapNotNull { it.hostAddress }?.filter { it.isNotBlank() }
                if (!servers.isNullOrEmpty()) VpnClient.setSystemDnsServers(servers, null)
            }

            override fun onLost(network: Network) {
                if (manager.allNetworks.none { manager.getNetworkCapabilities(it)?.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) == true }) {
                    vpnClient.notifyNetworkChange(false)
                }
            }
        }
        manager.registerNetworkCallback(request, callback)
        networkCallback = callback
    }

    private fun fail(startId: Int) {
        active = false
        disposeClient(stop = true)
        publishState(VpnState.DISCONNECTED.code)
        prefs.edit().remove(CONFIG_KEY).apply()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf(startId)
    }

    private fun stopTunnel(startId: Int? = null) {
        if (!active && client == null) {
            prefs.edit().remove(CONFIG_KEY).apply()
            publishState(VpnState.DISCONNECTED.code)
            stopForeground(STOP_FOREGROUND_REMOVE)
            if (startId == null) stopSelf() else stopSelf(startId)
            return
        }
        active = false
        disposeClient(stop = true)
        prefs.edit().remove(CONFIG_KEY).apply()
        publishState(VpnState.DISCONNECTED.code)
        logEvent("TrustTunnel: отключено")
        stopForeground(STOP_FOREGROUND_REMOVE)
        if (startId == null) stopSelf() else stopSelf(startId)
    }

    private fun disposeClient(stop: Boolean) {
        // Invalidate callbacks from the old native instance before stopping it.
        clientGeneration++
        val oldClient = client
        client = null
        if (stop) try { oldClient?.stop() } catch (_: Exception) { }
        try { oldClient?.close() } catch (_: Exception) { }
        networkCallback?.let { callback ->
            try { connectivity?.unregisterNetworkCallback(callback) } catch (_: Exception) { }
        }
        networkCallback = null
        certificateVerificator = null
    }

    override fun onRevoke() {
        worker.execute { stopTunnel() }
        super.onRevoke()
    }

    override fun onDestroy() {
        active = false
        disposeClient(stop = true)
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = super.onBind(intent)

    private fun handleNativeState(generation: Int, state: Int) {
        if (generation != clientGeneration) return
        publishState(state)
        when (state) {
            VpnState.CONNECTED.code -> {
                getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, notification("Подключено"))
                logEvent("TrustTunnel: подключено")
            }
            VpnState.CONNECTING.code -> getSystemService(NotificationManager::class.java)
                .notify(NOTIFICATION_ID, notification("Подключение"))
            VpnState.DISCONNECTED.code -> {
                logEvent("TrustTunnel: соединение завершено")
                if (active) worker.execute { if (generation == clientGeneration && active) stopTunnel() }
            }
            VpnState.RECOVERING.code, VpnState.WAITING_RECOVERY.code, VpnState.WAITING_FOR_NETWORK.code ->
                getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, notification("Восстановление соединения"))
        }
    }

    private fun handleConnectionInfo(generation: Int, info: String) {
        if (generation != clientGeneration) return
        // Connection details can include hostnames; keep the local journal private and minimal.
        if (info.contains("error", ignoreCase = true)) logEvent("TrustTunnel: сетевое событие")
    }

    private fun publishState(state: Int) {
        prefs.edit().putInt(STATE_KEY, state).apply()
        main.post {
            sendBroadcast(Intent(ACTION_STATE).setPackage(packageName).putExtra(EXTRA_STATE, state))
        }
    }

    private fun logEvent(message: String) {
        if (!prefs.getBoolean("logging", true)) return
        val file = File(filesDir, "freetunnel.log")
        synchronized(file) {
            val old = if (file.exists()) file.readLines().takeLast(199) else emptyList()
            file.writeText((old + "${System.currentTimeMillis()} $message").joinToString("\n", postfix = "\n"))
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(CHANNEL_ID, "FreeTunnel VPN", NotificationManager.IMPORTANCE_LOW)
            getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        }
    }

    private fun notification(status: String): Notification =
        NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_lock_lock)
            .setContentTitle("FreeTunnel")
            .setContentText(status)
            .setOngoing(status != "Отключено")
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
}
