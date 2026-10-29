package org.freetunnel.android

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.VpnService
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.os.SystemClock
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
        const val EXTRA_PROFILE = "profile"
        const val EXTRA_STATE = "state"
        /** Broadcast to the app, while it is on the screen, with the reason a connection ended in failure (see showFailure). */
        const val ACTION_FAILURE = "org.freetunnel.android.VPN_FAILURE"
        const val EXTRA_REASON = "reason"
        private const val CHANNEL = "freetunnel_vpn"
        private const val FAILURE_CHANNEL = "freetunnel_vpn_failure"
        private const val NOTIFICATION_ID = 47
        private const val FAILURE_ID = 48
        // Its own id, so this alert and "settings were not applied" are posted
        // and cancelled independently.
        private const val BLOCKED_ID = 49
        // A session that an automatic attempt started and that ends sooner than
        // this carries its episode on, so a server that accepts and then drops
        // at once can't cause a 1 s reconnect loop or an alert every 30 s.
        private const val SHORT_SESSION_MS = 10_000L
        // Gives a network that just appeared a moment to settle before an attempt.
        private const val NETWORK_SETTLE_MS = 1_000L
        // Handler token of the pending automatic attempt.
        private val RETRY = Any()
        @Volatile private var currentState = 0
        @Volatile private var owner: FreeTunnelVpnService? = null
        // When the connection last came up, on SystemClock.elapsedRealtime(), or 0 while it is not up: the time
        // Home shows beside the logo. The desktop restarts it each time it becomes connected (applyVpnClientState),
        // so this does too. It is not connectedAt, which keeps a session going across the core's own recovery for
        // the backoff, and which nothing outside the service should have to understand.
        @Volatile private var sessionStartedAt = 0L
        // Whether the app is on the screen, from its onResume to its onPause. The desktop sends an error to its tray only while its window is not
        // in view (Main.qml, errorsGoToTray), and never puts a system message over its own open window: the app's own screen says what went
        // wrong (the line under the logo, the Reconnect link under it), and a pop-over covered its nav bar. While it is, the alerts below are
        // posted without a sound or a heads-up, and are still in the shade; with the app away they alert as before.
        @Volatile private var appVisible = false

        @JvmStatic fun setAppVisible(visible: Boolean) { appVisible = visible }
        @JvmStatic fun state(context: Context): Int = currentState
        /** SystemClock.elapsedRealtime() at the moment the connection last became connected, or 0 where it is not. */
        @JvmStatic fun sessionStart(): Long = sessionStartedAt
        /**
         * Starts the VPN on this config. [profile] is the split profile it uses; without one, it is the profile of the
         * config the app has selected, which is the config this is wherever the app starts it.
         */
        @JvmStatic @JvmOverloads fun start(context: Context, source: String, profile: String? = null) {
            val intent = Intent(context, FreeTunnelVpnService::class.java).setAction(ACTION_START).putExtra(EXTRA_CONFIG, source)
            if (profile != null) intent.putExtra(EXTRA_PROFILE, profile)
            context.startForegroundService(intent)
        }
        @JvmStatic fun stop(context: Context) { send(context, ACTION_STOP) }
        // The same request as the notification's Reconnect: with no config it
        // reconnects the profile in use. Nothing in the app's screens sends it (the desktop's Home has no such
        // control, and the service reconnects by itself); device tests do.
        @JvmStatic fun reconnect(context: Context) { send(context, ACTION_START) }
        // A process killed while blocked leaves its alert behind, and only the
        // service's next onCreate would clear it. The state lives in this
        // process, so anything but 6 means no episode is running.
        @JvmStatic fun clearStaleAlert(context: Context) {
            if (currentState != 6) context.getSystemService(NotificationManager::class.java).cancel(BLOCKED_ID)
        }
        /** Sent by PackageReplacedReceiver, in the new version, when the VPN was on at Install. */
        const val ACTION_RESUME = "org.freetunnel.android.RESUME_VPN"
        /** Preference: the versionCode being installed while the VPN was on. */
        const val RESUME_AFTER_UPDATE = "update_resume_vpn"
        @JvmStatic fun resume(context: Context) { send(context, ACTION_RESUME) }
        private fun send(context: Context, action: String) {
            context.startForegroundService(Intent(context, FreeTunnelVpnService::class.java).setAction(action))
        }
    }

    // Its notifications speak the app's language, which Android 8 to 12 only knows from this (AppLocale).
    override fun attachBaseContext(base: Context) { super.attachBaseContext(AppLocale.wrap(base)) }

    private val worker = Executors.newSingleThreadExecutor { Thread(it, "freetunnel-vpn") }
    private val prefs by lazy { getSharedPreferences("freetunnel", MODE_PRIVATE) }
    private var client: VpnClient? = null
    private var retainedTun: ParcelFileDescriptor? = null
    private var collector: NetworkUtils.Companion.NetworkCollector? = null
    private var generation = 0L
    private var establishedThisRequest = false
    private var keepBlocked = true
    private var activeSource: String? = null
    // The split profile activeSource runs with.
    private var activeProfile: String? = null
    // Automatic recovery while the kill switch blocks traffic. retainedTun was
    // built from activePlan; blocked lasts from a fatal failure until the core
    // reports 2 or the TUN is closed.
    private var activePlan: RoutingConfig.Plan? = null
    private var blocked = false
    private var backoff = ReconnectBackoff() // A var so a device test can swap in a longer one.
    private var connectedAt = 0L // When the current session began.
    private var attempts = 0
    private val loggedReasons = HashSet<String>()
    private var loggedRecovery = false
    private var retrySeq = 0L
    private var availability: ConnectivityManager.NetworkCallback? = null
    // Its time stops in deep sleep, so a sleeping phone is never woken to retry.
    private val main = Handler(Looper.getMainLooper())
    @Volatile private var permissionEpoch = 0L
    @Volatile private var destroyed = false
    // The id of the latest start, recorded by onStartCommand once that start has been answered with startForeground.
    @Volatile private var latestStartId = 0
    // The id of the start whose task the worker ran last, which is what the worker ends the service with. A start that is
    // newer is still to be served, so ending the service with this id fails and leaves that start its service. Worker only.
    private var servedStartId = 0

    override fun onCreate() {
        super.onCreate()
        owner = this
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(NotificationChannel(CHANNEL, "FreeTunnel VPN", NotificationManager.IMPORTANCE_LOW))
        manager.createNotificationChannel(NotificationChannel(FAILURE_CHANNEL, getString(R.string.text_vpn_disconnected), NotificationManager.IMPORTANCE_HIGH))
        startForeground(NOTIFICATION_ID, notification(getString(R.string.text_preparing_vpn)))
        // A process killed while blocked leaves its alert behind.
        manager.cancel(BLOCKED_ID)
        val connectivity = getSystemService(ConnectivityManager::class.java)
        val request = NetworkRequest.Builder()
            .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN).build()
        collector = NetworkUtils.Companion.NetworkCollector().also { connectivity.registerNetworkCallback(request, it) }
        // The collector keeps its state private, so a second callback tells a
        // blocked episode that a network appeared.
        availability = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) = execute { networkAvailable() }
        }.also { connectivity.registerNetworkCallback(request, it) }
        // Migrate away from stale persistent state, including pre-upgrade installs.
        prefs.edit { remove("vpn_state"); remove("service_config_encrypted") }
    }

    private fun execute(action: () -> Unit) {
        try { worker.execute(action) } catch (_: RejectedExecutionException) { /* Instance already destroyed. */ }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // Every start is a startForegroundService (send, start, the tile, the receiver, the notification's actions, Android's
        // Always-on), and the system wants startForeground for each one, whatever its action: a stop, a start that will be refused,
        // a start with nothing to run. Nothing goes before it. A new start may arrive after stopForeground but before onDestroy,
        // when onCreate is not called again, so every start acknowledges itself here.
        startForeground(NOTIFICATION_ID, notification(stateText()))
        // Recorded only now. A service that is ended with stopSelfResult(the id of the latest start) while that start still owes its
        // startForeground fails the app with ForegroundServiceDidNotStartInTimeException ("Bringing down service while still waiting
        // for start foreground"), and the worker, in the middle of a failure, may read this id the moment it is recorded.
        latestStartId = startId
        // A disconnect also cancels the reconnect planned for after an update.
        if (intent?.action == ACTION_STOP) prefs.edit { remove(RESUME_AFTER_UPDATE) }
        val epoch = permissionEpoch
        execute {
            if (destroyed || startId != latestStartId || epoch != permissionEpoch) return@execute
            servedStartId = startId
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
            // running, and so does an automatic attempt while the kill switch
            // blocks traffic: that episode goes on and reconnects by itself.
            // Between attempts the kept TUN has no client, and these starts
            // reconnect it at once.
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
                val given = intent?.getStringExtra(EXTRA_CONFIG)
                // A start with no config in it runs the one the app has selected, with the split profile that config
                // uses: Android's Always-on, a restart of a killed service, and the notification's Reconnect when
                // nothing runs yet. The tile and the app say which they start, and the profile with it.
                val saved = if (given == null && activeSource == null) SavedProfile.selected(prefs) else null
                val source = given ?: activeSource ?: saved?.toml ?: error("No configuration")
                val profile = intent?.getStringExtra(EXTRA_PROFILE) ?: saved?.profile
                    ?: (if (given == null) activeProfile else null) ?: SavedProfile.profile(prefs)
                val plan = RoutingConfig.prepare(this, source, profile)
                check(VpnService.prepare(this) == null) { "VPN permission is required" }
                replace(plan, startId, epoch)
                activeSource = source
                activeProfile = profile
            } catch (error: Throwable) {
                if (!destroyed && startId == latestStartId && epoch == permissionEpoch) {
                    if (retainedTun != null && client === previous && !establishedThisRequest) {
                        logEvent("VPN settings rejected: ${error.javaClass.simpleName}", LogLines.WARN)
                        publishState(currentState)
                        getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, notification(stateText()))
                        getSystemService(NotificationManager::class.java).notify(FAILURE_ID,
                            NotificationCompat.Builder(this, FAILURE_CHANNEL).setSmallIcon(android.R.drawable.ic_dialog_alert)
                                .setContentTitle(getString(R.string.text_vpn_settings_were_not_applied)).setContentText(getString(R.string.config_validation_error, error.message))
                                .setContentIntent(openApp()).setAutoCancel(true).build())
                        return@execute
                    }
                    logEvent("VPN start failed: ${error.javaClass.simpleName}", LogLines.ERROR)
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
        // Loading Android's CA store can fail, so it happens before the handover.
        val verifier = CertificateVerificator()
        if (destroyed || startId != latestStartId || epoch != permissionEpoch) return
        val newTun = createTun(config, parsed.getBoolean("endpoint.has_ipv6") ?: false, plan)
        establishedThisRequest = true
        // Own the new TUN before any operation that can fail. The core receives
        // a duplicate; our descriptor survives native failure and closes on stop.
        close(publish = false)
        retainedTun = newTun
        activePlan = plan
        keepBlocked = parsed.getBoolean("killswitch_enabled") ?: true
        activeSource = plan.toml
        check(!destroyed && epoch == permissionEpoch) { "VPN start cancelled" }
        startCore(plan, newTun, verifier)
        getSystemService(NotificationManager::class.java).cancel(FAILURE_ID)
        logEvent("TrustTunnel: starting connection")
    }

    /** Starts a core on a duplicate of [tun], the retained TUN built from [plan]. */
    private fun startCore(plan: RoutingConfig.Plan, tun: ParcelFileDescriptor, verifier: CertificateVerificator = CertificateVerificator()) {
        val token = generation + 1
        val listener = object : VpnClientListener {
            override fun protectSocket(socket: Int) = protect(socket)
            override fun verifyCertificate(certificate: ByteArray?, rawChain: List<ByteArray?>?) = verifier.verifyCertificate(certificate, rawChain)
            // Each connection the core makes, as the desktop logs them (Backend: connectionInfo): at INFO, when logging is on.
            override fun onConnectionInfo(info: String) = logEvent(info)
            override fun onStateChanged(state: Int) = execute {
                if (destroyed || token != generation) return@execute
                when {
                    state == 0 -> fail(getString(R.string.text_connection_to_the_server_was_lost))
                    // An automatic attempt is still connecting and traffic is
                    // still blocked, so the blocked state stays on screen.
                    blocked && state != 2 -> Unit
                    else -> {
                        if (state == 2) recovered()
                        publishState(state)
                        getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID,
                            notification(if (state == 2) getString(R.string.text_connected) else getString(R.string.text_recovering_connection), plan.warning))
                    }
                }
            }
        }
        var candidate: VpnClient? = null
        var descriptor: ParcelFileDescriptor? = null
        try {
            candidate = VpnClient(plan.toml, listener)
            generation = token
            client = candidate
            descriptor = ParcelFileDescriptor.dup(tun.fileDescriptor)
            if (!blocked) publishState(1)
            collector?.startNotifying(candidate)
            check(candidate.start(descriptor)) { "VPN core rejected the configuration" }
            candidate = null
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
        // retryBlocked() would drop a stale attempt anyway; this frees it early.
        cancelRetry()
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
        activePlan = null
        blocked = false
        attempts = 0
        activeSource = null
        activeProfile = null
        getSystemService(NotificationManager::class.java).cancel(BLOCKED_ID)
        if (publish) publishState(0)
    }

    private fun fail(reason: String) {
        stopClient()
        if (keepBlocked && retainedTun != null) {
            // No consumer remains on the TUN. Traffic covered by this VPN is
            // discarded while new cores are tried on the same TUN, until one
            // connects or the user reconnects or disconnects.
            if (!blocked) {
                blocked = true
                // attempts > 0: the session that just ended began with an
                // automatic reconnect. If it was short, the episode goes on: the
                // delay keeps growing, nothing is logged twice and the alert
                // comes back silently.
                val continued = attempts > 0 && SystemClock.elapsedRealtime() - connectedAt < SHORT_SESSION_MS
                if (!continued) {
                    attempts = 0
                    loggedReasons.clear()
                    loggedRecovery = false
                    backoff.reset()
                }
                publishState(6)
                getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID,
                    notification(getString(R.string.vpn_traffic_blocked)))
                showBlocked(reason, silent = continued)
            }
            // Once per reason and episode: retries every 30 s would otherwise
            // push everything else out of the log.
            if (loggedReasons.add(reason)) logEvent("$reason; kill switch keeps traffic blocked, reconnecting automatically", LogLines.WARN)
            scheduleRetry(backoff.next())
        } else {
            close()
            showFailure(reason)
            stopForeground(STOP_FOREGROUND_REMOVE)
            // Not the latest start: one that is newer than the one served last has asked for its own turn, and it keeps the service.
            stopSelfResult(servedStartId)
        }
    }

    private fun recovered() {
        // A session begins here, not when the core recovers by itself from 3, 4
        // or 5. currentState still holds the state before this 2.
        if (blocked || currentState !in 2..5) connectedAt = SystemClock.elapsedRealtime()
        if (!blocked) return
        blocked = false
        getSystemService(NotificationManager::class.java).cancel(BLOCKED_ID)
        if (!loggedRecovery) logEvent("Reconnected automatically, attempt $attempts")
        loggedRecovery = true
    }

    // The delay waits on the main looper and then hops to the worker, which
    // stays a plain executor: a scheduled one would hide a task's exception in
    // its Future instead of crashing.
    private fun scheduleRetry(delayMs: Long) {
        cancelRetry()
        val seq = retrySeq
        val epoch = permissionEpoch
        main.postAtTime(Runnable { execute { if (seq == retrySeq && epoch == permissionEpoch) retryBlocked() } },
            RETRY, SystemClock.uptimeMillis() + delayMs)
    }

    private fun cancelRetry() {
        retrySeq++
        main.removeCallbacksAndMessages(RETRY)
    }

    // Runs a new core on the TUN that is kept, with the plan it was built from.
    // No new TUN, no validation process and no state broadcast per attempt.
    private fun retryBlocked() {
        val plan = activePlan ?: return
        val tun = retainedTun ?: return
        if (destroyed || !blocked || client != null) return
        attempts++
        try {
            startCore(plan, tun)
        } catch (error: Throwable) {
            fail("${getString(R.string.text_could_not_start_vpn)} (${error.javaClass.simpleName})")
        }
    }

    // A live core already hears about networks from the collector. In a blocked
    // episode a network also starts the delays again at 1 s, and between
    // attempts it brings the next one forward.
    private fun networkAvailable() {
        if (destroyed || !blocked) return
        backoff.reset()
        if (client == null) scheduleRetry(NETWORK_SETTLE_MS)
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
            val connectivity = getSystemService(ConnectivityManager::class.java)
            collector?.let { runCatching { connectivity.unregisterNetworkCallback(it) } }
            availability?.let { runCatching { connectivity.unregisterNetworkCallback(it) } }
            collector = null
            availability = null
        }
        worker.shutdown()
        super.onDestroy()
    }

    private fun publishState(state: Int) {
        if (owner !== this) return
        // The state that was current before this one is what tells a new connection from a repeat of the same one.
        if (state == 2 && currentState != 2) sessionStartedAt = SystemClock.elapsedRealtime()
        else if (state != 2) sessionStartedAt = 0L
        currentState = state
        sendBroadcast(Intent(ACTION_STATE).setPackage(packageName))
        QuickTileService.refresh(this)
    }

    private fun showFailure(reason: String) {
        logEvent(reason, LogLines.ERROR)
        // The desktop says an error as a toast in its window whenever the window is in view, and tells the tray as well only when it is not
        // (Main.qml: toast.show(msg), then errorsGoToTray). While the app is on the screen the notice below is silent, and Home has nothing to
        // say of a failure but that the VPN is off, so the reason goes to the app, which says it in its own toast. Away, the notice alerts.
        if (appVisible) sendBroadcast(Intent(ACTION_FAILURE).setPackage(packageName).putExtra(EXTRA_REASON, reason))
        getSystemService(NotificationManager::class.java).notify(FAILURE_ID,
            NotificationCompat.Builder(this, FAILURE_CHANNEL).setSmallIcon(android.R.drawable.ic_dialog_alert)
                .setContentTitle(getString(R.string.text_vpn_disconnected)).setContentText(reason)
                .setStyle(NotificationCompat.BigTextStyle().bigText(getString(R.string.vpn_failure_details, reason)))
                .setContentIntent(openApp()).setAutoCancel(true).setSilent(appVisible).setPriority(NotificationCompat.PRIORITY_HIGH).build())
    }

    // Posted once per blocked episode, silently when it carries one on or while the app is on the screen. Its
    // actions are the ongoing notification's own PendingIntents.
    private fun showBlocked(reason: String, silent: Boolean) {
        getSystemService(NotificationManager::class.java).notify(BLOCKED_ID,
            NotificationCompat.Builder(this, FAILURE_CHANNEL).setSmallIcon(android.R.drawable.ic_dialog_alert)
                .setContentTitle(getString(R.string.vpn_blocked_title)).setContentText(reason)
                .setStyle(NotificationCompat.BigTextStyle().bigText(getString(R.string.vpn_blocked_details, reason)))
                .setContentIntent(openApp()).setAutoCancel(true).setOnlyAlertOnce(true).setSilent(silent || appVisible)
                .setCategory(NotificationCompat.CATEGORY_ERROR).setPriority(NotificationCompat.PRIORITY_HIGH)
                .addAction(0, getString(R.string.vpn_reconnect), serviceAction(ACTION_START, 1))
                .addAction(0, getString(R.string.text_disconnect_vpn), serviceAction(ACTION_STOP, 2)).build())
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

    // A line of the log the Logs page shows, "yyyy-MM-dd HH:mm:ss<TAB>LEVEL<TAB>message" as the desktop writes it
    // (LogLines), when logging is on. Called from this service's worker and from the core's own threads, which is what
    // LogFile's lock is for; a failure to write is never a failure to connect.
    private fun logEvent(message: String, level: String = LogLines.INFO) {
        if (!prefs.getBoolean("logging", true)) return
        runCatching {
            LogFile.append(File(filesDir, LogFile.NAME),
                LogLines.entry(System.currentTimeMillis(), java.time.ZoneId.systemDefault(), level, message))
        }
    }
}
