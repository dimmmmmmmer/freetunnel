package org.freetunnel.android

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.VpnService
import android.os.Build
import android.os.ParcelFileDescriptor
import androidx.core.content.edit
import androidx.core.app.NotificationCompat
import com.adguard.trusttunnel.CertificateVerificator
import com.adguard.trusttunnel.VpnClient
import com.adguard.trusttunnel.VpnClientListener
import com.adguard.trusttunnel.VpnServiceConfig
import com.adguard.trusttunnel.utils.NetworkUtils
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException

/** All native resources belong to this instance and are used on its single worker. */
class FreeTunnelVpnService : VpnService() {
    companion object {
        const val ACTION_START = "org.freetunnel.android.START_VPN"
        const val ACTION_STOP = "org.freetunnel.android.STOP_VPN"
        const val ACTION_STATE = "org.freetunnel.android.VPN_STATE"
        const val EXTRA_CONFIG = "config"
        const val EXTRA_STATE = "state"
        private const val CHANNEL = "freetunnel_vpn"
        private const val FAILURE_CHANNEL = "freetunnel_vpn_failure"
        private const val NOTIFICATION_ID = 47
        private const val FAILURE_ID = 48
        @Volatile private var currentState = 0
        @Volatile private var owner: FreeTunnelVpnService? = null

        @JvmStatic fun state(context: Context): Int = currentState
        @JvmStatic fun start(context: Context, source: String) {
            val intent = Intent(context, FreeTunnelVpnService::class.java).setAction(ACTION_START).putExtra(EXTRA_CONFIG, source)
            context.startForegroundService(intent)
        }
        @JvmStatic fun stop(context: Context) { send(context, ACTION_STOP) }
        /** Sent by PackageReplacedReceiver, in the new version, when the VPN was on at Install. */
        const val ACTION_RESUME = "org.freetunnel.android.RESUME_VPN"
        /** Preference: the versionCode being installed while the VPN was on. */
        const val RESUME_AFTER_UPDATE = "update_resume_vpn"
        @JvmStatic fun resume(context: Context) { send(context, ACTION_RESUME) }
        private fun send(context: Context, action: String) {
            context.startForegroundService(Intent(context, FreeTunnelVpnService::class.java).setAction(action))
        }
    }

    private val worker = Executors.newSingleThreadExecutor { Thread(it, "freetunnel-vpn") }
    private val prefs by lazy { getSharedPreferences("freetunnel", MODE_PRIVATE) }
    private var client: VpnClient? = null
    private var retainedTun: ParcelFileDescriptor? = null
    private var collector: NetworkUtils.Companion.NetworkCollector? = null
    private var generation = 0L
    private var establishedThisRequest = false
    private var keepBlocked = true
    private var activeSource: String? = null
    @Volatile private var permissionEpoch = 0L
    @Volatile private var destroyed = false
    @Volatile private var latestStartId = 0

    override fun onCreate() {
        super.onCreate()
        owner = this
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(NotificationChannel(CHANNEL, "FreeTunnel VPN", NotificationManager.IMPORTANCE_LOW))
        manager.createNotificationChannel(NotificationChannel(FAILURE_CHANNEL, getString(R.string.text_vpn_disconnected), NotificationManager.IMPORTANCE_HIGH))
        startForeground(NOTIFICATION_ID, notification(getString(R.string.text_preparing_vpn)))
        collector = NetworkUtils.Companion.NetworkCollector().also { networks ->
            getSystemService(ConnectivityManager::class.java).registerNetworkCallback(NetworkRequest.Builder()
                .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN).build(), networks)
        }
        // Migrate away from stale persistent state, including pre-upgrade installs.
        prefs.edit { remove("vpn_state"); remove("service_config_encrypted") }
    }

    private fun execute(action: () -> Unit) {
        try { worker.execute(action) } catch (_: RejectedExecutionException) { /* Instance already destroyed. */ }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        latestStartId = startId
        // A disconnect also cancels the reconnect planned for after an update.
        if (intent?.action == ACTION_STOP) prefs.edit { remove(RESUME_AFTER_UPDATE) }
        // A new start may arrive after stopForeground but before onDestroy.
        // onCreate is not called again in that window; acknowledge every FGS start.
        startForeground(NOTIFICATION_ID, notification(stateText()))
        val epoch = permissionEpoch
        execute {
            if (destroyed || startId != latestStartId || epoch != permissionEpoch) return@execute
            if (intent?.action == ACTION_STOP) {
                close()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelfResult(startId)
                return@execute
            }
            // Android's Always-on start, which AOSP may send a second time while
            // the first is still setting up, and the reconnect after an update
            // only start a VPN that is not running, so neither cuts a working
            // connection. A client that is connecting or recovering counts as
            // running. A TUN the kill switch keeps after a failure has no client,
            // so these starts still reconnect it.
            val resuming = intent?.action == ACTION_RESUME
            if ((resuming || intent?.action == VpnService.SERVICE_INTERFACE) && client != null) {
                logEvent("VPN already running or reconnecting; start ignored")
                return@execute
            }
            if (resuming) logEvent("Reconnecting after update")
            startForeground(NOTIFICATION_ID, notification(stateText()))
            val previous = client
            establishedThisRequest = false
            try {
                val source = intent?.getStringExtra(EXTRA_CONFIG) ?: activeSource ?: SavedProfile.selected(prefs)
                val plan = RoutingConfig.prepare(this, source)
                check(VpnService.prepare(this) == null) { "VPN permission is required" }
                replace(plan, startId, epoch)
                activeSource = source
            } catch (error: Throwable) {
                if (!destroyed && startId == latestStartId && epoch == permissionEpoch) {
                    if (retainedTun != null && client === previous && !establishedThisRequest) {
                        logEvent("VPN settings rejected: ${error.javaClass.simpleName}")
                        publishState(currentState)
                        getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, notification(stateText()))
                        getSystemService(NotificationManager::class.java).notify(FAILURE_ID,
                            NotificationCompat.Builder(this, FAILURE_CHANNEL).setSmallIcon(android.R.drawable.ic_dialog_alert)
                                .setContentTitle(getString(R.string.text_vpn_settings_were_not_applied)).setContentText(getString(R.string.config_validation_error, error.message))
                                .setContentIntent(openApp()).setAutoCancel(true).build())
                        return@execute
                    }
                    logEvent("VPN start failed: ${error.javaClass.simpleName}")
                    fail(getString(R.string.text_could_not_start_vpn))
                }
            }
        }
        // Request framework-managed recreation using SavedProfile. Android controls
        // restart scheduling; only system lockdown protects while no service runs.
        return if (intent?.action == ACTION_STOP) START_NOT_STICKY else START_STICKY
    }

    private fun replace(plan: RoutingConfig.Plan, startId: Int, epoch: Long) {
        NativeConfigValidator.validate(this, plan.toml)
        val parsed = RoutingConfig.validate(plan.toml)
        val config = VpnServiceConfig.parseToml(plan.toml) ?: error("Invalid adapter configuration")
        val verifier = CertificateVerificator()
        val token = generation + 1
        val listener = object : VpnClientListener {
            override fun protectSocket(socket: Int) = protect(socket)
            override fun verifyCertificate(certificate: ByteArray?, rawChain: List<ByteArray?>?) = verifier.verifyCertificate(certificate, rawChain)
            override fun onConnectionInfo(info: String) = Unit
            override fun onStateChanged(state: Int) = execute {
                if (destroyed || token != generation) return@execute
                if (state == 0) {
                    fail(getString(R.string.text_connection_to_the_server_was_lost))
                } else {
                    publishState(state)
                    getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID,
                        notification(if (state == 2) getString(R.string.text_connected) else getString(R.string.text_recovering_connection), plan.warning))
                }
            }
        }
        var candidate: VpnClient? = null
        var descriptor: ParcelFileDescriptor? = null
        try {
            candidate = VpnClient(plan.toml, listener)
            if (destroyed || startId != latestStartId || epoch != permissionEpoch) return
            val newTun = createTun(config, parsed.getBoolean("endpoint.has_ipv6") ?: false, plan)
            establishedThisRequest = true
            // Own the new TUN before any operation that can fail. The core receives
            // a duplicate; our descriptor survives native failure and closes on stop.
            close(publish = false)
            retainedTun = newTun
            keepBlocked = parsed.getBoolean("killswitch_enabled") ?: true
            activeSource = plan.toml
            check(!destroyed && epoch == permissionEpoch) { "VPN start cancelled" }
            generation = token
            client = candidate
            descriptor = ParcelFileDescriptor.dup(newTun.fileDescriptor)
            publishState(1)
            collector?.startNotifying(candidate)
            check(candidate.start(descriptor)) { "VPN core rejected the configuration" }
            candidate = null
            getSystemService(NotificationManager::class.java).cancel(FAILURE_ID)
            logEvent("TrustTunnel: starting connection")
        } finally {
            runCatching { descriptor?.close() }
            candidate?.let { runCatching { it.stop() }; runCatching { it.close() } }
        }
    }

    private fun createTun(config: VpnServiceConfig, hasIpv6: Boolean, plan: RoutingConfig.Plan): ParcelFileDescriptor {
        val tun = config.listener.tun
        val ipv6 = hasIpv6 && tun.includedRoutes.any { ':' in it }
        require(tun.mtuSize in (if (ipv6) 1280L else 576L)..65535L) { "Invalid MTU" }
        val builder = Builder().setSession("FreeTunnel").setMtu(tun.mtuSize.toInt()).addAddress("172.20.2.13", 32)
        if (Build.VERSION.SDK_INT >= 29) builder.setMetered(false)
        if (ipv6) builder.addAddress("fdfd:29::2", 128)
        // Never call allowFamily: an absent family must be blocked by Android.
        val dns = if (config.endpoint.dnsUpstreams.isEmpty()) listOf("46.243.231.30", "46.243.231.31") else listOf("198.18.53.53")
        dns.forEach(builder::addDnsServer)
        val included = tun.includedRoutes.filter { ipv6 || ':' !in it }
        val excluded = tun.excludedRoutes.filter { ipv6 || ':' !in it } + RoutingConfig.platformExcludedRoutes
        val routes = VpnClient.excludeCidr(included, excluded) ?: error("Invalid routes")
        require(routes.isNotEmpty()) { "No VPN routes" }
        routes.forEach {
            val pair = NetworkUtils.convertCidrToAddressPrefixPair(it) ?: error("Invalid subnet")
            builder.addRoute(pair.first, pair.second)
        }
        val installed = plan.apps.filter { it != packageName }.filter {
            try { packageManager.getApplicationInfo(it, 0); true } catch (_: android.content.pm.PackageManager.NameNotFoundException) { false }
        }
        if (plan.allowApps && installed.isNotEmpty()) {
            installed.forEach(builder::addAllowedApplication)
        } else {
            // If every saved app was uninstalled, the safe fallback is full VPN.
            if (!plan.allowApps) installed.forEach(builder::addDisallowedApplication)
            builder.addDisallowedApplication(packageName)
        }
        return builder.establish() ?: error("Android could not establish VPN")
    }

    private fun stopClient() {
        generation++
        collector?.stopNotifying()
        val old = client
        client = null
        old?.let { runCatching { it.stop() }; runCatching { it.close() } }
    }

    private fun close(publish: Boolean = true) {
        stopClient()
        runCatching { retainedTun?.close() }
        retainedTun = null
        activeSource = null
        if (publish) publishState(0)
    }

    private fun fail(reason: String) {
        stopClient()
        if (keepBlocked && retainedTun != null) {
            // No consumer remains on the TUN. Traffic covered by this VPN is
            // discarded until an explicit reconnect or disconnect.
            publishState(6)
            logEvent("VPN stopped; kill switch retains the TUN")
            getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID,
                notification(getString(R.string.vpn_traffic_blocked)))
        } else {
            close()
            showFailure(reason)
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelfResult(latestStartId)
        }
    }

    override fun onRevoke() {
        permissionEpoch++
        prefs.edit { remove(RESUME_AFTER_UPDATE) }
        publishState(0)
        showFailure(getString(R.string.text_vpn_permission_was_revoked))
        val revokedId = latestStartId
        execute {
            // Revocation always invalidates resources, even if another START
            // arrived while this task waited. A later start must obtain consent.
            close()
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelfResult(revokedId)
        }
    }

    override fun onDestroy() {
        destroyed = true
        publishState(0)
        if (owner === this) owner = null
        execute {
            close(publish = false)
            collector?.let { runCatching { getSystemService(ConnectivityManager::class.java).unregisterNetworkCallback(it) } }
            collector = null
        }
        worker.shutdown()
        super.onDestroy()
    }

    private fun publishState(state: Int) {
        if (owner !== this) return
        currentState = state
        sendBroadcast(Intent(ACTION_STATE).setPackage(packageName))
        QuickTileService.refresh(this)
    }

    private fun showFailure(reason: String) {
        logEvent(reason)
        getSystemService(NotificationManager::class.java).notify(FAILURE_ID,
            NotificationCompat.Builder(this, FAILURE_CHANNEL).setSmallIcon(android.R.drawable.ic_dialog_alert)
                .setContentTitle(getString(R.string.text_vpn_disconnected)).setContentText(reason)
                .setStyle(NotificationCompat.BigTextStyle().bigText(getString(R.string.vpn_failure_details, reason)))
                .setContentIntent(openApp()).setAutoCancel(true).setPriority(NotificationCompat.PRIORITY_HIGH).build())
    }

    private fun openApp() = PendingIntent.getActivity(this, 0,
        Intent(this, MainActivity::class.java).setAction(Intent.ACTION_MAIN), PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

    private fun stateText(): String = getString(when (currentState) {
        2 -> R.string.text_connected
        6 -> R.string.vpn_traffic_blocked
        3, 4, 5 -> R.string.text_recovering_connection
        else -> R.string.text_connecting
    })

    private fun notification(status: String, warning: Int? = null): Notification = NotificationCompat.Builder(this, CHANNEL)
        .setSmallIcon(android.R.drawable.ic_lock_lock).setContentTitle("FreeTunnel").setContentText(status)
        .setStyle(NotificationCompat.BigTextStyle().bigText(listOfNotNull(status, warning?.let { getString(it) }).joinToString(". ")))
        .setContentIntent(openApp())
        .addAction(0, getString(R.string.vpn_reconnect), serviceAction(ACTION_START, 1))
        .addAction(0, getString(R.string.text_disconnect_vpn), serviceAction(ACTION_STOP, 2))
        .setOngoing(true).setPriority(NotificationCompat.PRIORITY_LOW).build()

    private fun serviceAction(action: String, request: Int) = PendingIntent.getForegroundService(this, request,
        Intent(this, FreeTunnelVpnService::class.java).setAction(action), PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

    private fun logEvent(message: String) {
        if (!prefs.getBoolean("logging", true)) return
        runCatching {
            val file = File(filesDir, "freetunnel.log")
            val old = if (file.exists()) file.readLines().takeLast(199) else emptyList()
            file.writeText((old + "${System.currentTimeMillis()} $message").joinToString("\n", postfix = "\n"))
        }
    }
}
