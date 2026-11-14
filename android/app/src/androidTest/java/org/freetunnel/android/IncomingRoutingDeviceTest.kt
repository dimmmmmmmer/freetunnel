package org.freetunnel.android

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File

/** The external host probes a distinct application's UID over emulator ingress. */
class IncomingRoutingDeviceTest {
    @Test fun holdProductionTunnelForExternalIncomingProbe() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("incoming_probe") == "true")
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val prefs = context.getSharedPreferences("freetunnel", Context.MODE_PRIVATE)
        val previous = prefs.all
        fun shell(command: String): String = instrumentation.uiAutomation.executeShellCommand(command).let {
            android.os.ParcelFileDescriptor.AutoCloseInputStream(it).bufferedReader().use { reader -> reader.readText() }
        }
        val certificate = File("/data/local/tmp/incoming-endpoint.pem").readText()
        try {
            // The emulator forwards incoming host connections with source 10.0.2.2.
            // Deliberately include that peer to test replies, not the default LAN exclusion.
            prefs.edit().clear().putBoolean("logging", true).putBoolean("split_enabled", false)
                .putString(ExcludedRoutes.KEY, "[]").commit()
            if (InstrumentationRegistry.getArguments().getString("bypass_probe_app") == "true") {
                prefs.edit().putBoolean("split_enabled", true).putBoolean("through", false).commit()
                SplitStore.edit(SplitPrefs(prefs), null) { it.addApp(SplitProfiles.DEFAULT, "org.freetunnel.incomingprobe") }
            }
            val source = """
                vpn_mode = "general"
                killswitch_enabled = true
                exclusions = []
                [endpoint]
                hostname = "vpn.invalid"
                addresses = ["127.0.0.1:29443"]
                username = "test"
                password = "probe-only"
                skip_verification = false
                certificate = CERTIFICATE_PEM
                upstream_protocol = "http2"
                has_ipv6 = false
                [listener.tun]
                included_routes = ["10.0.2.2/32"]
                excluded_routes = []
                mtu_size = 1350
            """.trimIndent().replace("CERTIFICATE_PEM", "\"\"\"\n${certificate.trim()}\n\"\"\"")
            FreeTunnelVpnService.start(context, source)
            val cm = context.getSystemService(ConnectivityManager::class.java)
            val deadline = SystemClock.elapsedRealtime() + 40_000
            var ready = false
            while (SystemClock.elapsedRealtime() < deadline) {
                ready = FreeTunnelVpnService.state(context) == 2 && cm.allNetworks.any { network ->
                    cm.getNetworkCapabilities(network)?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) == true &&
                        cm.getLinkProperties(network)?.routes?.any { it.destination.toString() == "10.0.2.2/32" } == true
                }
                if (ready) break
                SystemClock.sleep(100)
            }
            assertTrue("Production VPN did not connect to the authenticated endpoint", ready)
            shell("touch /data/local/tmp/incoming-vpn-ready")
            val stopDeadline = SystemClock.elapsedRealtime() + 120_000
            var stopped = false
            while (SystemClock.elapsedRealtime() < stopDeadline) {
                // executeShellCommand executes argv, not shell operators such as &&.
                stopped = shell("cat /data/local/tmp/incoming-vpn-stop").trim() == "stopped"
                if (stopped) break
                SystemClock.sleep(100)
            }
            assertTrue("External host did not finish the probe", stopped)
        } finally {
            FreeTunnelVpnService.stop(context)
            val cm = context.getSystemService(ConnectivityManager::class.java)
            val deadline = SystemClock.elapsedRealtime() + 35_000
            while (SystemClock.elapsedRealtime() < deadline && cm.allNetworks.any {
                cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) == true
            }) SystemClock.sleep(100)
            val editor = prefs.edit().clear()
            previous.forEach { (key, value) -> when (value) {
                is Boolean -> editor.putBoolean(key, value)
                is String -> editor.putString(key, value)
                is Int -> editor.putInt(key, value)
                is Long -> editor.putLong(key, value)
                is Float -> editor.putFloat(key, value)
                is Set<*> -> { @Suppress("UNCHECKED_CAST") editor.putStringSet(key, value as Set<String>) }
            } }
            editor.commit()
        }
    }
}
