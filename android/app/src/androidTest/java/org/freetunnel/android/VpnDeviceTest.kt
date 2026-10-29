package org.freetunnel.android

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.RouteInfo
import android.net.VpnService
import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import com.adguard.trusttunnel.VpnServiceConfig
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import java.io.File
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.security.KeyStore
import java.util.concurrent.ConcurrentLinkedQueue
import kotlin.concurrent.thread

class VpnDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private val prefs get() = context.getSharedPreferences("freetunnel", Context.MODE_PRIVATE)
    private var oldPreferences: Map<String, *> = emptyMap<String, Any>()
    private val source = """
        vpn_mode = "general"
        killswitch_enabled = true
        exclusions = []
        [endpoint] # import comment
        hostname = "vpn.invalid"
        addresses = ["192.0.2.1:443"]
        username = "test"
        password = "test"
        upstream_protocol = "http2"
        has_ipv6 = false
        [listener.tun] # multiline import
        included_routes = [
          "0.0.0.0/0", "::/0",
        ]
        excluded_routes = []
        mtu_size = 1350
    """.trimIndent()
    private fun network() = context.getSystemService(ConnectivityManager::class.java).let { cm ->
        cm.allNetworks.firstOrNull { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) == true }
    }
    // On a timeout, the message lists every change in snapshot() during the wait.
    private fun eventually(timeoutMs: Long = 40_000, condition: () -> Boolean) {
        val start = SystemClock.elapsedRealtime()
        val seen = ArrayList<String>()
        var last = ""
        while (true) {
            if (condition()) return
            val elapsed = SystemClock.elapsedRealtime() - start
            val now = runCatching { snapshot() }.getOrElse { "no snapshot: $it" }
            if (now != last) { seen.add("+$elapsed ms $now"); last = now }
            if (elapsed >= timeoutMs) break
            SystemClock.sleep(50)
        }
        val omitted = if (seen.size > 20) "(${seen.size - 20} earlier omitted)\n" else ""
        fail("Condition did not become true in $timeoutMs ms. Seen, oldest first:\n$omitted" + seen.takeLast(20).joinToString("\n"))
    }
    @Before fun setup() {
        context.getSystemService(android.app.NotificationManager::class.java).run { cancel(48); cancel(49) }
        oldPreferences = prefs.all
        prefs.edit().clear().putBoolean("logging", false).commit()
    }
    // A service that is up and not on its way out. A stop sent to one that is going, or to none, makes a new one for the moment it takes to be
    // told (startForegroundService), and its onCreate cancels alert 49, which a test that comes next may have just posted.
    private fun serviceIsUp(): Boolean {
        val owner = serviceOwner() ?: return false
        return !(runCatching { serviceField("destroyed").getBoolean(owner) }.getOrDefault(false))
    }
    // Returns once no service has been there for this long.
    private fun noServiceFor(quietMs: Long) {
        val deadline = SystemClock.elapsedRealtime() + 30_000
        var since = SystemClock.elapsedRealtime()
        while (SystemClock.elapsedRealtime() - since < quietMs) {
            if (serviceOwner() != null) since = SystemClock.elapsedRealtime()
            if (SystemClock.elapsedRealtime() > deadline) fail("A service is still there: ${snapshot()}")
            SystemClock.sleep(50)
        }
    }
    @After fun cleanup() {
        if (serviceIsUp()) FreeTunnelVpnService.stop(context)
        eventually { network() == null && FreeTunnelVpnService.state(context) == 0 && serviceOwner() == null }
        val editor = prefs.edit().clear()
        oldPreferences.forEach { (key, value) -> when (value) {
            is Boolean -> editor.putBoolean(key, value)
            is String -> editor.putString(key, value)
            is Int -> editor.putInt(key, value)
            is Long -> editor.putLong(key, value)
            is Float -> editor.putFloat(key, value)
            is Set<*> -> { @Suppress("UNCHECKED_CAST") editor.putStringSet(key, value as Set<String>) }
        } }
        editor.commit()
    }
    @Test fun consentEntryIsPrivate() {
        assertFalse(context.packageManager.getActivityInfo(ComponentName(context, VpnPermissionActivity::class.java), 0).exported)
    }
    @Test fun persistedConnectedValueCannotSpoofState() {
        prefs.edit().putInt("vpn_state", 2).commit()
        assertEquals(0, FreeTunnelVpnService.state(context))
    }
    @Test fun adapterAndNativeFactoryAcceptSerializedTablesAndRejectBadCoreValues() {
        val plan = RoutingConfig.prepare(source, false, false, emptyList(), emptyList(), true)
        assertNotNull(VpnServiceConfig.parseToml(plan.toml))
        NativeConfigValidator.validate(context, plan.toml)
        val invalid = source.replace("has_ipv6 = false", "has_ipv6 = false\nclient_random = \"abcd/\"")
        assertThrows(IllegalArgumentException::class.java) { NativeConfigValidator.validate(context, invalid) }
        assertNull(network())
    }
    @Test fun adapterAndNativeFactoryPreserveEscapedPasswordsAndPem() {
        val certificate = instrumentation.context.assets.open("test-endpoint.pem").bufferedReader().use { it.readText() }
        val passwords = listOf("ending\\", "ending\"", "ending,", "ending}", "ending#", "mixed\\\",}#\\")
        for (password in passwords) {
            val input = source.replace("password = \"test\"", "password = '" + password + "'")
                .replace("has_ipv6 = false", "has_ipv6 = false\ncertificate = \"\"\"\n" + certificate + "\"\"\"\n")
            val plan = RoutingConfig.prepare(input, false, false, emptyList(), emptyList(), true)
            val decoded = VpnServiceConfig.parseToml(plan.toml)
            assertNotNull("Adapter must accept escaped credentials and PEM", decoded)
            assertEquals(password, org.tomlj.Toml.parse(plan.toml).getString("endpoint.password"))
            assertEquals(certificate, org.tomlj.Toml.parse(plan.toml).getString("endpoint.certificate"))
            NativeConfigValidator.validate(context, plan.toml)
        }
        assertNull(network())
    }
    @Test fun anEndpointExportBecomesAConfigTheCoreAccepts() {
        val certificate = instrumentation.context.assets.open("test-endpoint.pem").bufferedReader().use { it.readText() }
        // The endpoint's export, with the server's key after its certificate, as a
        // combined certificate file holds it. The key's label is put together here so
        // that no line of the source reads as a key, and its bytes only have a key's shape.
        val label = "PRIVATE" + " KEY"
        val combined = "$certificate-----BEGIN $label-----\nMAoCAQAwAwYBAAQA\n-----END $label-----\n"
        val export = "# This file was automatically generated by endpoint and could be used in vpn client.\n" +
            "hostname = \"vpn.invalid\"\naddresses = [\"192.0.2.1:443\"]\ncustom_sni = \"\"\nhas_ipv6 = true\n" +
            "username = \"test\"\npassword = \"test\"\nclient_random_prefix = \"\"\nclient_random_auth_key = \"\"\n" +
            "skip_verification = false\ncertificate = \"\"\"\n$combined\"\"\"\nupstream_protocol = \"http2\"\n" +
            "anti_dpi = false\nname = \"Exported\"\ndns_upstreams = []\n"
        val imported = RoutingConfig.cleanImport(export)
        assertEquals("Exported", imported.name)
        assertTrue(imported.certificateIgnored)
        val plan = RoutingConfig.prepare(imported.toml, false, false, emptyList(), emptyList(), true)
        assertNotNull("Adapter must accept a converted export", VpnServiceConfig.parseToml(plan.toml))
        assertEquals(certificate, org.tomlj.Toml.parse(plan.toml).getString("endpoint.certificate"))
        assertFalse(plan.toml.contains(label))
        assertFalse(plan.toml.contains("MAoCAQAwAwYBAAQA"))
        NativeConfigValidator.validate(context, plan.toml)
        assertNull(network())
    }
    @Test fun adapterRequiredFieldsAreRejectedByTheSharedSaveValidator() {
        for (missing in listOf("excluded_routes = []", "mtu_size = 1350")) {
            assertThrows(IllegalStateException::class.java) { NativeConfigValidator.validate(context, source.replace(missing, "")) }
        }
        assertNull(network())
    }
    @Test fun corruptedCiphertextIsNeverReplaced() {
        val isolated = context.getSharedPreferences("damaged-config-test", 0)
        val ciphertext = "AAAA"
        isolated.edit().clear().putString("configs_encrypted", ciphertext).commit()
        val keys = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        val before = keys.aliases().toList().toSet()
        assertThrows(IllegalStateException::class.java) { ConfigStore.read(isolated) }
        assertFalse(ConfigStore.write(isolated, "[]"))
        assertEquals(ciphertext, isolated.getString("configs_encrypted", null))
        keys.load(null)
        assertEquals(before, keys.aliases().toList().toSet())
        isolated.edit().clear().commit()
    }
    @Test fun encryptedConfigsRoundTripAndPlaintextMigrationRemainsCompatible() {
        val isolated = context.getSharedPreferences("cipher-config-test", 0)
        val json = """[{"name":"Private","toml":"password = very-private"}]"""
        isolated.edit().clear().putString("configs", json).commit()
        assertEquals(json, ConfigStore.read(isolated))
        assertTrue(ConfigStore.write(isolated, json))
        assertFalse(isolated.contains("configs"))
        assertFalse(isolated.getString("configs_encrypted", "")!!.contains("very-private"))
        assertEquals(json, ConfigStore.read(isolated))
        isolated.edit().clear().commit()
    }
    private fun splitStore() = SplitPrefs(prefs)
    private fun profiles() = SplitStore.load(splitStore(), null)
    // The rules of a plan's TOML, the destinations the core is told about.
    private fun exclusions(toml: String): List<Any> = org.tomlj.Toml.parse(toml).getArray("exclusions")!!.toList()

    @Test fun staleAppRulesFallBackToFullVpnWithWarning() {
        prefs.edit().putBoolean("split_enabled", true).putBoolean("through", true).commit()
        SplitStore.edit(splitStore(), null) { p -> p.addApp("Default", "not.installed.package"); p.addApp("Default", context.packageName) }
        val plan = RoutingConfig.prepare(context, source, "Default")
        assertTrue(plan.apps.isEmpty()); assertFalse(plan.allowApps); assertNotNull(plan.warning)
        // The profile keeps the application that is gone, which is how it can be taken out of the list.
        assertTrue(profiles().apps("Default").contains("not.installed.package"))
    }
    @Test fun anotherProfilesApplicationsDoNotCountForAConfig() {
        prefs.edit().putBoolean("split_enabled", true).putBoolean("through", true).commit()
        val other = context.packageManager.getInstalledApplications(0).map { it.packageName }.first { it != context.packageName }
        SplitStore.edit(splitStore(), null) { p -> p.add("Work"); p.addApp("Work", other) }
        val used = RoutingConfig.prepare(context, source, "Work")
        assertEquals(listOf(other), used.apps); assertTrue(used.allowApps)
        val unused = RoutingConfig.prepare(context, source, "Default")
        assertTrue("Default lists none", unused.apps.isEmpty()); assertNotNull("so Through VPN has nothing", unused.warning)
    }
    @Test fun replacesTunWithoutDisconnectedStateAndBlocksUnsupportedIpv6() {
        assertNull("Grant ACTIVATE_VPN before running device tests", VpnService.prepare(context))
        val sink = ServerSocket(0, 8, InetAddress.getByName("127.0.0.1"))
        val sockets = ConcurrentLinkedQueue<Socket>()
        val accepting = thread(name = "test-tls-sink", isDaemon = true) {
            runCatching { while (!sink.isClosed) sockets.add(sink.accept()) }
        }
        val config = source.replace("192.0.2.1:443", "127.0.0.1:${sink.localPort}")
        val states = ConcurrentLinkedQueue<Int>()
        val receiver = object : android.content.BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) { states.add(FreeTunnelVpnService.state(context)) }
        }
        androidx.core.content.ContextCompat.registerReceiver(context, receiver, android.content.IntentFilter(FreeTunnelVpnService.ACTION_STATE),
            androidx.core.content.ContextCompat.RECEIVER_NOT_EXPORTED)
        try {
            FreeTunnelVpnService.start(context, config)
            eventually { network() != null && FreeTunnelVpnService.state(context) != 0 }
            val cm = context.getSystemService(ConnectivityManager::class.java)
            val properties = cm.getLinkProperties(network()!!)!!
            assertFalse(properties.linkAddresses.any { it.address is java.net.Inet6Address })
            assertFalse(properties.routes.any { it.destination.address is java.net.Inet6Address && it.type == RouteInfo.RTN_UNICAST })
            // Android sets the VPN's capabilities on its own schedule, after the
            // network itself appears.
            eventually { network()?.let { cm.getNetworkCapabilities(it)?.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED) } == true }
            instrumentation.startActivitySync(Intent(context, MainActivity::class.java).setAction(Intent.ACTION_VIEW)
                .putExtra("tile_connect", true).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            instrumentation.waitForIdleSync()
            assertNotNull("Exported launcher must not stop VPN", network())
            states.clear()
            repeat(4) { FreeTunnelVpnService.start(context, config.replace("1350", (1400 + it).toString())) }
            eventually { network()?.let { cm.getLinkProperties(it)?.mtu == 1403 } == true }
            assertFalse("Replacement must never publish disconnected", states.contains(0))
            FreeTunnelVpnService.start(context, config.replace("has_ipv6 = false", "has_ipv6 = false\nclient_random = \"abcd/\""))
            SystemClock.sleep(750)
            assertNotNull("Rejected replacement must retain old TUN", network())
            assertEquals(1403, cm.getLinkProperties(network()!!)!!.mtu)
            FreeTunnelVpnService.stop(context)
            eventually { network() == null && FreeTunnelVpnService.state(context) == 0 }
        } finally {
            context.unregisterReceiver(receiver)
            FreeTunnelVpnService.stop(context)
            sink.close(); accepting.join(500); sockets.forEach { it.close() }
        }
    }
    private fun serviceOwner(): Any? = FreeTunnelVpnService::class.java.getDeclaredField("owner").apply { isAccessible = true }.get(null)
    private fun nativeClient(): com.adguard.trusttunnel.VpnClient? {
        val owner = serviceOwner() ?: return null
        return FreeTunnelVpnService::class.java.getDeclaredField("client").apply { isAccessible = true }.get(owner)
            as? com.adguard.trusttunnel.VpnClient
    }

    private fun alert(id: Int) = context.getSystemService(android.app.NotificationManager::class.java)
        .activeNotifications.firstOrNull { it.id == id }
    private fun tunName() = network()?.let { context.getSystemService(ConnectivityManager::class.java).getLinkProperties(it)?.interfaceName }
    private fun serviceField(name: String) = FreeTunnelVpnService::class.java.getDeclaredField(name).apply { isAccessible = true }
    private fun useBackoff(backoff: ReconnectBackoff) = serviceField("backoff").set(serviceOwner(), backoff)
    // About 10 min to the next attempt, and 1 s again once reset.
    private fun grownBackoff() = ReconnectBackoff(1_000, 600_000).apply { repeat(10) { next() } }
    // Returns once the service's worker has run everything queued so far.
    private fun awaitWorker() {
        (serviceField("worker").get(serviceOwner()) as java.util.concurrent.ExecutorService)
            .submit {}.get(30, java.util.concurrent.TimeUnit.SECONDS)
    }
    // Waits for the client of the next attempt, about 1 s away for an automatic
    // one, and then for the worker to finish starting it. The service publishes
    // the client before start(), and the 1.1.7 core reports its own 1 from
    // inside start() (vpn_listen waits for the task that raised it), so only
    // from here on does a state the test reports reach the service after that
    // 1, instead of being undone by it.
    private fun nextClient(after: com.adguard.trusttunnel.VpnClient, timeoutMs: Long = 10_000): com.adguard.trusttunnel.VpnClient {
        var next: com.adguard.trusttunnel.VpnClient? = null
        eventually(timeoutMs) { next = nativeClient(); next != null && next !== after }
        awaitWorker()
        return next!!
    }
    private val driven = ArrayList<String>()
    private fun identity(client: Any?) = if (client == null) "none" else "@" + Integer.toHexString(System.identityHashCode(client))
    // Reports state through the callback the JNI core uses, and remembers it for snapshot().
    private fun inject(client: com.adguard.trusttunnel.VpnClient, state: Int) {
        driven.add("${identity(client)}<-$state")
        client.onStateChanged(state)
    }
    private fun alertKind() = alert(49)?.notification?.let {
        // What NotificationCompat's setSilent sets on a notification outside a group.
        if (it.groupAlertBehavior == android.app.Notification.GROUP_ALERT_SUMMARY) "silent" else "alerting"
    } ?: "none"
    // What the service holds now, for failure messages. client is the core it
    // runs, driven the last states the test reported and from which client.
    private fun snapshot(): String {
        val owner = serviceOwner()
        val service = if (owner == null) "no service" else {
            fun read(name: String) = runCatching { serviceField(name).get(owner) }.getOrNull()
            "blocked=${read("blocked")} generation=${read("generation")} attempts=${read("attempts")} client=${identity(read("client"))}"
        }
        return "state=${FreeTunnelVpnService.state(context)} $service driven=${driven.takeLast(3).ifEmpty { listOf("none") }.joinToString(",")}" +
            " alert49=${alertKind()} vpn=${network() != null}"
    }
    // Reports 2 from client's core and returns once the service has handled
    // it. Android removes a cancelled notification asynchronously, so alert 49
    // is waited for.
    private fun reportConnected(client: com.adguard.trusttunnel.VpnClient) {
        inject(client, 2)
        awaitWorker()
        assertEquals("The service must publish the 2: ${snapshot()}", 2, FreeTunnelVpnService.state(context))
        assertFalse("After a 2 traffic must not stay blocked: ${snapshot()}", serviceField("blocked").getBoolean(serviceOwner()))
        eventually { alert(49) == null }
    }
    // Tells the service, as Android would, that a network besides the VPN appeared.
    private fun networkAppears() {
        val cm = context.getSystemService(ConnectivityManager::class.java)
        val underlying = cm.allNetworks.firstOrNull { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) == false }
        assertNotNull("The emulator needs a network besides the VPN", underlying)
        (serviceField("availability").get(serviceOwner()) as ConnectivityManager.NetworkCallback).onAvailable(underlying!!)
    }
    private val log get() = java.io.File(context.filesDir, "freetunnel.log")
    private fun logged(text: String) = if (log.exists()) log.readLines().count { text in it } else 0
    private val lostReason get() = context.getString(R.string.text_connection_to_the_server_was_lost)
    // Runs block against a local server that accepts and never answers. The
    // real core of each client keeps running underneath what a test reports:
    // it reports 1 from inside start(), and as its pings never get a reply,
    // 0 about 25 s later (five pings of 5 s). Tests report states only after
    // nextClient(), and are done with a client long before its core gives up.
    private fun withSink(block: (config: String) -> Unit) {
        assertNull("Grant ACTIVATE_VPN before running device tests", VpnService.prepare(context))
        val sink = ServerSocket(0, 8, InetAddress.getByName("127.0.0.1"))
        val sockets = ConcurrentLinkedQueue<Socket>()
        val accepting = thread(isDaemon = true) { runCatching { while (!sink.isClosed) sockets.add(sink.accept()) } }
        try {
            block(source.replace("192.0.2.1:443", "127.0.0.1:${sink.localPort}"))
        } finally {
            FreeTunnelVpnService.stop(context)
            sink.close(); accepting.join(500); sockets.forEach { it.close() }
        }
    }
    // Runs block while sampling network() every 20 ms, and returns how often
    // there was no VPN network.
    private fun tunGaps(block: () -> Unit): Int {
        val gaps = java.util.concurrent.atomic.AtomicInteger()
        val sampling = java.util.concurrent.atomic.AtomicBoolean(true)
        val sampler = thread(isDaemon = true) {
            while (sampling.get()) { if (network() == null) gaps.incrementAndGet(); SystemClock.sleep(20) }
        }
        try { block() } finally { sampling.set(false); sampler.join() }
        return gaps.get()
    }
    private fun recordStates(): Pair<ConcurrentLinkedQueue<Int>, android.content.BroadcastReceiver> {
        val states = ConcurrentLinkedQueue<Int>()
        val receiver = object : android.content.BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) { states.add(FreeTunnelVpnService.state(context)) }
        }
        androidx.core.content.ContextCompat.registerReceiver(context, receiver, android.content.IntentFilter(FreeTunnelVpnService.ACTION_STATE),
            androidx.core.content.ContextCompat.RECEIVER_NOT_EXPORTED)
        return states to receiver
    }
    // Starts with logging on, loses the connection and lets the first automatic
    // attempt connect. Returns that attempt's client.
    private fun reconnectAutomatically(config: String): com.adguard.trusttunnel.VpnClient {
        prefs.edit().putBoolean("logging", true).commit()
        log.delete()
        FreeTunnelVpnService.start(context, config)
        eventually { network() != null && nativeClient() != null }
        val first = nativeClient()!!
        inject(first, 0)
        val attempt = nextClient(first)
        reportConnected(attempt)
        return attempt
    }

    @Test fun fatalNativeDisconnectRetainsGuardAndExplicitStopReleasesIt() {
        FreeTunnelVpnService.start(context, source)
        eventually { network() != null && nativeClient() != null }
        val active = nativeClient()!!
        // Same callback boundary used by the JNI core on a fatal error.
        inject(active, 0)
        eventually { FreeTunnelVpnService.state(context) == 6 }
        assertNotNull(network())
        eventually { alert(49) != null }
        val notification = context.getSystemService(android.app.NotificationManager::class.java)
            .activeNotifications.first { it.id == 47 }.notification
        assertEquals(2, notification.actions.size)
        FreeTunnelVpnService.start(context, source + "\ninvalid TOML")
        eventually { context.getSystemService(android.app.NotificationManager::class.java).activeNotifications.any { it.id == 48 } }
        assertEquals(6, FreeTunnelVpnService.state(context))
        assertNotNull("Rejected settings must retain the blocking guard", network())
        FreeTunnelVpnService.stop(context)
        eventually { network() == null && FreeTunnelVpnService.state(context) == 0 }
        eventually { alert(49) == null && serviceOwner() == null }
        // Past the first retry delay, the TUN and the service are still gone:
        // nothing starts them again after a stop.
        SystemClock.sleep(1_500)
        assertNull(network())
        assertNull(serviceOwner())
    }

    // The desktop sends an error to its tray only while its window is not in view (Main.qml, errorsGoToTray) and never puts a system message
    // over its own open window. The alert of a blocked connection is posted without a sound or a pop-over while the app is on the screen, whose
    // Home already says it and offers Reconnect, and alerts as before when it is not.
    @Test fun theBlockedAlertDoesNotPopOverTheOpenApp() {
        FreeTunnelVpnService.setAppVisible(true)
        try {
            FreeTunnelVpnService.start(context, source)
            eventually { network() != null && nativeClient() != null }
            inject(nativeClient()!!, 0)
            eventually { FreeTunnelVpnService.state(context) == 6 && alert(49) != null }
            assertEquals("Silent while the app is on the screen", "silent", alertKind())
        } finally {
            FreeTunnelVpnService.setAppVisible(false)
            FreeTunnelVpnService.stop(context)
            eventually { network() == null && FreeTunnelVpnService.state(context) == 0 }
            eventually { alert(49) == null && serviceOwner() == null }
        }
    }

    // A failure that ends the connection (the config is refused, say) is a notice in the shade, which alerts when the app is away. While the app
    // is on the screen it is silent, and the reason is sent to the app, which says it in its toast as the desktop's window does (Main.qml:
    // toast.show, with the tray told as well only when the window is not in view); with the app away nothing is sent.
    @Test fun aFailureIsSentToTheOpenAppAndAlertsOnlyWhenTheAppIsAway() {
        fun kind() = alert(48)?.notification?.let {
            if (it.groupAlertBehavior == android.app.Notification.GROUP_ALERT_SUMMARY) "silent" else "alerting"
        } ?: "none"
        fun gone() = eventually { network() == null && FreeTunnelVpnService.state(context) == 0 && serviceOwner() == null }
        val reasons = ConcurrentLinkedQueue<String>()
        val receiver = object : android.content.BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) { intent.getStringExtra(FreeTunnelVpnService.EXTRA_REASON)?.let { reasons.add(it) } }
        }
        androidx.core.content.ContextCompat.registerReceiver(context, receiver, android.content.IntentFilter(FreeTunnelVpnService.ACTION_FAILURE),
            androidx.core.content.ContextCompat.RECEIVER_NOT_EXPORTED)
        try {
            FreeTunnelVpnService.setAppVisible(true)
            FreeTunnelVpnService.start(context, source + "\ninvalid TOML")
            eventually { alert(48) != null && reasons.isNotEmpty() }
            assertEquals("Silent while the app is on the screen", "silent", kind())
            assertEquals("and the app is told what the notice says", alert(48)!!.notification.extras.getCharSequence(android.app.Notification.EXTRA_TEXT).toString(),
                reasons.first())
            gone()
            context.getSystemService(android.app.NotificationManager::class.java).cancel(48)
            reasons.clear()
            FreeTunnelVpnService.setAppVisible(false)
            FreeTunnelVpnService.start(context, source + "\ninvalid TOML")
            eventually { alert(48) != null }
            SystemClock.sleep(500)
            assertEquals("It alerts when the app is away", "alerting", kind())
            assertTrue("and tells no app: $reasons", reasons.isEmpty())
        } finally {
            context.unregisterReceiver(receiver)
            FreeTunnelVpnService.setAppVisible(false)
            FreeTunnelVpnService.stop(context)
            gone()
        }
    }

    // The time Home shows under the logo counts from when the connection last became connected: nothing before the first 2, a repeated 2 changes
    // nothing, the next 2 after the core recovered starts again, as the desktop restarts its timer, and a stop clears it.
    @Test fun theSessionStartIsWhenTheConnectionLastCameUp() = withSink { config ->
        assertEquals("No session before a connection", 0L, FreeTunnelVpnService.sessionStart())
        FreeTunnelVpnService.start(context, config)
        eventually { network() != null && nativeClient() != null }
        awaitWorker()
        val first = nativeClient()!!
        assertEquals("Connecting is no session", 0L, FreeTunnelVpnService.sessionStart())
        val before = SystemClock.elapsedRealtime()
        reportConnected(first)
        val started = FreeTunnelVpnService.sessionStart()
        assertTrue("A 2 starts it: $started after $before", started >= before && started <= SystemClock.elapsedRealtime())
        // The same 2 again is not a new connection.
        SystemClock.sleep(50)
        inject(first, 2)
        awaitWorker()
        assertEquals(started, FreeTunnelVpnService.sessionStart())
        // The core recovering is not connected; its next 2 is a new connection.
        inject(first, 3)
        awaitWorker()
        assertEquals("Recovering is not a session", 0L, FreeTunnelVpnService.sessionStart())
        SystemClock.sleep(20)
        inject(first, 2)
        awaitWorker()
        assertTrue("A new 2 starts it again", FreeTunnelVpnService.sessionStart() > started)
        FreeTunnelVpnService.stop(context)
        eventually { FreeTunnelVpnService.state(context) == 0 }
        assertEquals("A stop clears it", 0L, FreeTunnelVpnService.sessionStart())
    }

    @Test fun fatalDisconnectReconnectsAutomaticallyOnTheKeptTun() = withSink { config ->
        prefs.edit().putBoolean("logging", true).commit()
        log.delete()
        FreeTunnelVpnService.start(context, config)
        eventually { network() != null && tunName() != null && nativeClient() != null }
        val tun = network()
        val name = tunName()
        val first = nativeClient()!!
        val (states, receiver) = recordStates()
        try {
            val gaps = tunGaps {
                inject(first, 0)
                eventually { FreeTunnelVpnService.state(context) == 6 && alert(49) != null }
                val blocked = alert(49)!!
                assertEquals("freetunnel_vpn_failure", blocked.notification.channelId)
                assertEquals(2, blocked.notification.actions.size)
                assertEquals("The first alert of an episode alerts", android.app.Notification.GROUP_ALERT_ALL,
                    blocked.notification.groupAlertBehavior)
                eventually { logged(lostReason) == 1 }
                // The line is the desktop's: date and time, a level (a warning here, traffic is blocked) and the message, with tabs between.
                val warning = log.readLines().first { lostReason in it }
                assertTrue(warning, Regex("\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\tWARN\t" + Regex.escape(lostReason) + ".*").matches(warning))
                // The first delay is about 1 s.
                val second = nextClient(first)
                // Neither the attempt nor the core's own Connecting may show.
                inject(second, 1)
                awaitWorker()
                assertEquals("The blocked state must hold through the attempt", 6, FreeTunnelVpnService.state(context))
                assertEquals(tun, network())
                assertEquals("The attempt must reuse the kept TUN, not establish one", name, tunName())
                // Lets every broadcast sent so far arrive before counting them.
                SystemClock.sleep(500)
                val broadcasts = states.size
                // A failed attempt leads to the next, about 2 s later, with no
                // second alert, log line or broadcast.
                inject(second, 0)
                val third = nextClient(second)
                assertEquals(6, FreeTunnelVpnService.state(context))
                assertEquals(name, tunName())
                assertEquals("The alert is posted once per episode", blocked.postTime, alert(49)!!.postTime)
                assertEquals("The reason is logged once per episode", 1, logged(lostReason))
                SystemClock.sleep(500)
                assertEquals("Attempts must not broadcast: $states", broadcasts, states.size)
                reportConnected(third)
                assertEquals(1, logged("Reconnected automatically"))
                assertTrue("at INFO", "\tINFO\tReconnected automatically" in log.readLines().first { "Reconnected automatically" in it })
            }
            assertEquals("The TUN must stay up through the attempts", 0, gaps)
            assertEquals(name, tunName())
            assertFalse(states.contains(0))
        } finally {
            context.unregisterReceiver(receiver)
            log.delete()
        }
    }

    @Test fun aDropSoonAfterAnAutomaticReconnectCarriesTheEpisodeOn() = withSink { config ->
        try {
            val attempt = reconnectAutomatically(config)
            // A reset would bring the next delay back to 1 s.
            useBackoff(grownBackoff())
            // Within 10 s of reconnecting, as from a server that accepts and then drops.
            inject(attempt, 0)
            eventually { FreeTunnelVpnService.state(context) == 6 }
            // What NotificationCompat's setSilent sets on a notification outside a group.
            eventually(10_000) { alert(49)?.notification?.groupAlertBehavior == android.app.Notification.GROUP_ALERT_SUMMARY }
            awaitWorker()
            assertEquals("The reason is not logged again", 1, logged(lostReason))
            SystemClock.sleep(3_000)
            assertNull("The delay must keep growing, not start again at 1 s", nativeClient())
            // A network still brings an attempt forward. Its reconnect isn't logged again.
            networkAppears()
            reportConnected(nextClient(attempt))
            assertEquals("The reconnect is logged once per episode", 1, logged("Reconnected automatically"))
        } finally {
            log.delete()
        }
    }

    @Test fun aLongSessionEndsTheEpisodeEvenAfterTheCoreRecoveredByItself() = withSink { config ->
        try {
            val attempt = reconnectAutomatically(config)
            // As if this session began a minute ago. Then the core recovers by
            // itself and soon loses the connection.
            serviceField("connectedAt").setLong(serviceOwner(), SystemClock.elapsedRealtime() - 60_000)
            inject(attempt, 4)
            inject(attempt, 2)
            inject(attempt, 0)
            eventually { FreeTunnelVpnService.state(context) == 6 && alert(49) != null }
            awaitWorker()
            assertEquals("A new episode logs its reason again", 2, logged(lostReason))
            assertEquals("A new episode alerts again", android.app.Notification.GROUP_ALERT_ALL,
                alert(49)!!.notification.groupAlertBehavior)
        } finally {
            log.delete()
        }
    }

    @Test fun anExplicitReconnectStartsTheNextEpisodeAfresh() = withSink { config ->
        try {
            val attempt = reconnectAutomatically(config)
            useBackoff(grownBackoff())
            FreeTunnelVpnService.reconnect(context)
            // A full start, through the validation process and a new TUN.
            val explicit = nextClient(attempt, 40_000)
            reportConnected(explicit)
            // Within 10 s, but this session didn't begin with an automatic reconnect.
            inject(explicit, 0)
            eventually { FreeTunnelVpnService.state(context) == 6 && alert(49) != null }
            awaitWorker()
            assertEquals("A new episode logs its reason again", 2, logged(lostReason))
            assertEquals("A new episode alerts again", android.app.Notification.GROUP_ALERT_ALL,
                alert(49)!!.notification.groupAlertBehavior)
            // And its delays start again at 1 s.
            nextClient(explicit)
        } finally {
            log.delete()
        }
    }

    @Test fun networkAppearingRetriesWithoutWaitingOutTheBackoff() = withSink { config ->
        FreeTunnelVpnService.start(context, config)
        eventually { network() != null && tunName() != null && nativeClient() != null }
        val tun = network()
        val name = tunName()
        val first = nativeClient()!!
        useBackoff(ReconnectBackoff(600_000, 600_000))
        inject(first, 0)
        eventually { FreeTunnelVpnService.state(context) == 6 && nativeClient() == null }
        SystemClock.sleep(2_000)
        assertNull("The next attempt waits out the long backoff", nativeClient())
        networkAppears()
        val attempt = nextClient(first)
        assertEquals(6, FreeTunnelVpnService.state(context))
        assertEquals(tun, network())
        assertEquals(name, tunName())
        // A network that appears during an attempt starts the delays again at
        // 1 s, so the attempt after a failed one comes about 1 s later.
        useBackoff(grownBackoff())
        networkAppears()
        inject(attempt, 0)
        nextClient(attempt)
        assertEquals(6, FreeTunnelVpnService.state(context))
    }

    @Test fun openingTheAppClearsTheAlertOfAKilledProcess() {
        // What a process killed while blocked leaves behind: alert 49 and no service.
        val manager = context.getSystemService(android.app.NotificationManager::class.java)
        manager.createNotificationChannel(android.app.NotificationChannel("stale-alert-test", "Test", android.app.NotificationManager.IMPORTANCE_LOW))
        // The test before this one may have a service on its way (see serviceIsUp), whose start cancels alert 49 and which was seen to
        // do so to this alert: the process has none when the alert of a killed one is left behind.
        noServiceFor(1_500)
        manager.notify(49, android.app.Notification.Builder(context, "stale-alert-test").setSmallIcon(android.R.drawable.ic_dialog_alert).build())
        eventually { alert(49) != null }
        val activity = instrumentation.startActivitySync(Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        try {
            eventually(10_000) { alert(49) == null }
        } finally {
            instrumentation.runOnMainSync { activity.finish() }
            manager.deleteNotificationChannel("stale-alert-test")
        }
    }

    // The desktop's Home has no control to reconnect now: while its tunnel reconnects it says Connecting… and the logo is the one control.
    // Here the service reconnects by itself with its backoff (the next try is far off, so that only a tap could end this), Home says
    // the same, keeps the alert of the live episode, and a tap on the logo disconnects, as it does a tunnel that is up or connecting.
    @Test fun blockedHomeScreenSaysConnectingHasNoReconnectAndTheLogoDisconnects() = withSink { config ->
        FreeTunnelVpnService.start(context, config)
        eventually { network() != null && tunName() != null && nativeClient() != null }
        useBackoff(ReconnectBackoff(600_000, 600_000))
        inject(nativeClient()!!, 0)
        eventually { FreeTunnelVpnService.state(context) == 6 && alert(49) != null }
        val activity = instrumentation.startActivitySync(Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        fun home(tag: String): android.view.View? {
            var found: android.view.View? = null
            instrumentation.runOnMainSync { found = activity.window.decorView.findViewWithTag(tag) }
            return found
        }
        try {
            instrumentation.waitForIdleSync()
            // Long enough for a cancel from onResume to take effect.
            SystemClock.sleep(500)
            assertNotNull("Opening the app must keep the alert of a live episode", alert(49))
            val status = home("home:status") as android.widget.TextView?
            assertNotNull("State 6 shows the line under the logo", status)
            assertEquals("and it says what the desktop's Home says while it reconnects",
                context.getString(R.string.text_connecting), status!!.text.toString())
            var extras = -1
            instrumentation.runOnMainSync {
                val page = MainActivity::class.java.getDeclaredField("pageRoot").apply { isAccessible = true }.get(activity) as android.view.ViewGroup
                // A link that is a control of its own. The selector's name is a Ui.LinkText too, inside the selector, which is the control
                // (focusable and clicked; the name is neither), so it does not count.
                fun count(view: android.view.View): Int =
                    (if (view.isShown && (view is android.widget.Button || view is Ui.LinkText && (view.isFocusable || view.isClickable))) 1 else 0) +
                    (if (view is android.view.ViewGroup) (0 until view.childCount).sumOf { count(view.getChildAt(it)) } else 0)
                extras = count(page)
            }
            assertEquals("Home has no link or button of its own to reconnect now", 0, extras)
            // The logo disconnects: the tunnel is released and the alert goes with the episode.
            val logo = home("home:toggle")
            assertNotNull(logo)
            instrumentation.runOnMainSync { assertTrue(logo!!.performClick()) }
            eventually { FreeTunnelVpnService.state(context) == 0 }
            eventually { alert(49) == null }
        } finally {
            instrumentation.runOnMainSync { activity.finish() }
        }
    }

    @Test fun fatalNativeDisconnectWithoutKillSwitchReleasesTun() {
        prefs.edit().putBoolean("killswitch", false).commit()
        FreeTunnelVpnService.start(context, source)
        eventually { network() != null && nativeClient() != null }
        inject(nativeClient()!!, 0)
        eventually { network() == null && FreeTunnelVpnService.state(context) == 0 }
    }

    @Test fun failedSynchronousStartRetainsNewTunWhenKillSwitchIsEnabled() {
        FreeTunnelVpnService.start(context, source.replace("192.0.2.1:443", "does-not-exist.invalid:443"))
        eventually { FreeTunnelVpnService.state(context) == 6 }
        assertNotNull("Resolution failure after establish must retain TUN", network())
        FreeTunnelVpnService.stop(context)
        eventually { network() == null }
    }

    // 1.0.0's one list of domains becomes Default's, and the rules an older preview let be saved that the core cannot use are
    // dropped from it, once, with the notice, as they were from the one list.
    @Test fun legacyRulesAreMigratedWithoutBreakingAlwaysOnPreparation() {
        prefs.edit().putBoolean("split_enabled", true)
            .putStringSet("domains", setOf("intranet", "example.com:443", "example.com", "10.0.0.0/8")).commit()
        val plan = RoutingConfig.prepare(context, source, "Default")
        assertEquals(listOf("10.0.0.0/8", "example.com"), profiles().domains("Default"))
        assertEquals(listOf("10.0.0.0/8", "example.com"), exclusions(plan.toml))
        // The old list, which 1.0.0 reads, is the same.
        assertEquals(setOf("example.com", "10.0.0.0/8"), prefs.getStringSet("domains", emptySet()))
        assertTrue(prefs.getBoolean("rules_migrated", false))
        NativeConfigValidator.validate(context, plan.toml)
    }
    @Test fun legacyRulesOfAnotherProfileAreDroppedWhenThatProfileIsUsed() {
        prefs.edit().putBoolean("split_enabled", true).putString(SplitStore.KEY,
            """{"profiles":[{"name":"Default","domains":["home.example"]},{"name":"Work","domains":["bad:1","work.example"]}]}""").commit()
        val plan = RoutingConfig.prepare(context, source, "Default")
        assertEquals(listOf("home.example"), exclusions(plan.toml))
        assertFalse("Default had nothing to drop", prefs.getBoolean("rules_migrated", false))
        assertEquals(listOf("bad:1", "work.example"), profiles().domains("Work"))
        val work = RoutingConfig.prepare(context, source, "Work")
        assertEquals(listOf("work.example"), exclusions(work.toml))
        assertTrue(prefs.getBoolean("rules_migrated", false))
        assertEquals(listOf("work.example"), profiles().domains("Work"))
        NativeConfigValidator.validate(context, work.toml)
    }
    // A new installation is not made by the VPN: with nothing saved, the service is given no recommended list, and builds from what there is.
    @Test fun theServiceNeverMakesANewInstallationsDefaults() {
        val plan = RoutingConfig.prepare(context, source, "Default")
        assertTrue(exclusions(plan.toml).isEmpty())
        assertFalse("split tunnelling is not turned on behind the app's back", prefs.contains("split_enabled"))
        assertTrue(profiles().domains("Default").isEmpty())
    }

    @Test fun validationUsesPrivateIsolatedProcessAndExitsAfterEachRequest() {
        val info = context.packageManager.getServiceInfo(ComponentName(context, ConfigValidationService::class.java), 0)
        assertFalse(info.exported)
        assertTrue(info.flags and android.content.pm.ServiceInfo.FLAG_ISOLATED_PROCESS != 0)
        repeat(3) { NativeConfigValidator.validate(context, source) }
        // validate() waits for Binder death after unbind; this return also proves
        // that successive requests cannot reuse the previous native process.
        assertNull(network())
    }

    @androidx.test.filters.SdkSuppress(minSdkVersion = 29)
    @Test fun cancelledAsyncEditCannotOverwriteADeletedProfilesSuccessor() {
        val profiles = org.json.JSONArray()
        for (name in listOf("Profile A", "Profile B")) profiles.put(org.json.JSONObject().put("name", name).put("toml", source))
        assertTrue(ConfigStore.write(prefs, profiles.toString()))
        val activity = instrumentation.startActivitySync(Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        val executor = MainActivity::class.java.getDeclaredField("validationWorker").apply { isAccessible = true }.get(activity)
            as java.util.concurrent.ExecutorService
        val gate = java.util.concurrent.CountDownLatch(1)
        executor.execute { gate.await() }
        try {
            instrumentation.runOnMainSync {
                MainActivity::class.java.getDeclaredMethod("editConfig", Int::class.javaPrimitiveType).apply { isAccessible = true }.invoke(activity, 0)
            }
            instrumentation.waitForIdleSync()
            // The editor is an overlay of the activity's own window, its buttons found by their tags.
            fun click(key: String) {
                instrumentation.runOnMainSync {
                    val button = activity.window.decorView.findViewWithTag<android.view.View?>(key)
                    assertNotNull("The editor's $key must exist", button)
                    assertTrue(button!!.performClick())
                }
                instrumentation.waitForIdleSync()
            }
            // Save of an editor in which nothing changed writes nothing and closes it (as the desktop's does), so the form is changed first. Save
            // then starts a check, which waits behind the gate; Cancel asks whether to discard what was changed, and Discard closes the editor,
            // and the check with it. The question takes no answer for the first 400 ms.
            instrumentation.runOnMainSync {
                val host = activity.window.decorView.findViewWithTag<android.widget.EditText?>("editor:host")
                assertNotNull("The editor's editor:host must exist", host)
                host!!.setText("other.example.com")
            }
            click("editor:save")
            click("editor:cancel")
            android.os.SystemClock.sleep(500)
            click("confirm:ok")
            instrumentation.runOnMainSync {
                val configs = MainActivity::class.java.getDeclaredField("configs").apply { isAccessible = true }.get(activity) as MutableList<*>
                configs.removeAt(0)
                MainActivity::class.java.getDeclaredMethod("saveConfigs").apply { isAccessible = true }.invoke(activity)
            }
            gate.countDown()
            executor.submit {}.get(30, java.util.concurrent.TimeUnit.SECONDS)
            instrumentation.waitForIdleSync()
            val saved = org.json.JSONArray(ConfigStore.read(prefs))
            assertEquals(1, saved.length())
            assertEquals("Profile B", saved.getJSONObject(0).getString("name"))
        } finally {
            gate.countDown()
            instrumentation.runOnMainSync { activity.finish() }
        }
    }

    private fun withMainActivity(block: (MainActivity) -> Unit) {
        val activity = instrumentation.startActivitySync(Intent(context, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        try { block(activity) } finally { instrumentation.runOnMainSync { activity.finish() } }
    }
    private fun privateMethod(name: String, vararg types: Class<*>?) =
        MainActivity::class.java.getDeclaredMethod(name, *types).apply { isAccessible = true }
    private fun descendants(view: android.view.View): Sequence<android.view.View> = sequence {
        yield(view)
        if (view is android.view.ViewGroup) for (i in 0 until view.childCount) yieldAll(descendants(view.getChildAt(i)))
    }
    // Runs block on the main thread and rethrows what it throws on the test thread. Thrown
    // on the main thread, a failed assertion would crash the instrumentation process and
    // skip every test after this one.
    private fun onMain(block: () -> Unit) {
        var failure: Throwable? = null
        instrumentation.runOnMainSync { try { block() } catch (t: Throwable) { failure = t } }
        failure?.let { throw it }
    }
    // Opens one of MainActivity's overlays on the main thread, hands check the overlay's layer in the activity's own window,
    // and closes it with Back (until the keyboard is away and the overlay with it), which asks nothing of an editor in which nothing was typed.
    private fun inOverlay(activity: MainActivity, name: String, start: () -> Unit, check: (android.view.ViewGroup) -> Unit) {
        onMain { start() }
        instrumentation.waitForIdleSync()
        onMain {
            val layer = activity.window.decorView.findViewWithTag<android.view.ViewGroup?>("overlay:$name")
            assertNotNull("The $name must be an overlay of the activity's window", layer)
            check(layer!!)
        }
        // With the soft keyboard up, which the app picker puts up on a phone (the CI emulator has no hardware keyboard), the first Back only
        // takes the keyboard away, as for any app: Back is sent until the overlay is gone, three times at most.
        repeat(3) {
            var open = false
            onMain { open = activity.window.decorView.findViewWithTag<android.view.View?>("overlay:$name") != null }
            if (!open) return@repeat
            instrumentation.sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_BACK)
            instrumentation.waitForIdleSync()
        }
        onMain { assertNull("Back must close the $name", activity.window.decorView.findViewWithTag<android.view.View?>("overlay:$name")) }
    }

    // The editor holds the password, and the server and the user name with it: a keyboard is asked not to go full screen in landscape (its
    // own copy of the text would be outside FLAG_SECURE), not to copy the text into one if it does, and not to learn from it.
    @androidx.test.filters.SdkSuppress(minSdkVersion = 29)
    @Test fun editorKeepsItsFieldsOutOfTheKeyboardsFullScreenView() {
        withMainActivity { activity ->
            inOverlay(activity, "editor", { privateMethod("editConfig", Int::class.javaPrimitiveType).invoke(activity, -1) }) { content ->
                val fields = descendants(content).filterIsInstance<android.widget.EditText>().toList()
                assertTrue("The editor's fields: ${fields.map { it.tag }}", fields.size >= 9)
                assertTrue("including the password", fields.any { it.tag == "editor:password" })
                for (field in fields) {
                    // What a keyboard is given when the field takes focus.
                    val info = android.view.inputmethod.EditorInfo()
                    assertNotNull(field.onCreateInputConnection(info))
                    assertTrue("${field.tag}: the keyboard must be asked not to go full screen",
                        info.imeOptions and android.view.inputmethod.EditorInfo.IME_FLAG_NO_FULLSCREEN != 0)
                    assertTrue("${field.tag}: the text must not be copied into a full-screen keyboard",
                        info.imeOptions and android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI != 0)
                    assertTrue("${field.tag}: the keyboard must be asked not to learn from it",
                        info.imeOptions and android.view.inputmethod.EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING != 0)
                }
            }
        }
    }

    // The fill a control rests on, when it has one that covers: the colour its own text is read against.
    private fun restFill(view: android.view.View): Int? {
        val states = view.background as? android.graphics.drawable.StateListDrawable ?: return null
        states.setState(intArrayOf())
        val drawable = states.current as? android.graphics.drawable.GradientDrawable ?: return null
        val color = drawable.color?.defaultColor ?: return null
        return if (android.graphics.Color.alpha(color) == 255) color else null
    }

    // Every overlay is drawn in the theme's own colours on its own card, which is the theme's bg in the theme in use: what is in
    // it has to read against that, in both themes. Text takes its colour from the theme's tokens and is at least 4.5:1 against what
    // it is on (3 on the desktop's red button, which is white on #e06a6a in its dark theme); the desktop's textFaint, which is what
    // its placeholders and an application's package are in, is lighter by design, and is held to what its own palette gives:
    // 3.2 dark and 2.3 light on the card, 3.4 and 1.9 on a field.
    @androidx.test.filters.SdkSuppress(minSdkVersion = 29)
    @Test fun overlayTextIsReadableAgainstTheOverlaysOwnBackgroundInBothThemes() {
        val profiles = org.json.JSONArray().put(org.json.JSONObject().put("name", "Profile A").put("toml", source))
        assertTrue(ConfigStore.write(prefs, profiles.toString()))
        withMainActivity { activity ->
            val overlays = listOf("editor" to { privateMethod("editConfig", Int::class.javaPrimitiveType).invoke(activity, -1) },
                "picker" to { privateMethod("appPicker").invoke(activity) },
                "confirm" to { privateMethod("confirmRemove", Int::class.javaPrimitiveType).invoke(activity, 0) })
            for (dark in listOf(false, true)) {
                val theme = Theme.of(dark)
                val which = if (dark) "dark" else "light"
                // The setting, then a render(), which is what makes a theme the one the controls are drawn in.
                onMain {
                    MainActivity::class.java.getDeclaredField("themeMode").apply { isAccessible = true }.set(activity, which)
                    privateMethod("render").invoke(activity)
                }
                val tokens = listOf(theme.text, theme.textDim, theme.textFaint, theme.accent, theme.accentText, theme.danger, theme.onDanger)
                for ((name, start) in overlays) inOverlay(activity, name, { start() }) { layer ->
                    val card = layer.getChildAt(1)
                    val background = (card.background as android.graphics.drawable.GradientDrawable).color!!.defaultColor
                    assertEquals("The $name's own background is the theme's bg, in the $which theme", theme.bg, background)
                    val texts = descendants(layer).filterIsInstance<android.widget.TextView>().filter { it.isShown }.toList()
                    assertFalse("The $name shows no text to check", texts.isEmpty())
                    for (text in texts) {
                        val where = "\"${text.hint ?: text.text}\" in the $name, in the $which theme"
                        val fill = restFill(text)
                        val under = fill ?: background
                        val color = text.currentTextColor
                        assertTrue("$where: ${Integer.toHexString(color)} is no colour of this theme", color in tokens)
                        val floor = when {
                            color == theme.textFaint -> if (dark) 3.2 else 2.3
                            fill != null && color == theme.onDanger -> 3.0
                            else -> 4.5
                        }
                        val contrast = androidx.core.graphics.ColorUtils.calculateContrast(color, under)
                        assertTrue("$where: contrast $contrast", contrast >= floor)
                        if (text.hint != null) {
                            assertEquals("$where: its hint is textFaint", theme.textFaint, text.currentHintTextColor)
                            val hintContrast = androidx.core.graphics.ColorUtils.calculateContrast(text.currentHintTextColor, fill ?: background)
                            assertTrue("$where: hint contrast $hintContrast", hintContrast >= if (dark) 3.4 else 1.9)
                        }
                    }
                }
            }
        }
    }

    // Receive from phone takes several texts to a session. The VPN runs the config "Home"; the first text has that name, so the TV offers Replace, and a
    // second waits behind it. Answering Replace builds the VPN again on the new text, though the second text's check starts the moment the first is
    // answered: a check of a text to add is not what makes that rebuild stale (it was one number for both, and the later check dropped the
    // earlier answer, which left the VPN on the old server while the list showed the new). The second is then asked about, not lost.
    @Test fun replacingTheRunningConfigRebuildsTheVpnWhileTheNextTextIsChecked() = withSink { config ->
        val cm = context.getSystemService(ConnectivityManager::class.java)
        val saved = org.json.JSONArray().put(org.json.JSONObject().put("name", "Home").put("toml", config))
        assertTrue(ConfigStore.write(prefs, saved.toString()))
        FreeTunnelVpnService.start(context, config)
        eventually { network()?.let { cm.getLinkProperties(it)?.mtu == 1350 } == true && nativeClient() != null && FreeTunnelVpnService.state(context) != 0 }
        withMainActivity { activity ->
            fun onScreen(key: String): Boolean {
                var found = false
                onMain { found = activity.window.decorView.findViewWithTag<android.view.View?>(key) != null }
                return found
            }
            fun message(): String? {
                var shown: String? = null
                onMain { shown = activity.window.decorView.findViewWithTag<android.widget.TextView?>("confirm:message")?.text?.toString() }
                return shown
            }
            val queue = MainActivity::class.java.getDeclaredField("receivedTexts").apply { isAccessible = true }.get(activity) as ReceivedQueue
            onMain {
                // The activity read the state when it was made; the receiver that keeps it is told of changes as they come.
                MainActivity::class.java.getDeclaredField("vpnState").apply { isAccessible = true }.setInt(activity, FreeTunnelVpnService.state(context))
                queue.waiting().addAndGet(2)
                queue.add(config.replace("1350", "1300"), "Home", "192.0.2.9")
                queue.add(config.replace("1350", "1250"), "Second", "192.0.2.9")
                privateMethod("processReceived").invoke(activity)
            }
            eventually { message()?.contains(context.getString(R.string.import_exists, "Home")) == true }
            // The red Replace takes no click for its first 400 ms.
            SystemClock.sleep(600)
            onMain { assertTrue(activity.window.decorView.findViewWithTag<android.view.View>("confirm:ok").performClick()) }
            eventually { network()?.let { cm.getLinkProperties(it)?.mtu == 1300 } == true }
            assertTrue("The list shows the replacement", ConfigStore.read(prefs).contains("1300"))
            // And the second text, which was checked while the VPN was being built again, is asked about.
            eventually { message()?.contains(context.getString(R.string.import_question_file)) == true }
            assertTrue(onScreen("confirm:cancel"))
            onMain { activity.window.decorView.findViewWithTag<android.view.View>("confirm:cancel").performClick() }
            eventually { !onScreen("confirm:cancel") }
        }
    }

    @Test fun serviceStartsFromSavedProfileWithoutIntentExtras() {
        assertNull(VpnService.prepare(context))
        val sink = ServerSocket(0, 8, InetAddress.getByName("127.0.0.1"))
        val sockets = ConcurrentLinkedQueue<Socket>()
        val accepting = thread(isDaemon = true) { runCatching { while (!sink.isClosed) sockets.add(sink.accept()) } }
        try {
            val config = source.replace("192.0.2.1:443", "127.0.0.1:${sink.localPort}").replace("1350", "1200")
            val json = org.json.JSONArray().put(org.json.JSONObject().put("name", "Recovery test").put("toml", config)).toString()
            assertTrue(ConfigStore.write(prefs, json))
            // Reproduces the start contract, not Android's process-restart scheduling.
            context.startForegroundService(Intent(context, FreeTunnelVpnService::class.java))
            val cm = context.getSystemService(ConnectivityManager::class.java)
            eventually { network()?.let { cm.getLinkProperties(it)?.mtu == 1200 } == true }
            // The tunnel is up before the core is made and the state says connecting: the one is read, then the other is waited for.
            eventually { FreeTunnelVpnService.state(context) != 0 }
        } finally {
            FreeTunnelVpnService.stop(context)
            sink.close(); accepting.join(500); sockets.forEach { it.close() }
        }
    }
    // The saved profile, with the reconnect marker an Install with the VPN on
    // leaves behind, and an APK in the download folder.
    private fun afterUpdate(address: String, marker: Long): File {
        val config = source.replace("192.0.2.1:443", address).replace("1350", "1200")
        val json = org.json.JSONArray().put(org.json.JSONObject().put("name", "Update test").put("toml", config)).toString()
        assertTrue(ConfigStore.write(prefs, json))
        prefs.edit().putLong(FreeTunnelVpnService.RESUME_AFTER_UPDATE, marker).commit()
        return File(context.cacheDir, "updates").apply { mkdirs() }.resolve("verified-test.apk").apply { writeText("APK") }
    }
    @Test fun resumeAfterUpdateReconnectsSavedProfile() {
        assertNull(VpnService.prepare(context))
        val sink = ServerSocket(0, 8, InetAddress.getByName("127.0.0.1"))
        val sockets = ConcurrentLinkedQueue<Socket>()
        val accepting = thread(isDaemon = true) { runCatching { while (!sink.isClosed) sockets.add(sink.accept()) } }
        try {
            val download = afterUpdate("127.0.0.1:${sink.localPort}", BuildConfig.VERSION_CODE.toLong())
            PackageReplacedReceiver().onReceive(context, Intent(Intent.ACTION_MY_PACKAGE_REPLACED))
            val cm = context.getSystemService(ConnectivityManager::class.java)
            eventually { network()?.let { cm.getLinkProperties(it)?.mtu == 1200 } == true }
            assertFalse(prefs.contains(FreeTunnelVpnService.RESUME_AFTER_UPDATE))
            assertFalse(download.exists())
            assertTrue(download.parentFile!!.listFiles().isNullOrEmpty())
        } finally {
            FreeTunnelVpnService.stop(context)
            sink.close(); accepting.join(500); sockets.forEach { it.close() }
        }
    }
    @Test fun staleResumeMarkerIsDropped() {
        assertNull(VpnService.prepare(context))
        // Names another version: an update that failed, or one installed later.
        // A start would show as a TUN even though nothing answers at this address.
        val download = afterUpdate("192.0.2.1:443", BuildConfig.VERSION_CODE.toLong() + 1)
        PackageReplacedReceiver().onReceive(context, Intent(Intent.ACTION_MY_PACKAGE_REPLACED))
        SystemClock.sleep(2_000)
        assertNull(network())
        assertNull(serviceOwner())
        assertFalse(prefs.contains(FreeTunnelVpnService.RESUME_AFTER_UPDATE))
        assertFalse(download.exists())
    }
    @Test fun disconnectForgetsResumeMarker() {
        FreeTunnelVpnService.start(context, source)
        eventually { network() != null && nativeClient() != null }
        prefs.edit().putLong(FreeTunnelVpnService.RESUME_AFTER_UPDATE, BuildConfig.VERSION_CODE.toLong()).commit()
        FreeTunnelVpnService.stop(context)
        eventually { network() == null && FreeTunnelVpnService.state(context) == 0 }
        assertFalse(prefs.contains(FreeTunnelVpnService.RESUME_AFTER_UPDATE))
    }
    // Sends one start and returns once the service's worker has handled it. Two
    // starts sent together would test nothing: the second supersedes the first
    // before the first reaches its "already running" check.
    private fun handled(send: () -> Unit) {
        val owner = serviceOwner()!!
        val latest = FreeTunnelVpnService::class.java.getDeclaredField("latestStartId").apply { isAccessible = true }
        val before = latest.getInt(owner)
        send()
        eventually { latest.getInt(owner) > before }
        // onStartCommand queues its task just after recording the start.
        instrumentation.waitForIdleSync()
        (FreeTunnelVpnService::class.java.getDeclaredField("worker").apply { isAccessible = true }.get(owner)
            as java.util.concurrent.ExecutorService).submit {}.get(30, java.util.concurrent.TimeUnit.SECONDS)
    }
    // The app, the tile and Android's Always-on run the config the app has selected, and each builds the session from the rules of the
    // split profile that config uses: here "Work", where the other config uses Default.
    private fun activePlanToml(): String {
        val plan = serviceField("activePlan").get(serviceOwner()) as RoutingConfig.Plan
        return plan.toml
    }
    @Test fun theTileAndAlwaysOnRunTheProfileOfTheSelectedConfig() = withSink { config ->
        val saved = org.json.JSONArray()
            .put(org.json.JSONObject().put("name", "Home").put("toml", source))
            .put(org.json.JSONObject().put("name", "Office").put("toml", config).put("profile", "Work"))
        assertTrue(ConfigStore.write(prefs, saved.toString()))
        prefs.edit().putInt("active", 1).putBoolean("split_enabled", true).commit()
        SplitStore.edit(splitStore(), null) { p ->
            p.addDomains("Default", "home-only.example")
            p.add("Work"); p.addDomains("Work", "work-only.example")
        }
        // The tile and the consent screen: the selected config, with its profile.
        val selected = SavedProfile.selected(prefs)
        assertEquals("Work", selected.profile)
        assertEquals(listOf("work-only.example"), exclusions(RoutingConfig.prepare(context, selected.toml, selected.profile).toml))
        FreeTunnelVpnService.start(context, selected.toml, selected.profile)
        eventually { network() != null && nativeClient() != null }
        assertEquals(listOf("work-only.example"), exclusions(activePlanToml()))
        FreeTunnelVpnService.stop(context)
        eventually { network() == null && FreeTunnelVpnService.state(context) == 0 && serviceOwner() == null }
        // Always-on: Android starts the service with nothing in the start but the service's own action.
        context.startForegroundService(Intent(VpnService.SERVICE_INTERFACE).setClass(context, FreeTunnelVpnService::class.java))
        eventually { network() != null && nativeClient() != null }
        assertEquals("Always-on builds with the profile of the selected config", listOf("work-only.example"), exclusions(activePlanToml()))
        FreeTunnelVpnService.stop(context)
        eventually { network() == null && FreeTunnelVpnService.state(context) == 0 && serviceOwner() == null }
        // With the other config selected, the same two starts use Default.
        prefs.edit().putInt("active", 0).commit()
        val home = SavedProfile.selected(prefs)
        assertEquals("Default", home.profile)
        context.startForegroundService(Intent(VpnService.SERVICE_INTERFACE).setClass(context, FreeTunnelVpnService::class.java))
        eventually { network() != null && nativeClient() != null }
        assertEquals(listOf("home-only.example"), exclusions(activePlanToml()))
    }
    // A start that says nothing of a profile, as every start in these tests does, uses the selected config's.
    @Test fun aStartWithoutAProfileUsesTheSelectedConfigs() = withSink { config ->
        val saved = org.json.JSONArray().put(org.json.JSONObject().put("name", "Office").put("toml", config).put("profile", "Work"))
        assertTrue(ConfigStore.write(prefs, saved.toString()))
        prefs.edit().putBoolean("split_enabled", true).commit()
        SplitStore.edit(splitStore(), null) { p -> p.addDomains("Default", "home-only.example"); p.add("Work"); p.addDomains("Work", "work-only.example") }
        FreeTunnelVpnService.start(context, config)
        eventually { network() != null && nativeClient() != null }
        assertEquals(listOf("work-only.example"), exclusions(activePlanToml()))
    }
    @Test fun aProfileThatIsGoneIsDefaultForTheService() = withSink { config ->
        val saved = org.json.JSONArray().put(org.json.JSONObject().put("name", "Office").put("toml", config).put("profile", "Deleted"))
        assertTrue(ConfigStore.write(prefs, saved.toString()))
        prefs.edit().putBoolean("split_enabled", true).commit()
        SplitStore.edit(splitStore(), null) { p -> p.addDomains("Default", "home-only.example") }
        FreeTunnelVpnService.start(context, config)
        eventually { network() != null && nativeClient() != null }
        assertEquals(listOf("home-only.example"), exclusions(activePlanToml()))
    }
    @Test fun resumeAndAlwaysOnStartLeaveRunningClientAlone() {
        val sink = ServerSocket(0, 8, InetAddress.getByName("127.0.0.1"))
        val sockets = ConcurrentLinkedQueue<Socket>()
        val accepting = thread(isDaemon = true) { runCatching { while (!sink.isClosed) sockets.add(sink.accept()) } }
        try {
            FreeTunnelVpnService.start(context, source.replace("192.0.2.1:443", "127.0.0.1:${sink.localPort}"))
            eventually { network() != null && nativeClient() != null }
            val running = nativeClient()
            handled { FreeTunnelVpnService.resume(context) }
            assertSame("The reconnect after an update must not restart a running VPN", running, nativeClient())
            // What Android sends for Always-on: the VpnService action, no extras.
            handled { context.startForegroundService(Intent(VpnService.SERVICE_INTERFACE).setClass(context, FreeTunnelVpnService::class.java)) }
            assertSame("An Always-on start must not restart a running VPN", running, nativeClient())
            assertNotNull(network())
        } finally {
            FreeTunnelVpnService.stop(context)
            sink.close(); accepting.join(500); sockets.forEach { it.close() }
        }
    }
    @Test fun alwaysOnStartStillReconnectsATunKeptBlocked() {
        FreeTunnelVpnService.start(context, source)
        eventually { network() != null && nativeClient() != null }
        nativeClient()!!.onStateChanged(0)
        eventually { FreeTunnelVpnService.state(context) == 6 && nativeClient() == null }
        // The kill switch keeps the TUN but no client, so this start reconnects.
        context.startForegroundService(Intent(VpnService.SERVICE_INTERFACE).setClass(context, FreeTunnelVpnService::class.java))
        eventually { nativeClient() != null }
        assertNotNull(network())
    }
    @Test fun malformedConfigCannotLeaveATunOrConnectedState() {
        FreeTunnelVpnService.start(context, source + "\ninvalid TOML")
        eventually { context.getSystemService(android.app.NotificationManager::class.java).activeNotifications.any { it.id == 48 } }
        eventually { network() == null && FreeTunnelVpnService.state(context) == 0 }
    }
    @Test fun rejectedNativeConfigCannotLeaveATunOrConnectedState() {
        FreeTunnelVpnService.start(context, source.replace("has_ipv6 = false", "has_ipv6 = false\nclient_random = \"abcd/\""))
        eventually { context.getSystemService(android.app.NotificationManager::class.java).activeNotifications.any { it.id == 48 } }
        eventually { network() == null && FreeTunnelVpnService.state(context) == 0 }
    }

    // Each connection the core reports is a line of the log at INFO, as the desktop logs its connection info: one line, whatever the core
    // sends, written in the format the Logs page reads; and with logging off nothing is written.
    @Test fun theCoresConnectionInfoIsLoggedAtInfoAndOnlyWithLoggingOn() = withSink { config ->
        prefs.edit().putBoolean("logging", true).commit()
        log.delete()
        FreeTunnelVpnService.start(context, config)
        eventually { network() != null && nativeClient() != null }
        awaitWorker()
        val client = nativeClient()!!
        val info = client.javaClass.getDeclaredMethod("onConnectionInfo", String::class.java).apply { isAccessible = true }
        try {
            info.invoke(client, "tunnel example.test")
            eventually { logged("tunnel example.test") == 1 }
            val line = log.readLines().first { "tunnel example.test" in it }
            assertTrue(line, Regex("\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\tINFO\ttunnel example\\.test").matches(line))
            // A message of several lines is one line of the log, and cannot pass for a second entry.
            info.invoke(client, "bypass a.test\nERROR\tforged")
            eventually { logged("bypass a.test") == 1 }
            assertEquals(1, logged("bypass a.test ERROR forged"))
            prefs.edit().putBoolean("logging", false).commit()
            info.invoke(client, "tunnel off.test")
            SystemClock.sleep(300)
            assertEquals("nothing is written with logging off", 0, logged("off.test"))
        } finally {
            FreeTunnelVpnService.stop(context)
            eventually { FreeTunnelVpnService.state(context) == 0 }
            log.delete()
        }
    }
}
