package org.freetunnel.android

import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.VpnService
import android.os.Build
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inspector.WindowInspector
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeFalse
import org.junit.Before
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.File
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.URI
import java.net.URLEncoder
import java.util.Locale
import java.util.concurrent.ConcurrentLinkedQueue
import kotlin.concurrent.thread

// The UI tour: every screen of the app, as it is drawn, saved as a picture for review, in the dark theme, then the light one, and
// in Russian; and the pages the TV serves to a phone, saved as they are, in both languages. It is how the app's look is held
// against the desktop's: the parity is a thing to look at, which no assertion can say.
//
// A pass seeds three configs, sets theme_mode and recreates the activity, then drives the screens with a remote's keys: the focus is
// put on a control and OK pressed on it. On the phone leg two more passes (touch dark, touch light) go through it by taps, with
// no focus to show, since a phone in a hand has none. Each screen is a picture of the whole display (UiAutomation), except while the
// window is FLAG_SECURE, which the editor makes it and which a screenshot shows as black: that is drawn from the activity's own
// view into a Bitmap instead, and the app's protection is left alone.
//
// Where the files go: the instrumentation argument additionalTestOutputDir, which Android Gradle Plugin 8.9.1 copies to
// app/build/outputs/connected_android_test_additional_output, and android.yml uploads from there. If the app's process may not
// write there (scoped storage), the files are written as the shell user instead, through "dd", into the same directory. Without the
// argument (a run from adb), they go to /data/local/tmp/uitour, to be pulled with adb. index.txt of each pass says what each file
// is, and which state could not be reached, which is logged and never faked.
//
// On every screen it also asserts what the parity is about, and collects what it finds, so that one finding does not cost the
// pictures after it: the activity's own window is the only one shown (no system dialog, and no other app's), no view has a
// foreground (a ring), and the control that has the focus is no wider than what it holds plus its padding, unless it is a row of a
// list, a field or a box of text, or a small cell, or the Home logo (a control of 132 across by design, with no tile behind it).
class UiTourDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private val prefs get() = context.getSharedPreferences("freetunnel", Context.MODE_PRIVATE)
    private val isTv get() =
        (context.resources.configuration.uiMode and Configuration.UI_MODE_TYPE_MASK) == Configuration.UI_MODE_TYPE_TELEVISION
    private val leg get() = if (isTv) "tv" else "phone"

    private var oldPreferences: Map<String, *> = emptyMap<String, Any>()
    private var activity: MainActivity? = null
    private var touchModeBefore: Boolean? = null
    private val listeners = ArrayList<ServerSocket>()

    // The pass under way: its key-driven or touch-driven mode, its folder, its numbering, and what it found.
    private var remote = true
    private var counter = 0
    private var output: Output? = null
    private val log = ArrayList<String>()
    private val shots = ArrayList<String>()
    private val violations = ArrayList<String>()
    private val failures = ArrayList<String>()

    private val source = """
        vpn_mode = "general"
        killswitch_enabled = true
        exclusions = []
        [endpoint]
        hostname = "vpn.invalid"
        addresses = ["192.0.2.1:443"]
        username = "test"
        password = "test"
        upstream_protocol = "http2"
        has_ipv6 = false
        [listener.tun]
        included_routes = ["0.0.0.0/0", "::/0"]
        excluded_routes = []
        mtu_size = 1350
    """.trimIndent()
    private val configNames = listOf("Germany · Frankfurt", "Netherlands · Amsterdam", "United States · New York")
    private val link = "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQz"

    // ---- The setup: three configs that can be pinged, rules to show, and everything put back afterwards ----

    private fun listener(): ServerSocket = ServerSocket(0, 8, InetAddress.getByName("127.0.0.1")).also { listeners.add(it) }

    // The first and the third config answer a probe, with a time; the second is a port nothing listens on, which the probe shows as a dash.
    private fun seedConfigs() {
        val ports = listOf(listener().localPort, 1, listener().localPort)
        val hosts = listOf("frankfurt.example.com", "amsterdam.example.com", "newyork.example.com")
        val list = org.json.JSONArray()
        for (i in 0 until 3) {
            val toml = source.replace("vpn.invalid", hosts[i]).replace("192.0.2.1:443", "127.0.0.1:${ports[i]}")
            list.put(org.json.JSONObject().put("name", configNames[i]).put("toml", toml))
        }
        assertTrue(ConfigStore.write(prefs, list.toString()))
    }

    @Before fun setup() {
        oldPreferences = prefs.all
        prefs.edit().clear().putBoolean("logging", true).putBoolean("split_enabled", true)
            .putStringSet("domains", setOf("example.com", "*.example.org", "192.168.50.0/24", "video.example.net")).commit()
        // Another installed application this one can see, for the application's chip.
        val pm = context.packageManager
        val other = listOf(Intent.CATEGORY_LAUNCHER, Intent.CATEGORY_LEANBACK_LAUNCHER)
            .flatMap { pm.queryIntentActivities(Intent(Intent.ACTION_MAIN).addCategory(it), 0) }
            .map { it.activityInfo.packageName }.firstOrNull { it != context.packageName }
        if (other != null) prefs.edit().putStringSet("apps", setOf(other)).commit()
        seedConfigs()
        counter = 0; log.clear(); shots.clear(); violations.clear(); failures.clear()
    }

    @After fun cleanup() {
        activity?.let { opened ->
            // While this app's window has the focus, which Android requires to change it.
            touchModeBefore?.let { instrumentation.setInTouchMode(it) }
            instrumentation.runOnMainSync { if (!opened.isDestroyed) opened.finish() }
        }
        // The one the test holds is the one it opened. A recreation makes another, which a test that stopped before it saw it never held,
        // and which would stay up, with its timers, in front of every test after it: none outlives its test.
        instrumentation.runOnMainSync {
            for (stage in listOf(Stage.CREATED, Stage.STARTED, Stage.RESUMED, Stage.PAUSED, Stage.STOPPED, Stage.RESTARTED))
                ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(stage).filterIsInstance<MainActivity>()
                    .forEach { if (!it.isDestroyed && !it.isFinishing) it.finish() }
        }
        for (open in listeners) runCatching { open.close() }
        listeners.clear()
        serviceField("currentState").setInt(null, 0)
        updater("officialBuild", null); updater("certificatePin", BuildConfig.RELEASE_CERTIFICATE_SHA256)
        updater("state", AndroidUpdater.State.IDLE)
        updater("release", null); updater("failure", null); updater("failedCheck", false); updater("committed", false)
        updater("progress", 0)
        // The language is kept by Android for the app from 13 on: back to the system's, or the next test is in Russian.
        prefs.edit().remove("language").commit()
        if (Build.VERSION.SDK_INT >= 33) {
            context.getSystemService(android.app.LocaleManager::class.java).setApplicationLocales(android.os.LocaleList.getEmptyLocaleList())
            val deadline = SystemClock.elapsedRealtime() + 10_000
            while (context.resources.configuration.locales.get(0).language == "ru" && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(100)
        }
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

    // ---- The passes ----

    @Test fun tourDark() = tour("dark", "dark", true, null)
    @Test fun tourLight() = tour("light", "light", true, null)
    @Test fun tourRussianDark() = tour("dark-ru", "dark", true, "ru")
    @Test fun tourTouchDark() { assumeFalse("A TV has no touch", isTv); tour("touch-dark", "dark", false, null) }
    @Test fun tourTouchLight() { assumeFalse("A TV has no touch", isTv); tour("touch-light", "light", false, null) }

    // ---- Plumbing: reflection into the activity, the main thread, waiting ----

    private fun field(name: String) = MainActivity::class.java.getDeclaredField(name).apply { isAccessible = true }
    private fun method(name: String, vararg types: Class<*>?) =
        MainActivity::class.java.getDeclaredMethod(name, *types).apply { isAccessible = true }
    private fun updater(name: String, value: Any?) =
        AndroidUpdater::class.java.getDeclaredField(name).apply { isAccessible = true }.set(null, value)
    private fun serviceField(name: String) = FreeTunnelVpnService::class.java.getDeclaredField(name).apply { isAccessible = true }

    private fun <T> onMain(block: () -> T): T {
        var value: T? = null
        var failure: Throwable? = null
        instrumentation.runOnMainSync { try { value = block() } catch (t: Throwable) { failure = t } }
        failure?.let { throw it }
        @Suppress("UNCHECKED_CAST")
        return value as T
    }
    private fun eventually(what: String, timeoutMs: Long = 10_000, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        while (SystemClock.elapsedRealtime() < deadline) {
            if (condition()) return
            SystemClock.sleep(50)
        }
        assertTrue(what, condition())
    }
    // Animations are 130 to 280 ms; the pulse never ends, so the main thread's queue is what is waited for, and a moment more.
    private fun settle(ms: Long = 450) {
        instrumentation.waitForIdleSync()
        SystemClock.sleep(ms)
        instrumentation.waitForIdleSync()
    }

    private val SYSTEM_PANEL_MS = 5_000L

    private fun act(): MainActivity = activity!!
    private fun root(): ViewGroup = field("host").get(act()) as ViewGroup
    private fun descendants(view: View): Sequence<View> = sequence {
        yield(view)
        if (view is ViewGroup) for (i in 0 until view.childCount) yieldAll(descendants(view.getChildAt(i)))
    }
    private fun has(key: String): Boolean = onMain { root().findViewWithTag<View?>(key) != null }
    private fun overlayUp(name: String): Boolean = has("overlay:$name")
    private fun focusedKey(): String? = onMain { act().currentFocus?.tag as? String }

    private fun inTouchMode(): Boolean = onMain { act().window.decorView.isInTouchMode }
    // The lifecycle monitor is asked on the main thread, and nowhere else: from another it throws "Querying activity state off main thread".
    private fun resumed(): MainActivity? = onMain {
        ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).filterIsInstance<MainActivity>().firstOrNull()
    }

    // A remote's first key press leaves touch mode, as a TV starts out; a phone starts in it.
    private fun leaveTouchMode() {
        if (touchModeBefore == null) touchModeBefore = inTouchMode()
        instrumentation.setInTouchMode(false)
        val deadline = SystemClock.elapsedRealtime() + 2_000
        while (inTouchMode() && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(50)
        // The first arrow press leaves touch mode as well, and is used up by that.
        if (inTouchMode()) instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_DOWN)
        eventually("The window must leave touch mode") { !inTouchMode() }
        settle(150)
    }
    private fun enterTouchMode() {
        if (touchModeBefore == null) touchModeBefore = inTouchMode()
        instrumentation.setInTouchMode(true)
        eventually("The window must be in touch mode") { inTouchMode() }
        settle(150)
    }
    private fun ensureMode() { if (remote) leaveTouchMode() else enterTouchMode() }

    // The activity made again: the theme and the language are read when it is created. Waits for the new one to be on the screen.
    // A change of language makes it by itself (Android 13 and up, through the app's locales; before, AppLocale.choose does), so that
    // one is not recreated a second time here.
    private fun recreated(recreate: Boolean = true, change: () -> Unit) {
        val before = act()
        change()
        if (recreate) instrumentation.runOnMainSync { before.recreate() }
        awaitNew(before)
    }
    private fun awaitNew(before: MainActivity) {
        eventually("The activity must be made again", 30_000) { val now = resumed(); now != null && now !== before }
        activity = resumed()
        settle()
        ensureMode()
    }

    // ---- Driving the screens: a key at a time, as a remote does, or a tap ----

    private fun focusOn(key: String) {
        onMain {
            val target = root().findViewWithTag<View?>(key) ?: throw AssertionError("No control has the key $key")
            if (!target.requestFocus()) throw AssertionError("$key must take the focus")
        }
        instrumentation.waitForIdleSync()
    }
    private fun tap(key: String) {
        onMain {
            val target = root().findViewWithTag<View?>(key) ?: throw AssertionError("No control has the key $key")
            if (!target.performClick()) throw AssertionError("$key must be clickable")
        }
    }
    // OK on a control with a remote, a tap with a finger.
    private fun activate(key: String) {
        if (remote) {
            focusOn(key)
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_CENTER)
        } else {
            tap(key)
        }
        settle()
    }
    private fun press(keyCode: Int) { instrumentation.sendKeyDownUpSync(keyCode); settle() }
    // Back until it has done what it is for. With the keyboard up, the first Back is the keyboard's own and puts it away.
    private fun backUntil(what: String, done: () -> Boolean) {
        repeat(3) { if (!done()) press(KeyEvent.KEYCODE_BACK) }
        eventually("Back must have $what") { done() }
    }
    private fun goTo(page: Int) {
        activate("tab:$page")
        eventually("The page must be $page") { onMain { field("page").getInt(act()) } == page }
        settle(200)
    }
    private fun awaitOverlay(name: String) {
        eventually("The $name must be up") { overlayUp(name) }
        settle()
    }
    // Closes what is up, the way Back does without asking anything: the tour does not stop at a question.
    private fun closeOverlays() {
        repeat(8) {
            val top = onMain { (field("overlays").get(act()) as List<*>).lastOrNull() } ?: return
            onMain {
                val close = MainActivity::class.java.getDeclaredMethod("closeOverlay", top.javaClass).apply { isAccessible = true }
                close.invoke(act(), top)
            }
            settle(200)
        }
    }
    private fun hideToast() { onMain { method("hideToastNow").invoke(act()) } }
    private fun render() { onMain { method("render").invoke(act()) }; settle(200) }
    private fun scrollPageToEnd() {
        onMain { (field("pageScroll").get(act()) as ScrollView?)?.let { it.scrollTo(0, it.getChildAt(0).height) } }
        settle(200)
    }
    private fun scrollPageTo(key: String) {
        onMain {
            val scroll = field("pageScroll").get(act()) as ScrollView
            val target = root().findViewWithTag<View>(key)
            val at = IntArray(2); target.getLocationInWindow(at)
            val scrollAt = IntArray(2); scroll.getLocationInWindow(scrollAt)
            scroll.scrollBy(0, at[1] - scrollAt[1] - scroll.height / 3)
        }
        settle(200)
    }
    // As typed: the caret is after the text.
    private fun typeInto(key: String, text: String) {
        onMain { (root().findViewWithTag<View>(key) as EditText).apply { setText(text); setSelection(text.length) } }
        settle(200)
    }

    // ---- What is written, and where ----

    // The folder of a pass, <additionalTestOutputDir>/uitour/<name>. Written to directly where the app may, else as the shell user,
    // whose files AGP's pull and adb both see. Every file is also written to /data/local/tmp/uitour/<name>, which nothing deletes
    // when the apps are uninstalled and adb can pull, so that the pictures survive a Gradle that copies nothing (android.yml pulls
    // them if it finds nothing copied). A write that fails there is said once, in index.txt, and is no failure.
    private inner class Output(val name: String) {
        private val dir = "${base()}/uitour/$name"
        private val local = File(dir)
        private val direct: Boolean
        private var mirror = Build.VERSION.SDK_INT >= 31 && !dir.startsWith("/data/local/tmp/")
        private val mirrorDir = "/data/local/tmp/uitour/$name"
        var mirrorProblem: String? = null
        init {
            direct = try {
                local.mkdirs()
                File(local, ".probe").also { it.writeText("x") }.delete()
                true
            } catch (t: Throwable) { false }
            if (!direct) {
                runShell("mkdir -p $dir")
            }
            if (mirror) runShell("mkdir -p $mirrorDir")
        }
        fun write(file: String, bytes: ByteArray): Unit {
            if (direct) File(local, file).writeBytes(bytes) else writeAsShell("$dir/$file", bytes)
            if (mirror) {
                try { writeAsShell("$mirrorDir/$file", bytes) }
                catch (t: Throwable) { mirror = false; mirrorProblem = "${t.javaClass.simpleName}: ${t.message}" }
            }
        }
        fun write(file: String, text: String): Unit { write(file, text.toByteArray(Charsets.UTF_8)) }
        override fun toString() = dir + (if (direct) "" else " (written as the shell user)") + (if (mirror) ", and $mirrorDir" else "")
    }
    private fun runShell(command: String): String =
        ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command)).bufferedReader().use { it.readText() }
    // "dd" takes what is piped to it: the command is not run by a shell, so there is no redirection to write. UiAutomation.executeShellCommandRw
    // is Android 12's (API 31): on 11 it would be a NoSuchMethodError and not this message.
    private fun writeAsShell(path: String, bytes: ByteArray) {
        assertTrue("Writing as the shell user needs Android 12", Build.VERSION.SDK_INT >= 31)
        val pipes = instrumentation.uiAutomation.executeShellCommandRw("dd of=$path")
        ParcelFileDescriptor.AutoCloseOutputStream(pipes[1]).use { it.write(bytes) }
        ParcelFileDescriptor.AutoCloseInputStream(pipes[0]).use { it.readBytes() }
    }
    private fun base(): String =
        InstrumentationRegistry.getArguments().getString("additionalTestOutputDir")?.trimEnd('/') ?: "/data/local/tmp"

    private fun note(line: String) {
        log.add(line)
        output?.write("index.txt", log.joinToString("\n") + "\n")
    }
    private fun unreachable(name: String, reason: String) = note("UNREACHABLE  $name: $reason")

    // ---- A picture of the screen, and what must be true of it ----

    private fun drawn(): Bitmap = onMain {
        val decor = act().window.decorView
        val bitmap = Bitmap.createBitmap(decor.width, decor.height, Bitmap.Config.ARGB_8888)
        decor.draw(Canvas(bitmap))
        bitmap
    }
    private fun png(bitmap: Bitmap): ByteArray =
        ByteArrayOutputStream().also { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }.toByteArray()

    // Android TV's own panel says "VPN is connected" or "VPN is disconnected" over the top right of the screen, for a few seconds after the VPN comes
    // up or goes down: it is the system's, the app cannot suppress it, and it is over the app's screens in the pictures taken then. So on a TV a picture
    // taken after the VPN changed waits until the panel has gone, and the pictures show the app alone.
    private var vpnSeen: String? = null
    private var vpnChangedAt = 0L
    private fun waitForTheSystemPanel() {
        if (!isTv) return
        val now = SystemClock.elapsedRealtime()
        val seen = "${FreeTunnelVpnService.state(context)}/${vpnNetwork() != null}"
        if (seen != vpnSeen) { vpnSeen = seen; vpnChangedAt = now }
        val remaining = SYSTEM_PANEL_MS - (now - vpnChangedAt)
        if (remaining > 0) SystemClock.sleep(remaining)
    }

    private fun shot(name: String, what: String) {
        waitForTheSystemPanel()
        settle()
        assertScreen(name)
        val secure = onMain { act().window.attributes.flags and WindowManager.LayoutParams.FLAG_SECURE != 0 }
        var how = ""
        var bitmap: Bitmap? = null
        if (secure) {
            bitmap = drawn()
            how = " (FLAG_SECURE: drawn from the activity's view, so without the system's bars)"
        } else {
            bitmap = instrumentation.uiAutomation.takeScreenshot()
            if (bitmap == null) {
                bitmap = drawn()
                how = " (the screenshot failed: drawn from the activity's view)"
            }
        }
        assertBarsDim(name, bitmap!!)
        counter++
        val file = String.format(Locale.ROOT, "%02d-%s.png", counter, name)
        output!!.write(file, png(bitmap!!))
        shots.add(file)
        note("$file  $what$how")
    }

    // The strips under the status and navigation bars are the page's colour as it is drawn beside them: the page's own while nothing is
    // open, and its dim under a card or a question, once or twice, as the desktop dims the whole of its window. From Android 15 on, with
    // the app targeting it, the bars are drawn over the window and the backdrops did not reach under them: every dimmed picture of the
    // phone had a bright band across the top and a band of another grey at the foot (BarScrim paints them now). The pixel is at the left
    // edge, where the page has nothing drawn over its colour; the foot is the keyboard's, and not the app's, while a keyboard is up.
    private fun assertBarsDim(name: String, picture: Bitmap) {
        if (Build.VERSION.SDK_INT < 35) return
        val state = onMain {
            val given = act().window.decorView.rootWindowInsets
            Triple(field("barDims").getInt(act()), given?.getInsets(android.view.WindowInsets.Type.systemBars()),
                given?.isVisible(android.view.WindowInsets.Type.ime()) == true)
        }
        val (backdrops, bars, keyboard) = state
        if (bars == null) return
        fun differs(x: Int, y: Int) = Math.abs(Color.red(x) - Color.red(y)) > 3 || Math.abs(Color.green(x) - Color.green(y)) > 3 ||
            Math.abs(Color.blue(x) - Color.blue(y)) > 3
        val shown = if (backdrops == 0) "not dimmed" else "dimmed $backdrops time(s)"
        if (bars.top > 0 && bars.top + 3 < picture.height) {
            val strip = picture.getPixel(2, bars.top / 2)
            val page = picture.getPixel(2, bars.top + 3)
            if (differs(strip, page)) violate(name, "the strip under the status bar is #${Integer.toHexString(strip)}, and the page beside it, $shown, #${Integer.toHexString(page)}")
        }
        if (bars.bottom > 0 && !keyboard && bars.bottom + 3 < picture.height) {
            val strip = picture.getPixel(2, picture.height - 1 - bars.bottom / 2)
            val page = picture.getPixel(2, picture.height - bars.bottom - 4)
            if (differs(strip, page)) violate(name, "the strip under the navigation bar is #${Integer.toHexString(strip)}, and the page beside it, $shown, #${Integer.toHexString(page)}")
        }
    }

    private fun violate(name: String, problem: String) {
        violations.add("$name: $problem")
        note("VIOLATION  $name: $problem")
    }

    private fun assertScreen(name: String) {
        // Only the activity's own window: no dialog or popup of the process besides it, and nothing of another app in front.
        if (Build.VERSION.SDK_INT >= 29) {
            var others = emptyList<String>()
            val deadline = SystemClock.elapsedRealtime() + 1_500
            while (SystemClock.elapsedRealtime() < deadline) {
                others = onMain {
                    val decor = act().window.decorView
                    WindowInspector.getGlobalWindowViews().filter { it !== decor && it.isShown }.map { it.javaClass.name + " " + it.tag }
                }
                if (others.isEmpty()) break
                SystemClock.sleep(100)
            }
            if (others.isNotEmpty()) violate(name, "another window is shown: $others")
        }
        val front = instrumentation.uiAutomation.rootInActiveWindow?.packageName?.toString()
        if (front != null && front != context.packageName) violate(name, "the active window is $front's, not the app's")
        // No view has a foreground: a ring drawn over a control is not the desktop's look.
        val covered = onMain {
            descendants(root()).filter { it.foreground != null }.map { it.javaClass.simpleName + " \"" + it.tag + "\"" }.toList()
        }
        if (covered.isNotEmpty()) violate(name, "views with a foreground: $covered")
        // The control with the focus holds its words, not the width of the screen.
        val wide = onMain { tooWide() }
        if (wide != null) violate(name, wide)
        // A confirmation is a card that holds its question and its buttons. It once collapsed to a few pixels wide, the question cut to
        // single characters and the buttons 2 px across, on every screen that asked one, and no check of the tour said so.
        val shrunk = onMain {
            val layer = root().findViewWithTag<View?>("overlay:confirm")
            if (layer == null || !layer.isShown) null else {
                val density = act().resources.displayMetrics.density
                listOf("confirm:message", "confirm:cancel", "confirm:ok").map { it to layer.findViewWithTag<View?>(it) }
                    .firstOrNull { (_, view) -> view == null || !view.isShown || view.width < 60 * density || view.height < 14 * density }
                    ?.let { (key, view) -> "the confirmation's $key is ${view?.width}x${view?.height}: the card has collapsed" }
            }
        }
        if (shrunk != null) violate(name, shrunk)
    }

    // What a view needs to show what it holds: its words and picture and padding, or what its children need.
    private fun needed(view: View): Int {
        if (view is TextView) {
            val layout = view.layout
            var words = 0f
            if (layout != null && layout.lineCount > 0) {
                for (i in 0 until layout.lineCount) words = Math.max(words, layout.getLineWidth(i))
            } else {
                words = view.paint.measureText(view.text.toString())
            }
            return Math.ceil(words.toDouble()).toInt() + view.compoundPaddingLeft + view.compoundPaddingRight
        }
        if (view is ViewGroup) {
            var right = 0
            for (i in 0 until view.childCount) {
                val child = view.getChildAt(i)
                if (child.visibility == View.GONE) continue
                val margins = child.layoutParams as? ViewGroup.MarginLayoutParams
                right = Math.max(right, child.left + needed(child) + (margins?.rightMargin ?: 0))
            }
            return right + view.paddingRight
        }
        return view.width
    }
    // The rows of a list, fields and boxes of text are as wide as their place by design, as the desktop's are; the Home logo is 132 across.
    private val rowKeys = Regex("(config:\\d+:select|home:pick:\\d+|select:\\d+|add:[a-z]+|picker:\\d+|log:box|home:toggle|editor:cert|editor:name|editor:profile|editor:protocol|editor:certfile)")
    private fun tooWide(): String? {
        val focused = act().currentFocus ?: return null
        val key = focused.tag as? String ?: return null
        if (rowKeys.matches(key) || focused is EditText) return null
        val density = act().resources.displayMetrics.density
        // A small cell cannot span the screen: a tab, the header's + and ping, Home's +, a ChipX, a toggle (the row round which fills).
        if (focused.width <= 64 * density) return null
        if (focused is TextView && focused.lineCount > 1) return null
        var minimum = focused.minimumWidth
        if (focused is TextView) minimum = Math.max(minimum, focused.minWidth)
        val allowed = Math.max(needed(focused), minimum) + Math.ceil(2.0 * density).toInt()
        return if (focused.width > allowed) "the focused \"$key\" is ${focused.width} wide and needs ${needed(focused)} (at least $minimum)" else null
    }

    // ---- One screen of the tour: it fails alone, and the tour goes on ----

    private fun screen(name: String, block: () -> Unit) {
        try {
            closeOverlays()
            hideToast()
            block()
        } catch (t: Throwable) {
            val line = t.stackTrace.firstOrNull { it.className.contains("UiTourDeviceTest") }?.lineNumber
            failures.add("$name: ${t.javaClass.simpleName}: ${t.message} (line $line)")
            note("FAILED  $name: ${t.javaClass.simpleName}: ${t.message} (line $line)")
        }
    }

    // ---- The pass ----

    private fun tour(pass: String, themeMode: String, byKeys: Boolean, language: String?) {
        remote = byKeys
        output = Output("$leg-$pass")
        val driven = if (byKeys) "keys" else "taps"
        note("UI tour: pass $pass on the $leg leg, theme $themeMode, language ${language ?: "English"}, driven by $driven")
        note("Files: ${output}")
        note("Android ${Build.VERSION.SDK_INT}, ${Build.MODEL}")
        official()
        activity = instrumentation.startActivitySync(Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        settle()
        // Sets theme_mode and recreates, which is how the theme reaches an activity that is up.
        recreated { prefs.edit().putString("theme_mode", themeMode).commit() }
        if (language != null) recreated(false) { onMain { AppLocale.choose(act(), prefs, language) } }
        ensureMode()
        val dark = onMain { field("dark").getBoolean(act()) }
        val speaking = onMain { AppLocale.current(act()) }
        note("Theme in use: " + (if (dark) "dark" else "light") + "; language in use: " + speaking)
        // The pictures are of the pass they are filed under, or the tour says it is not.
        if (dark != (themeMode == "dark")) {
            failures.add("this pass is $themeMode, and the app drew the other theme")
            note("FAILED  this pass is $themeMode, and the app drew the other theme")
        }
        val wanted = language ?: "en"
        if (speaking != wanted) {
            failures.add("this pass is in $wanted, and the app spoke $speaking")
            note("FAILED  this pass is in $wanted, and the app spoke $speaking")
        }

        tourHome()
        tourConfigs()
        tourEditor()
        tourImport()
        tourSplit()
        tourPicker()
        tourSettings()
        tourLogs()
        if (isTv) tourReceive() else unreachable("receive-from-phone", "the phone leg: Receive from phone is a TV's")
        tourEmpty()

        note("${shots.size} pictures, ${violations.size} findings, ${failures.size} screens that failed")
        output!!.mirrorProblem?.let { note("The copy in /data/local/tmp/uitour stopped: $it") }
        output!!.write("index.html", contactSheet())
        assertTrue("The tour found ${violations.size} things and ${failures.size} screens failed:\n" +
            (violations + failures).joinToString("\n"), violations.isEmpty() && failures.isEmpty())
    }

    // The build the user gets has the Maintenance row; a debug build would show the notice instead.
    // And it makes no check of its own on launch (it made one just now, as far as the 12 hours go): the tour puts the states there itself.
    private fun official() {
        updater("officialBuild", true)
        prefs.edit().putLong(AndroidUpdater.LAST_LAUNCH_CHECK, System.currentTimeMillis()).commit()
    }

    private fun contactSheet(): String {
        val cells = shots.joinToString("") { "<figure><a href=\"$it\"><img src=\"$it\" loading=\"lazy\"></a><figcaption>$it</figcaption></figure>" }
        return "<!doctype html><meta charset=\"utf-8\"><title>UI tour</title>" +
            "<style>body{font:14px sans-serif;margin:12px}figure{display:inline-block;margin:6px;vertical-align:top}" +
            "img{width:260px;border:1px solid #888}figcaption{max-width:260px;word-wrap:break-word}</style>$cells"
    }

    // ---- Home ----

    private fun tourHome() {
        screen("home-disconnected") { goTo(0); shot("home-disconnected", "Home with no session: the logo at half opacity, no line under it, the selector, +, the speed tiles") }
        if (remote) {
            screen("home-selector-focused") { focusOn("home:config"); shot("home-selector-focused", "Home, the selector has the remote's focus") }
            screen("home-logo-focused") { focusOn("home:toggle"); shot("home-logo-focused", "Home, the logo has the remote's focus: held at 1.05, with no tile behind it") }
            screen("nav-focused-tile") { goTo(0); focusOn("tab:2"); shot("nav-focused-tile", "The nav bar, Split's tile focused, Home's the current page") }
            screen("nav-focused-current-tile") { focusOn("tab:0"); shot("nav-focused-current-tile", "The nav bar, the current page's tile focused") }
        }
        screen("home-config-dropdown") {
            goTo(0)
            activate("home:config")
            awaitOverlay("homepicker")
            shot("home-config-dropdown", "Home's config dropdown under the selector, the selected config in accent")
        }
        screen("home-toast") {
            goTo(0)
            onMain { method("showToast", CharSequence::class.java).invoke(act(), act().getString(R.string.config_added, configNames[0])) }
            shot("home-toast", "A toast on Home, between the selector and the speed tiles")
        }
        tourVpnStates()
    }

    // The service really connecting, connected and blocked, through a local server that accepts and never answers, and the core's
    // callback told what the core would tell (as VpnDeviceTest does). A state that cannot be reached here is logged and not drawn.
    private fun tourVpnStates() {
        val all = listOf("home-connecting", "home-connected", "home-disconnecting", "home-config-dropdown-connected",
            "configs-list-connected", "home-blocked")
        if (VpnService.prepare(context) != null) {
            for (name in all) unreachable(name, "the VPN consent is not granted (adb shell appops set org.freetunnel.android ACTIVATE_VPN allow)")
            return
        }
        val done = HashSet<String>()
        val sink = ServerSocket(0, 8, InetAddress.getByName("127.0.0.1"))
        val sockets = ConcurrentLinkedQueue<Socket>()
        val accepting = thread(isDaemon = true) { runCatching { while (!sink.isClosed) sockets.add(sink.accept()) } }
        try {
            closeOverlays()
            hideToast()
            goTo(0)
            FreeTunnelVpnService.start(context, source.replace("192.0.2.1:443", "127.0.0.1:${sink.localPort}"))
            eventually("The tunnel and the core must come up", 40_000) { vpnNetwork() != null && nativeClient() != null }
            awaitWorker()
            eventually("The service must be connecting", 20_000) { FreeTunnelVpnService.state(context) == 1 }
            shot("home-connecting", "Home while connecting: the logo pulses at 0.7, \"Connecting…\" under it"); done.add("home-connecting")
            val client = nativeClient()!!
            client.onStateChanged(2)
            awaitWorker()
            eventually("The service must be connected", 20_000) { FreeTunnelVpnService.state(context) == 2 }
            SystemClock.sleep(1_300)
            shot("home-connected", "Home connected: the logo at full opacity, the time under it"); done.add("home-connected")

            // Disconnecting is the time between the user's Disconnect and the service saying it is off: too short to be caught, so the
            // flag Disconnect sets is set on the connected session, which is what the screen is in that time.
            onMain { field("stopping").setBoolean(act(), true) }
            render()
            shot("home-disconnecting", "Home disconnecting (the app's stopping flag set on a connected session, as Disconnect sets it)"); done.add("home-disconnecting")
            onMain { field("stopping").setBoolean(act(), false) }
            render()

            activate("home:config")
            awaitOverlay("homepicker")
            shot("home-config-dropdown-connected", "Home's dropdown while connected: picking one reconnects"); done.add("home-config-dropdown-connected")
            closeOverlays()

            goTo(1)
            shot("configs-list-connected", "Configs while connected: the selected config's badge says connected"); done.add("configs-list-connected")

            // Blocked: the core says it is off, with the kill switch on, and the next try is far off, so that it stays blocked for the picture.
            // The desktop's Home says Connecting… then (its backend is connecting while its tunnel reconnects), with the logo pulsing.
            serviceField("backoff").set(vpnOwner(), ReconnectBackoff(600_000, 600_000))
            val drawn = onMain { field("renderGeneration").getInt(act()) }
            client.onStateChanged(0)
            eventually("Traffic must be blocked", 20_000) { FreeTunnelVpnService.state(context) == 6 }
            // The app draws Configs again for the new state, from its receiver, and puts the focus back on the tab that had it when that is
            // drawn. A key sent before then goes to that tab and not to Home's, whichever was focused for it meanwhile (the other states
            // above are waited out with a sleep; this one is waited for).
            eventually("The page must be drawn again for the new state") {
                onMain { field("renderGeneration").getInt(act()) > drawn && !field("restorePending").getBoolean(act()) }
            }
            settle()
            goTo(0)
            eventually("Home must say Connecting…") {
                onMain { (root().findViewWithTag<View?>("home:status") as? android.widget.TextView)?.text?.toString() == context.getString(R.string.text_connecting) }
            }
            if (remote) focusOn("home:toggle")
            shot("home-blocked", "Home with traffic blocked (state 6): Connecting…, as the desktop's says while it reconnects, and the logo with the first focus"); done.add("home-blocked")
        } catch (t: Throwable) {
            // The consent was granted, or this is not reached, so these screens can be taken here: one that is lost is the app's fault (a Home that
            // does not show the blocked state, a service that does not connect), and fails the tour with the others, as a screen of the pages does. Only the missing
            // consent, above, is a screen that cannot be reached.
            val line = t.stackTrace.firstOrNull { it.className.contains("UiTourDeviceTest") }?.lineNumber
            for (name in all) if (name !in done) {
                unreachable(name, "${t.javaClass.simpleName}: ${t.message}")
                failures.add("$name: ${t.javaClass.simpleName}: ${t.message} (line $line)")
            }
        } finally {
            runCatching { FreeTunnelVpnService.stop(context) }
            try {
                eventually("The tunnel must be gone", 40_000) { vpnNetwork() == null && FreeTunnelVpnService.state(context) == 0 && vpnOwner() == null }
            } catch (e: AssertionError) {
                failures.add("the VPN did not stop after its screens: ${e.message}")
                note("FAILED  the VPN did not stop after its screens")
            }
            runCatching { sink.close(); accepting.join(500); sockets.forEach { it.close() } }
        }
    }
    private fun vpnNetwork() = context.getSystemService(ConnectivityManager::class.java).let { cm ->
        cm.allNetworks.firstOrNull { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_VPN) == true }
    }
    private fun vpnOwner(): Any? = serviceField("owner").get(null)
    private fun nativeClient(): com.adguard.trusttunnel.VpnClient? {
        val owner = vpnOwner() ?: return null
        return serviceField("client").get(owner) as? com.adguard.trusttunnel.VpnClient
    }
    private fun awaitWorker() {
        (serviceField("worker").get(vpnOwner()) as java.util.concurrent.ExecutorService).submit {}.get(30, java.util.concurrent.TimeUnit.SECONDS)
    }

    // ---- Configs ----

    private fun tourConfigs() {
        screen("configs-list") {
            goTo(1)
            activate("configs:ping")
            eventually("The probes must be done") {
                onMain { descendants(root()).filterIsInstance<TextView>().none { it.text.toString() == "…" && it.isShown } }
            }
            shot("configs-list", "Configs: the rows with their logos, pings and icons, the selected one in Medium; + and the ping button above")
        }
        if (remote) {
            screen("configs-row-focused") { goTo(1); focusOn("config:1:select"); shot("configs-row-focused", "Configs, a row has the remote's focus") }
            screen("configs-icon-focused") {
                goTo(1); focusOn("config:1:export")
                shot("configs-icon-focused", "Configs, a row's export icon has the focus: the row on surface, the icon in the text colour")
            }
            screen("configs-remove-focused") {
                goTo(1); focusOn("config:1:remove")
                shot("configs-remove-focused", "Configs, a row's close icon has the focus: in danger")
            }
        }
        screen("configs-add-menu") {
            goTo(1)
            activate("configs:add")
            awaitOverlay("addmenu")
            shot("configs-add-menu", "The add menu under +: ${if (isTv) "Receive from phone first on a TV, " else ""}Paste from clipboard, From file…, Create new…")
        }
        screen("configs-export-menu") {
            goTo(1)
            activate("config:1:export")
            awaitOverlay("select")
            shot("configs-export-menu", "The export menu under a row's icon: Export .toml… and Copy deep-link")
        }
        screen("configs-delete-confirm") {
            goTo(1)
            activate("config:1:remove")
            awaitOverlay("confirm")
            shot("configs-delete-confirm", "Delete config “…”?: Cancel first, the red Delete")
        }
    }

    // ---- The editor: the window is secure while it is up, so it is drawn from the view ----

    private fun tourEditor() {
        screen("editor-new") {
            goTo(1)
            activate("configs:add")
            awaitOverlay("addmenu")
            activate("add:create")
            awaitOverlay("editor")
            shot("editor-new", "New config: the desktop's form, the name first, the fields two to a line, the switches, the certificate, Cancel and Save at the end")
        }
        screen("editor-edit-focused") {
            goTo(1)
            activate("config:0:edit")
            awaitOverlay("editor")
            eventually("The name has the focus first or the window is in touch mode") { !remote || focusedKey() == "editor:name" }
            shot("editor-edit-focused", "Edit config: the name field focused (the accent border)")
        }
        screen("editor-discard-confirm") {
            goTo(1)
            activate("config:0:edit")
            awaitOverlay("editor")
            typeInto("editor:name", configNames[0] + " 2")
            backUntil("asked to discard") { overlayUp("confirm") }
            settle()
            shot("editor-discard-confirm", "Discard unsaved changes? over the editor: Cancel first, the red Discard")
        }
    }

    // ---- What a link, a file or a phone offers ----

    private fun tourImport() {
        fun ask(name: String, hostname: String) {
            onMain {
                method("showImportConfirmation", String::class.java, Boolean::class.javaPrimitiveType,
                    RoutingConfig.Import::class.java, String::class.java, Boolean::class.javaPrimitiveType)
                    .invoke(act(), name, false, RoutingConfig.Import(source.replace("vpn.invalid", hostname), false), "192.168.1.23", true)
            }
            awaitOverlay("confirm")
        }
        screen("import-confirm") {
            goTo(1)
            ask("Brand new", "brandnew.example.com")
            shot("import-confirm", "Add a VPN server from this link?: the server, who sent it, Cancel first and the red Import")
        }
        screen("import-replace") {
            goTo(1)
            ask(configNames[1], "other.example.com")
            shot("import-replace", "“…” already exists: Cancel, Add copy, and the red Replace")
        }
    }

    // ---- Split ----

    private fun tourSplit() {
        // A second profile with rules of its own, and a third with none, for the notice.
        SplitStore.edit(SplitPrefs(prefs), null) { profiles ->
            profiles.add("Work")
            profiles.addDomains("Work", "corp.example.com, *.internal.example.net")
            profiles.add("Streaming")
            profiles.select("Default")
        }
        screen("split-profiles-rules") {
            goTo(2)
            shot("split-profiles-rules", "Split: the toggle, the mode, the profile chips, the rules and the applications of Default")
        }
        screen("split-new-profile-input") {
            goTo(2)
            activate("profile:add")
            eventually("The name field must show") { onMain { root().findViewWithTag<View>("profile:name").visibility == View.VISIBLE } }
            // As the desktop's picture is taken: with "games" typed into it, so that the typed text and the caret are what is compared.
            typeInto("profile:name", "games")
            shot("split-new-profile-input", "Split: the + chip has opened the profile name field, with \"games\" typed into it")
            // Back closes the field before anything else, as the keyboard's own Back does first where one is up.
            backUntil("closed the name field") { onMain { root().findViewWithTag<View>("profile:name").visibility != View.VISIBLE } }
        }
        screen("split-mode-dropdown") {
            goTo(2)
            activate("split:mode")
            awaitOverlay("select")
            shot("split-mode-dropdown", "Split's Mode dropdown: Bypass VPN with the tick, Through VPN")
        }
        screen("split-after-recommended") {
            goTo(2)
            activate("domains:russia")
            eventually("The list must be added") { onMain { descendants(root()).count { (it.tag as? String)?.startsWith("domain:") == true } } > 20 }
            // From the top of the page, as the desktop's picture is: the header with "Recommended for Russia" and "Clear all" in danger.
            // (The tour once scrolled to the field first, which made this picture almost the next one.)
            onMain { (field("pageScroll").get(act()) as ScrollView?)?.scrollTo(0, 0) }
            settle(200)
            shot("split-after-recommended", "Split after Recommended for Russia, from the top: the rules as chips, Clear all in danger")
            scrollPageToEnd()
            shot("split-after-recommended-scrolled", "Split after Recommended for Russia, scrolled to the end: the applications")
        }
        screen("split-through-vpn-notice") {
            // Through VPN, with the selected config on a profile that has no rule: the desktop's notice.
            prefs.edit().putBoolean("through", true).putBoolean("split_enabled", true).commit()
            val list = org.json.JSONArray(ConfigStore.read(prefs))
            list.getJSONObject(0).put("profile", "Streaming")
            assertTrue(ConfigStore.write(prefs, list.toString()))
            recreated { }
            goTo(2)
            activate("profile:2")
            settle()
            shot("split-through-vpn-notice", "Split, Through VPN: the notice that the profile has no rule, and the edited profile in accent")
        }
    }

    private fun tourPicker() {
        screen("app-picker") {
            goTo(2)
            activate("apps:choose")
            awaitOverlay("picker")
            val scanning = act().getString(R.string.picker_scanning)
            eventually("The applications must be listed", 30_000) {
                onMain { root().findViewWithTag<TextView?>("picker:status")?.text?.toString() != scanning }
            }
            shot("app-picker", "The application picker: the search field, rows of name over package")
        }
    }

    // ---- Settings ----

    private fun tourSettings() {
        screen("settings-top") { goTo(3); shot("settings-top", "Settings from the top: General, Security") }
        screen("settings-theme-dropdown") {
            goTo(3)
            activate("settings:theme")
            awaitOverlay("select")
            shot("settings-theme-dropdown", "Settings' Theme dropdown: System, Light, Dark, the one in use ticked")
        }
        screen("settings-language-dropdown") {
            goTo(3)
            activate("settings:language")
            awaitOverlay("select")
            shot("settings-language-dropdown", "Settings' Language dropdown: English, Русский")
        }
        screen("settings-routes") {
            goTo(3)
            scrollPageTo("routes:input")
            shot("settings-routes", "Settings: the excluded routes as chips, Restore defaults and Clear all, the field")
        }
        // The update row in each state the updater's hooks reach; the line itself, its picture and the Settings tile's dot.
        updater("release", UpdatePolicy.Release("99.0.0", "https://example.invalid/update.apk",
            "https://example.invalid/SHA256SUMS-android.txt", "https://example.invalid/SHA256SUMS-android.txt.sig"))
        val states = listOf(
            Triple("idle", AndroidUpdater.State.IDLE, "Check for updates, and the version at the right"),
            Triple("current", AndroidUpdater.State.CURRENT, "Check for updates with the tick: up to date"),
            Triple("checking", AndroidUpdater.State.CHECKING, "Checking: the hopping dots, nothing to click"),
            Triple("available", AndroidUpdater.State.AVAILABLE, "Version 99.0.0 is available: the arrow down, the dot on Settings' tile"),
            Triple("downloading", AndroidUpdater.State.DOWNLOADING, "Downloading, 42%: the arrow turning"),
            Triple("ready", AndroidUpdater.State.READY, "Downloaded: a click installs"),
            Triple("installing", AndroidUpdater.State.INSTALLING, "Installing: nothing to click"),
            Triple("error", AndroidUpdater.State.ERROR, "A failed check: the reason, the retry arrow"))
        for ((label, state, what) in states) {
            screen("settings-bottom-update-$label") {
                goTo(3)
                updater("failure", if (state == AndroidUpdater.State.ERROR) UpdatePolicy.Failure.NETWORK else null)
                updater("failedCheck", state == AndroidUpdater.State.ERROR)
                updater("progress", 42)
                updater("committed", false)
                updater("state", state)
                onMain { method("refreshUpdateControls").invoke(act()); method("refreshUpdateDot").invoke(act()) }
                scrollPageToEnd()
                if (remote) focusOn("settings:update")
                shot("settings-bottom-update-$label", "Settings' foot, the Maintenance row: $what")
            }
        }
        updater("state", AndroidUpdater.State.IDLE); updater("release", null); updater("failure", null)
        updater("failedCheck", false); updater("progress", 0)
    }

    // ---- Logs ----

    private fun tourLogs() {
        screen("logs") {
            val file = File(context.filesDir, LogFile.NAME)
            file.delete()
            val lines = listOf(
                LogLines.INFO to "Connecting to frankfurt.example.com:443",
                LogLines.INFO to "Tunnel up on 10.0.0.2",
                LogLines.INFO to "example.com via the tunnel",
                LogLines.INFO to "video.example.net bypassed",
                LogLines.WARN to "Kill switch: blocking until the tunnel is back",
                LogLines.INFO to "Connecting to frankfurt.example.com:443",
                LogLines.ERROR to "Connection to the server was lost",
                LogLines.INFO to "Tunnel up on 10.0.0.2")
            var at = 1_793_104_496_000L
            for ((level, message) in lines) { LogFile.append(file, LogLines.entry(at, java.time.ZoneId.of("UTC"), level, message)); at += 2_300 }
            prefs.edit().putBoolean("logging", true).commit()
            goTo(4)
            shot("logs", "Logs: Clear and Copy above, the lines as HH:mm:ss LEVEL message, Auto-scroll below")
        }
        screen("logs-off") {
            prefs.edit().putBoolean("logging", false).commit()
            goTo(3)
            goTo(4)
            shot("logs-off", "Logs with logging off: the warning, Turn on")
            prefs.edit().putBoolean("logging", true).commit()
        }
    }

    // ---- Receive from phone: a TV's ----

    private fun tourReceive() {
        screen("receive-from-phone") {
            goTo(1)
            activate("configs:add")
            awaitOverlay("addmenu")
            activate("add:receive")
            awaitOverlay("receive")
            eventually("The port must be open") { onMain { field("linkReceiver").get(act()) } != null }
            shot("receive-from-phone", "Receive from phone: the QR code on its white tile, the address and the short hint, centred, and the ← that closes it (no button)")
            // A phone sends a link, as its browser does: the card goes on under the question.
            val receiver = onMain { field("linkReceiver").get(act()) as LinkReceiver }
            val page = URI(receiver.url())
            val body = "link=" + URLEncoder.encode(link, "UTF-8")
            val request = "POST ${page.rawPath} HTTP/1.1\r\nHost: ${page.host}:${page.port}\r\n" +
                "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: ${body.length}\r\n\r\n$body"
            val answer = Socket(page.host, page.port).use { socket ->
                socket.soTimeout = 10_000
                socket.getOutputStream().write(request.toByteArray())
                socket.getInputStream().readBytes().toString(Charsets.UTF_8)
            }
            assertTrue(answer, answer.startsWith("HTTP/1.1 200 "))
            eventually("The question must be asked", 40_000) { overlayUp("confirm") }
            shot("receive-asked", "Receive from phone: the text that came is asked about, over the card, which counts what came")
        }
    }

    // ---- With nothing saved: the last, since it ends the config list ----

    private fun tourEmpty() {
        screen("empty") {
            closeOverlays()
            assertTrue(ConfigStore.write(prefs, "[]"))
            prefs.edit().putInt("active", 0).commit()
            recreated { }
            goTo(0)
            shot("home-empty", "Home with no config: \"Add a config\", no ▾ and no +")
            goTo(1)
            shot("configs-empty", "Configs with no config: the link in the middle, no ping button")
        }
    }

    // ---- The pages the TV serves to a phone: static HTML, in each language, as it is, and with each theme forced ----

    @Test fun thePhonePagesAreSavedInBothLanguages() {
        output = Output("$leg-phone-pages")
        for (language in listOf("en", "ru")) {
            val localized = context.createConfigurationContext(Configuration(context.resources.configuration).apply { setLocale(Locale.forLanguageTag(language)) })
            fun words(id: Int): String = localized.getString(id)
            val pages = LinkReceiver.Pages(language, words(R.string.receive_page_title), words(R.string.receive_page_prompt),
                words(R.string.receive_page_file), words(R.string.receive_page_send), words(R.string.receive_page_note),
                words(R.string.receive_page_sent), words(R.string.receive_page_retry), words(R.string.receive_page_too_large),
                words(R.string.receive_page_busy), words(R.string.receive_page_wrong_code), words(R.string.receive_page_locked_out))
            assertTrue("The sent page says so in $language", pages.sent.contains(LinkReceiver.escape(words(R.string.receive_page_sent))))
            assertTrue("The busy page says so in $language", pages.busy.contains(LinkReceiver.escape(words(R.string.receive_page_busy))))
            val all = listOf("form" to pages.form, "sent" to pages.sent, "retry" to pages.retry, "too-large" to pages.tooLarge,
                "busy" to pages.busy, "wrong-code" to pages.wrongCode, "locked-out" to pages.lockedOut)
            for ((name, html) in all) {
                // As served: the phone's own theme decides.
                output!!.write("phone-$name-$language.html", html)
                // And with a theme forced, to look at both in one browser: the dark custom properties made the only ones, or taken out.
                val dark = Regex("@media \\(prefers-color-scheme:dark\\)\\{:root\\{([^}]*)\\}\\}").find(html)
                assertTrue("The page has its dark theme", dark != null)
                val tokens = dark!!.groupValues[1]
                output!!.write("phone-$name-$language-dark.html", html.replace(dark.value, ":root{$tokens}"))
                output!!.write("phone-$name-$language-light.html", html.replace(dark.value, ""))
            }
        }
        output!!.write("index.txt", "The pages the TV serves to a phone, as static HTML.\n" +
            "phone-<page>-<language>.html is the page as served (the phone's theme decides), -dark and -light the same with a theme forced.\n" +
            "Pages: form, sent, retry, too-large, busy, wrong-code, locked-out. Languages: en, ru.\n")
    }
}
