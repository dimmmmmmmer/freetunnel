package org.freetunnel.android

import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.view.ViewGroup
import android.widget.TextView
import androidx.test.platform.app.InstrumentationRegistry
import org.json.JSONArray
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import java.nio.charset.StandardCharsets
import java.util.Date
import java.util.concurrent.atomic.AtomicInteger

// A pinned subscription fetch end to end on Android's own TLS stack (Conscrypt), which the JVM's PinnedFetchTest cannot
// reach: the app's real fetch (SubscriptionData.fetch, the real DIRECT dialer, PinnedHttps' SSLSocket and PinTrust) against a
// TLS server in this process (TestTls, the same file the JVM tests use) on 127.0.0.1. The name in the address, vpn.test,
// resolves to nothing, so a fetch that works went to the config's address. The last two tests go through MainActivity's
// refresh of a saved subscription, as the user's Update does. The JVM's rows are in PinnedFetchTest; this holds Conscrypt,
// the platform's X.509 parsing and its socket classes to the same ones. Phone and TV legs run all of it: nothing here
// is a touch or a key.
class PinnedSubscriptionDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private val prefs get() = context.getSharedPreferences("freetunnel", Context.MODE_PRIVATE)
    private var oldPreferences: Map<String, *> = emptyMap<String, Any>()
    private var activity: MainActivity? = null

    private val name = "vpn.test"

    @Before fun setup() {
        oldPreferences = prefs.all
        prefs.edit().clear().putBoolean("logging", false).commit()
    }
    @After fun cleanup() {
        activity?.let { opened -> instrumentation.runOnMainSync { opened.finish() } }
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

    // The subscription the server answers with: what the desktop's and the app's own fetchers read. The username says
    // which answer it is.
    private fun subscription(host: String, port: Int, username: String, certificate: String? = null): String =
        JSONObject().put("version", 1).put("hostname", host).put("address", "127.0.0.1:$port")
            .put("username", username).put("password", "secret").put("has_ipv6", false)
            .also { if (certificate != null) it.put("certificate", certificate) }.toString()

    private fun trust(host: String, server: TestTls.Server, pem: String) =
        SubscriptionData.Trust(host, "127.0.0.1:${server.port()}", pem)
    private fun url(host: String, server: TestTls.Server) = "https://$host:${server.port()}/sub"

    private fun fetch(url: String, trust: SubscriptionData.Trust): ByteArray {
        val request = SubscriptionData.requestFor(url, trust)
        assertTrue("A config with a certificate pins", request.pinned)
        return SubscriptionData.fetch(request)
    }
    private fun failure(url: String, trust: SubscriptionData.Trust): SubscriptionData.FetchException.Failure {
        try { fetch(url, trust) } catch (e: SubscriptionData.FetchException) { return e.failure }
        throw AssertionError("The fetch should have been refused")
    }
    private fun text(bytes: ByteArray) = String(bytes, StandardCharsets.UTF_8)

    // ---- SubscriptionData.fetch on the device ----

    @Test fun aPinnedSelfSignedLeafIsFetchedNamingTheServerOnConscrypt() {
        val cert = TestTls.leaf(name)
        TestTls.Server.of(cert) { _, _ -> TestTls.ok(subscription(name, 1, "fetched")) }.use { server ->
            val body = fetch(url(name, server), trust(name, server, cert.pem()))
            // The body is the server's, and the app reads it as it would the provider's.
            val document = SubscriptionData.apply(body, null)
            assertEquals(name, ConfigForm.read(document.toml).hostname)
            assertEquals("fetched", ConfigForm.read(document.toml).username)
            assertEquals(1, server.requests.size)
            val seen = server.requests[0]
            assertEquals("The SNI is the config's name, not the address connected to", name, seen.sni)
            assertEquals("GET /sub HTTP/1.1", seen.requestLine)
            assertEquals(name + ":" + server.port(), seen.headers["host"])
        }
    }

    @Test fun aLeafSignedByAPinnedCaAloneIsUntrustedAndGetsNoRequest() {
        val ca = TestTls.make(TestTls.spec().cn("Test CA").ca())
        val leaf = TestTls.make(TestTls.spec().cn(name).dns(name).signedBy(ca))
        // Served with its CA and without: only a pinned leaf is trusted, whatever vouches for it.
        for (chain in listOf(listOf(leaf.certificate), listOf(leaf.certificate, ca.certificate))) {
            TestTls.Server(leaf.keys.private, chain) { _, _ -> TestTls.ok(subscription(name, 1, "never")) }.use { server ->
                assertEquals(SubscriptionData.FetchException.Failure.UNTRUSTED, failure(url(name, server), trust(name, server, ca.pem())))
                assertEquals("Not a byte of a request goes to a server that is not trusted", 0, server.requests.size)
            }
        }
    }

    @Test fun aLeafIsTrustedWhenItAndItsCaAreBothPinned() {
        val ca = TestTls.make(TestTls.spec().cn("Test CA").ca())
        val leaf = TestTls.make(TestTls.spec().cn(name).dns(name).signedBy(ca))
        TestTls.Server(leaf.keys.private, listOf(leaf.certificate, ca.certificate)) { _, _ -> TestTls.ok(subscription(name, 1, "both")) }.use { server ->
            val body = fetch(url(name, server), trust(name, server, leaf.pem() + ca.pem()))
            assertEquals("both", ConfigForm.read(SubscriptionData.apply(body, null).toml).username)
            assertEquals(1, server.requests.size)
        }
    }

    @Test fun aPinnedLeafForAnotherNameIsUntrusted() {
        val other = TestTls.leaf("other.test")
        TestTls.Server.of(other) { _, _ -> TestTls.ok(subscription(name, 1, "never")) }.use { server ->
            assertEquals(SubscriptionData.FetchException.Failure.UNTRUSTED, failure(url(name, server), trust(name, server, other.pem())))
            assertEquals(0, server.requests.size)
        }
    }

    @Test fun anExpiredPinnedLeafIsUntrusted() {
        val now = System.currentTimeMillis()
        val day = 24L * 3600 * 1000
        val expired = TestTls.make(TestTls.spec().cn(name).dns(name).valid(Date(now - 30 * day), Date(now - day)))
        TestTls.Server.of(expired) { _, _ -> TestTls.ok(subscription(name, 1, "never")) }.use { server ->
            assertEquals(SubscriptionData.FetchException.Failure.UNTRUSTED, failure(url(name, server), trust(name, server, expired.pem())))
            assertEquals(0, server.requests.size)
        }
    }

    @Test fun aCertificateFieldThatCannotBeReadIsBadPinWithoutDialling() {
        val cert = TestTls.leaf(name)
        TestTls.Server.of(cert) { _, _ -> TestTls.ok(subscription(name, 1, "never")) }.use { server ->
            // The shape of a PEM block, with bytes that are no certificate: the config has a certificate, so the system's CAs
            // are never the way out of it.
            val request = SubscriptionData.requestFor(url(name, server), trust(name, server, TestTls.pem(ByteArray(3))))
            assertTrue(request.pinned)
            assertTrue(request.pins.isEmpty())
            assertEquals(SubscriptionData.FetchException.Failure.BAD_PIN, failure(url(name, server), trust(name, server, TestTls.pem(ByteArray(3)))))
            assertEquals("Nothing was dialled", 0, server.accepted.get())
        }
    }

    @Test fun anIpLiteralIsVerifiedByItsAddressAndSendsNoSni() {
        val ip = "127.0.0.1"
        val cert = TestTls.make(TestTls.spec().cn(ip).ip(ip))
        TestTls.Server.of(cert) { _, _ -> TestTls.ok(subscription(ip, 1, "by-address")) }.use { server ->
            val body = fetch(url(ip, server), trust(ip, server, cert.pem()))
            assertEquals(subscription(ip, 1, "by-address"), text(body))
            assertEquals(1, server.requests.size)
            assertNull("An address is no server name: no SNI is sent", server.requests[0].sni)
        }
    }

    // ---- The same, through the app: Update on a saved subscription ----

    private val configClass = Class.forName("org.freetunnel.android.MainActivity\$Config")
    private fun field(name: String) = MainActivity::class.java.getDeclaredField(name).apply { isAccessible = true }
    private fun method(name: String, vararg types: Class<*>?) =
        MainActivity::class.java.getDeclaredMethod(name, *types).apply { isAccessible = true }
    private fun <T> onMain(block: () -> T): T {
        var value: T? = null
        var failure: Throwable? = null
        instrumentation.runOnMainSync { try { value = block() } catch (t: Throwable) { failure = t } }
        failure?.let { throw it }
        @Suppress("UNCHECKED_CAST")
        return value as T
    }
    private fun eventually(what: String, timeoutMs: Long = 20_000, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        while (SystemClock.elapsedRealtime() < deadline) {
            if (condition()) return
            SystemClock.sleep(50)
        }
        assertTrue(what, condition())
    }
    private fun toastTexts(a: MainActivity): List<String> = onMain {
        val layer = field("toastLayer").get(a) as ViewGroup
        (0 until layer.childCount).map { (layer.getChildAt(it) as TextView).text.toString() }
    }
    private fun config(port: Int, pem: String) = """
        vpn_mode = "general"
        killswitch_enabled = true
        exclusions = []
        [endpoint]
        hostname = "$name"
        addresses = ["127.0.0.1:$port"]
        username = "saved"
        password = "test"
        upstream_protocol = "http2"
        has_ipv6 = false
        certificate = $pem
        [listener.tun]
        included_routes = ["0.0.0.0/0", "::/0"]
        excluded_routes = []
        mtu_size = 1350
    """.trimIndent()

    // One saved config, whose subscription is the server's own, as the app keeps it. It was updated just now, so the app's
    // own hourly refresh (the ticker) has nothing to do and the only fetch is the one a test asks for.
    private fun open(toml: String, subscriptionUrl: String): MainActivity {
        val profiles = JSONArray().put(JSONObject().put("name", "Pinned").put("toml", toml)
            .put("subscription", subscriptionUrl).put("subscription_updated", System.currentTimeMillis()))
        assertTrue(ConfigStore.write(prefs, profiles.toString()))
        val opened = instrumentation.startActivitySync(Intent(context, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        activity = opened
        instrumentation.waitForIdleSync()
        return opened
    }
    // What the user's Update does: refreshSubscription(config, manual = true), on the main thread as a tap would.
    private fun update(a: MainActivity) = onMain {
        val config = (field("configs").get(a) as List<*>)[0]
        method("refreshSubscription", configClass, Boolean::class.javaPrimitiveType).invoke(a, config, true)
    }
    private fun savedToml(): String = JSONArray(ConfigStore.read(prefs)).getJSONObject(0).getString("toml")

    @Test fun updatingASubscriptionPinnedToItsLeafReplacesTheConfigAndSaysSo() {
        val cert = TestTls.leaf(name)
        // The answer names the server's own port, which is known only once it is listening.
        val port = AtomicInteger()
        TestTls.Server.of(cert) { _, _ -> TestTls.ok(subscription(name, port.get(), "refreshed", cert.pem())) }.use { server ->
            port.set(server.port())
            val a = open(config(server.port(), "\"\"\"\n" + cert.pem() + "\"\"\""), url(name, server))
            assertEquals("saved", ConfigForm.read(savedToml()).username)
            update(a)
            eventually("The app says the subscription was updated") { context.getString(R.string.subscription_updated) in toastTexts(a) }
            assertEquals("One fetch, to the config's address, naming the server", 1, server.requests.size)
            assertEquals(name, server.requests[0].sni)
            // What was saved is the provider's answer, and the pin stays.
            val saved = ConfigForm.read(savedToml())
            assertEquals("refreshed", saved.username)
            assertEquals(name, saved.hostname)
            assertEquals(cert.pem().trim(), saved.certificate.trim())
        }
    }

    @Test fun updatingASubscriptionWhoseServerIsOnlyVouchedForByAPinnedCaSaysTheCertificateIsNotTrusted() {
        val ca = TestTls.make(TestTls.spec().cn("Test CA").ca())
        val leaf = TestTls.make(TestTls.spec().cn(name).dns(name).signedBy(ca))
        TestTls.Server(leaf.keys.private, listOf(leaf.certificate, ca.certificate)) { _, _ -> TestTls.ok(subscription(name, 1, "never", ca.pem())) }.use { server ->
            val toml = config(server.port(), "\"\"\"\n" + ca.pem() + "\"\"\"")
            val a = open(toml, url(name, server))
            update(a)
            eventually("The app says the certificate is not trusted") { context.getString(R.string.subscription_untrusted) in toastTexts(a) }
            assertEquals("The server is sent nothing", 0, server.requests.size)
            assertEquals("and the config is as it was", "saved", ConfigForm.read(savedToml()).username)
        }
    }
}
