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
    private fun eventually(timeoutMs: Long = 40_000, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        while (SystemClock.elapsedRealtime() < deadline) {
            if (condition()) return
            SystemClock.sleep(50)
        }
        assertTrue("Condition did not become true", condition())
    }
    @Before fun setup() {
        context.getSystemService(android.app.NotificationManager::class.java).run { cancel(48); cancel(49) }
        oldPreferences = prefs.all
        prefs.edit().clear().putBoolean("logging", false).commit()
    }
    @After fun cleanup() {
        if (serviceOwner() != null) FreeTunnelVpnService.stop(context)
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
    @Test fun staleAppRulesFallBackToFullVpnWithWarning() {
        prefs.edit().putBoolean("split_enabled", true).putBoolean("through", true)
            .putStringSet("apps", setOf("not.installed.package", context.packageName)).commit()
        val plan = RoutingConfig.prepare(context, source)
        assertTrue(plan.apps.isEmpty()); assertFalse(plan.allowApps); assertNotNull(plan.warning)
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
    private fun useBackoff(backoff: ReconnectBackoff) = FreeTunnelVpnService::class.java.getDeclaredField("backoff")
        .apply { isAccessible = true }.set(serviceOwner(), backoff)
    // Runs block against a local server that accepts and never answers, so the
    // real core hangs in its TLS handshake instead of failing at once.
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

    @Test fun fatalNativeDisconnectRetainsGuardAndExplicitStopReleasesIt() {
        FreeTunnelVpnService.start(context, source)
        eventually { network() != null && nativeClient() != null }
        val active = nativeClient()!!
        // Same callback boundary used by the JNI core on a fatal error.
        active.onStateChanged(0)
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
        // Longer than the first retry delay: no attempt may outlive a stop.
        SystemClock.sleep(1_500)
        assertNull(network())
        assertNull(serviceOwner())
    }

    @Test fun fatalDisconnectReconnectsAutomaticallyOnTheKeptTun() = withSink { config ->
        prefs.edit().putBoolean("logging", true).commit()
        val log = java.io.File(context.filesDir, "freetunnel.log").apply { delete() }
        FreeTunnelVpnService.start(context, config)
        eventually { network() != null && tunName() != null && nativeClient() != null }
        val tun = network()
        val name = tunName()
        val first = nativeClient()!!
        val (states, receiver) = recordStates()
        try {
            val gaps = tunGaps {
                first.onStateChanged(0)
                eventually { FreeTunnelVpnService.state(context) == 6 && alert(49) != null }
                val blocked = alert(49)!!.notification
                assertEquals("freetunnel_vpn_failure", blocked.channelId)
                assertEquals(2, blocked.actions.size)
                val reason = context.getString(R.string.text_connection_to_the_server_was_lost)
                eventually { log.exists() && log.readLines().takeLast(5).any { reason in it } }
                // The first delay is about 1 s.
                eventually(10_000) { nativeClient().let { it != null && it !== first } }
                val second = nativeClient()!!
                assertEquals(tun, network())
                assertEquals("The attempt must reuse the kept TUN, not establish one", name, tunName())
                // A late broadcast of the first Connecting may come before the 6.
                val blockedStates = states.toList().dropWhile { it != 6 }
                assertTrue("The blocked state must hold through the attempt: $states",
                    blockedStates.isNotEmpty() && blockedStates.all { it == 6 })
                second.onStateChanged(2)
                eventually { FreeTunnelVpnService.state(context) == 2 && alert(49) == null }
            }
            assertEquals("The TUN must stay up through the attempt", 0, gaps)
            assertEquals(name, tunName())
            assertFalse(states.contains(0))
        } finally {
            context.unregisterReceiver(receiver)
            log.delete()
        }
    }

    @Test fun networkAppearingRetriesWithoutWaitingOutTheBackoff() = withSink { config ->
        FreeTunnelVpnService.start(context, config)
        eventually { network() != null && tunName() != null && nativeClient() != null }
        val tun = network()
        val name = tunName()
        useBackoff(ReconnectBackoff(600_000, 600_000))
        nativeClient()!!.onStateChanged(0)
        eventually { FreeTunnelVpnService.state(context) == 6 && nativeClient() == null }
        SystemClock.sleep(2_000)
        assertNull("The next attempt waits out the long backoff", nativeClient())
        val cm = context.getSystemService(ConnectivityManager::class.java)
        val underlying = cm.allNetworks.firstOrNull { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) == false }
        assertNotNull("The emulator needs a network besides the VPN", underlying)
        val availability = FreeTunnelVpnService::class.java.getDeclaredField("availability").apply { isAccessible = true }
            .get(serviceOwner()) as ConnectivityManager.NetworkCallback
        availability.onAvailable(underlying!!)
        eventually(10_000) { nativeClient() != null }
        assertEquals(6, FreeTunnelVpnService.state(context))
        assertEquals(tun, network())
        assertEquals(name, tunName())
    }

    @Test fun blockedHomeScreenOffersReconnect() = withSink { config ->
        FreeTunnelVpnService.start(context, config)
        eventually { network() != null && tunName() != null && nativeClient() != null }
        // Only the button may start the next connection here.
        useBackoff(ReconnectBackoff(600_000, 600_000))
        nativeClient()!!.onStateChanged(0)
        eventually { FreeTunnelVpnService.state(context) == 6 }
        val name = tunName()
        val activity = instrumentation.startActivitySync(Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        fun reconnectButton(): android.widget.Button? {
            var found: android.widget.Button? = null
            instrumentation.runOnMainSync { found = activity.window.decorView.findViewWithTag("home:reconnect") }
            return found
        }
        val (states, receiver) = recordStates()
        try {
            instrumentation.waitForIdleSync()
            val button = reconnectButton()
            assertNotNull("State 6 must offer Reconnect on the home screen", button)
            assertEquals(context.getString(R.string.vpn_reconnect), button!!.text.toString())
            val gaps = tunGaps {
                instrumentation.runOnMainSync { assertTrue(button.performClick()) }
                // An explicit Reconnect establishes a new TUN. Android hands the
                // same VPN network over to it, so the interface is what changes.
                eventually { tunName().let { it != null && it != name } }
                eventually { reconnectButton() == null }
            }
            assertEquals("The explicit Reconnect must hand over without a gap", 0, gaps)
            assertNotEquals(0, FreeTunnelVpnService.state(context))
            assertFalse("Reconnect must never publish disconnected", states.contains(0))
        } finally {
            context.unregisterReceiver(receiver)
            instrumentation.runOnMainSync { activity.finish() }
        }
    }

    @Test fun fatalNativeDisconnectWithoutKillSwitchReleasesTun() {
        prefs.edit().putBoolean("killswitch", false).commit()
        FreeTunnelVpnService.start(context, source)
        eventually { network() != null && nativeClient() != null }
        nativeClient()!!.onStateChanged(0)
        eventually { network() == null && FreeTunnelVpnService.state(context) == 0 }
    }

    @Test fun failedSynchronousStartRetainsNewTunWhenKillSwitchIsEnabled() {
        FreeTunnelVpnService.start(context, source.replace("192.0.2.1:443", "does-not-exist.invalid:443"))
        eventually { FreeTunnelVpnService.state(context) == 6 }
        assertNotNull("Resolution failure after establish must retain TUN", network())
        FreeTunnelVpnService.stop(context)
        eventually { network() == null }
    }

    @Test fun legacyRulesAreMigratedWithoutBreakingAlwaysOnPreparation() {
        prefs.edit().putBoolean("split_enabled", true)
            .putStringSet("domains", setOf("intranet", "example.com:443", "example.com", "10.0.0.0/8")).commit()
        val plan = RoutingConfig.prepare(context, source)
        assertEquals(setOf("example.com", "10.0.0.0/8"), prefs.getStringSet("domains", emptySet()))
        assertTrue(prefs.getBoolean("rules_migrated", false))
        NativeConfigValidator.validate(context, plan.toml)
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
            fun click(buttonId: Int) {
                instrumentation.runOnMainSync {
                    val button = android.view.inspector.WindowInspector.getGlobalWindowViews()
                        .mapNotNull { it.findViewById<android.widget.Button>(buttonId) }.lastOrNull()
                    assertNotNull("Dialog action must exist", button)
                    assertTrue(button!!.performClick())
                }
                instrumentation.waitForIdleSync()
            }
            click(android.R.id.button1)
            click(android.R.id.button2)
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
    // Opens one of MainActivity's dialogs on the main thread, hands check the dialog's
    // window and the view MainActivity put in it, and closes the dialog with Cancel.
    private fun inDialog(start: () -> Unit, check: (android.view.View, android.view.ViewGroup) -> Unit) {
        onMain { start() }
        instrumentation.waitForIdleSync()
        onMain {
            val window = android.view.inspector.WindowInspector.getGlobalWindowViews()
                .last { it.findViewById<android.view.View>(android.R.id.custom) != null }
            try { check(window, window.findViewById<android.view.ViewGroup>(android.R.id.custom)) }
            finally { window.findViewById<android.widget.Button>(android.R.id.button2)?.performClick() }
        }
        instrumentation.waitForIdleSync()
    }

    @androidx.test.filters.SdkSuppress(minSdkVersion = 29)
    @Test fun editorKeepsTheTomlOutOfTheKeyboardsFullScreenView() {
        withMainActivity { activity ->
            inDialog({ privateMethod("editConfig", Int::class.javaPrimitiveType).invoke(activity, -1) }) { _, content ->
                val toml = descendants(content).filterIsInstance<android.widget.EditText>()
                    .single { it.inputType and android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE != 0 }
                // What a keyboard is given when the field takes focus.
                val info = android.view.inputmethod.EditorInfo()
                assertNotNull(toml.onCreateInputConnection(info))
                assertTrue("The keyboard must be asked not to go full screen",
                    info.imeOptions and android.view.inputmethod.EditorInfo.IME_FLAG_NO_FULLSCREEN != 0)
                assertTrue("The TOML must not be copied into a full-screen keyboard",
                    info.imeOptions and android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI != 0)
                assertTrue("The keyboard must be asked not to learn from the TOML",
                    info.imeOptions and android.view.inputmethod.EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING != 0)
            }
        }
    }

    // The light theme colours only the activity. AlertDialog.Builder(this) keeps
    // Theme.Material's dark dialog in both themes, so the text, hints and check boxes
    // MainActivity puts in a dialog have to be light in both.
    @androidx.test.filters.SdkSuppress(minSdkVersion = 29)
    @Test fun dialogTextIsReadableInBothThemes() {
        withMainActivity { activity ->
            val dialogs = listOf<() -> Unit>(
                { privateMethod("editConfig", Int::class.javaPrimitiveType).invoke(activity, -1) },
                { privateMethod("appPicker").invoke(activity) })
            for (dark in listOf(false, true)) {
                onMain {
                    MainActivity::class.java.getDeclaredField("dark").apply { isAccessible = true }.setBoolean(activity, dark)
                }
                for (start in dialogs) inDialog(start) { window, content ->
                    val attributes = window.context.obtainStyledAttributes(intArrayOf(android.R.attr.colorBackground))
                    val background = try { attributes.getColor(0, android.graphics.Color.TRANSPARENT) } finally { attributes.recycle() }
                    assertTrue("Dialogs are expected to stay dark in both themes",
                        androidx.core.graphics.ColorUtils.calculateLuminance(background) < 0.2)
                    val texts = descendants(content).filterIsInstance<android.widget.TextView>().filter { it.isShown }.toList()
                    assertFalse("The dialog shows no text to check", texts.isEmpty())
                    for (text in texts) {
                        val where = "\"${text.hint ?: text.text}\" in the ${if (dark) "dark" else "light"} theme"
                        val contrast = androidx.core.graphics.ColorUtils.calculateContrast(text.currentTextColor, background)
                        assertTrue("$where: contrast $contrast", contrast >= 4.5)
                        // Hints are drawn lighter on purpose, and an empty editor shows only
                        // its hints. 3:1 is the minimum for them and for the check boxes.
                        if (text.hint != null) {
                            val hintContrast = androidx.core.graphics.ColorUtils.calculateContrast(text.currentHintTextColor, background)
                            assertTrue("$where: hint contrast $hintContrast", hintContrast >= 3.0)
                        }
                        val tint = (text as? android.widget.CompoundButton)?.buttonTintList ?: continue
                        val boxContrast = androidx.core.graphics.ColorUtils.calculateContrast(tint.defaultColor, background)
                        assertTrue("$where: check box contrast $boxContrast", boxContrast >= 3.0)
                    }
                }
            }
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
            assertNotEquals(0, FreeTunnelVpnService.state(context))
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
}
