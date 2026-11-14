package org.freetunnel.android

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.ConcurrentLinkedQueue
import kotlin.concurrent.thread

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
        val sink = ServerSocket(0, 8, InetAddress.getByName("127.0.0.1"))
        val sockets = ConcurrentLinkedQueue<Socket>()
        val accepting = thread(isDaemon = true) { runCatching { while (!sink.isClosed) sockets.add(sink.accept()) } }
        try {
            // The emulator forwards incoming host connections with source 10.0.2.2.
            // Deliberately include that peer to test replies, not the default LAN exclusion.
            prefs.edit().clear().putBoolean("logging", true).putBoolean("split_enabled", false)
                .putString(ExcludedRoutes.KEY, "[]").commit()
            val source = """
                vpn_mode = "general"
                killswitch_enabled = true
                exclusions = []
                [endpoint]
                hostname = "vpn.invalid"
                addresses = ["127.0.0.1:${sink.localPort}"]
                username = "test"
                password = "test"
                upstream_protocol = "http2"
                has_ipv6 = false
                [listener.tun]
                included_routes = ["10.0.2.2/32"]
                excluded_routes = []
                mtu_size = 1350
            """.trimIndent()
            FreeTunnelVpnService.start(context, source)
            val cm = context.getSystemService(ConnectivityManager::class.java)
            val deadline = SystemClock.elapsedRealtime() + 30_000
            var ready = false
            while (SystemClock.elapsedRealtime() < deadline) {
                ready = cm.allNetworks.any { network ->
                    cm.getNetworkCapabilities(network)?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) == true &&
                        cm.getLinkProperties(network)?.routes?.any { it.destination.toString() == "10.0.2.2/32" } == true
                }
                if (ready) break
                SystemClock.sleep(100)
            }
            assertTrue("Production VPN route was not installed", ready)
            shell("touch /data/local/tmp/incoming-vpn-ready")
            val stopDeadline = SystemClock.elapsedRealtime() + 120_000
            var stopped = false
            while (SystemClock.elapsedRealtime() < stopDeadline) {
                stopped = shell("test -f /data/local/tmp/incoming-vpn-stop && echo stopped").trim() == "stopped"
                if (stopped) break
                SystemClock.sleep(100)
            }
            assertTrue("External host did not finish the probe", stopped)
        } finally {
            FreeTunnelVpnService.stop(context)
            sink.close(); accepting.join(500); sockets.forEach { runCatching { it.close() } }
            SystemClock.sleep(1000)
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
