package org.freetunnel.android

import android.app.Activity
import android.app.Instrumentation
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.provider.Settings
import android.view.FocusFinder
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.inspector.WindowInspector
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import androidx.test.filters.SdkSuppress
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.*
import org.junit.Assume.assumeFalse
import org.junit.Before
import org.junit.Test
import java.util.concurrent.ExecutorService
import java.util.concurrent.TimeUnit

// Android TV: the TV launcher entry, a focus stop with a key and a ring on every
// control, focus and scroll kept when render() rebuilds the views, Back, and the
// screens TVs leave out. Both device legs run it: the TV leg for the TV, the
// phone leg for phones, which must work as before.
class TvDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private val prefs get() = context.getSharedPreferences("freetunnel", Context.MODE_PRIVATE)
    private val isTv get() =
        (context.resources.configuration.uiMode and Configuration.UI_MODE_TYPE_MASK) == Configuration.UI_MODE_TYPE_TELEVISION
    private var oldPreferences: Map<String, *> = emptyMap<String, Any>()
    private var activity: MainActivity? = null
    private var touchModeBefore: Boolean? = null
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

    private fun seed(count: Int) {
        val profiles = org.json.JSONArray()
        for (i in 0 until count) profiles.put(org.json.JSONObject().put("name", "Profile $i").put("toml", source))
        assertTrue(ConfigStore.write(prefs, profiles.toString()))
    }
    @Before fun setup() {
        oldPreferences = prefs.all
        prefs.edit().clear().putBoolean("logging", false).putStringSet("domains", setOf("example.com", "example.org")).commit()
        // Another installed app this one can see, for an app row on the routing page.
        val pm = context.packageManager
        val other = listOf(Intent.CATEGORY_LAUNCHER, Intent.CATEGORY_LEANBACK_LAUNCHER)
            .flatMap { pm.queryIntentActivities(Intent(Intent.ACTION_MAIN).addCategory(it), 0) }
            .map { it.activityInfo.packageName }.firstOrNull { it != context.packageName }
        if (other != null) prefs.edit().putStringSet("apps", setOf(other)).commit()
        seed(2)
    }
    @After fun cleanup() {
        activity?.let { opened ->
            // While this app's window has the focus, which Android requires to change it.
            touchModeBefore?.let { instrumentation.setInTouchMode(it) }
            instrumentation.runOnMainSync { opened.finish() }
        }
        serviceState(0)
        updater("officialBuild", null); updater("certificatePin", BuildConfig.RELEASE_CERTIFICATE_SHA256)
        updater("state", AndroidUpdater.State.IDLE)
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

    private fun field(name: String) = MainActivity::class.java.getDeclaredField(name).apply { isAccessible = true }
    private fun method(name: String, vararg types: Class<*>?) =
        MainActivity::class.java.getDeclaredMethod(name, *types).apply { isAccessible = true }
    private fun updater(name: String, value: Any?) =
        AndroidUpdater::class.java.getDeclaredField(name).apply { isAccessible = true }.set(null, value)
    private fun serviceState(state: Int) =
        FreeTunnelVpnService::class.java.getDeclaredField("currentState").apply { isAccessible = true }.setInt(null, state)
    // apksigner's digest of the debug signer, which Android CI passes on both legs. As
    // the pin, it makes this debug build official, so Settings shows its update action.
    private fun officialBuild() {
        val digest = InstrumentationRegistry.getArguments().getString("debugCertificate")
        assertTrue("Run with -Pandroid.testInstrumentationRunnerArguments.debugCertificate=<apksigner's SHA-256 digest>, as Android CI does",
            digest != null && Regex("[0-9a-f]{64}").matches(digest))
        updater("officialBuild", null); updater("certificatePin", digest)
    }
    private fun shell(command: String): String =
        ParcelFileDescriptor.AutoCloseInputStream(instrumentation.uiAutomation.executeShellCommand(command))
            .bufferedReader().use { it.readText() }

    // Runs block on the main thread and returns what it returns. What it throws is
    // thrown again here: a failed assertion on the main thread would crash the
    // instrumentation process and skip every later test.
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
    private fun open(): MainActivity {
        val opened = instrumentation.startActivitySync(Intent(context, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        activity = opened
        instrumentation.waitForIdleSync()
        return opened
    }
    private fun inTouchMode(a: MainActivity) = onMain { a.window.decorView.isInTouchMode }
    // As a remote's first key press does. A phone starts in touch mode, a TV without.
    private fun leaveTouchMode(a: MainActivity) {
        if (touchModeBefore == null) touchModeBefore = inTouchMode(a)
        instrumentation.setInTouchMode(false)
        val deadline = SystemClock.elapsedRealtime() + 2_000
        while (inTouchMode(a) && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(50)
        // The first arrow press leaves touch mode as well, and is used up by that.
        if (inTouchMode(a)) instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_DOWN)
        eventually("The window must leave touch mode") { !inTouchMode(a) }
    }
    private fun root(a: MainActivity) = field("root").get(a) as ViewGroup
    // Shows the page as a tab or Back would: render() rebuilds every view.
    private fun render(a: MainActivity, page: Int) {
        onMain { field("page").setInt(a, page); method("render").invoke(a) }
        instrumentation.waitForIdleSync()
    }
    private fun focusedKey(a: MainActivity): String? = onMain { a.currentFocus?.tag as? String }
    private fun focusOn(a: MainActivity, key: String) = onMain {
        val view = root(a).findViewWithTag<View>(key)
        assertNotNull("No control has the key $key", view)
        assertTrue("$key must take the focus", view.requestFocus())
    }
    private fun descendants(view: View): Sequence<View> = sequence {
        yield(view)
        if (view is ViewGroup) for (i in 0 until view.childCount) yieldAll(descendants(view.getChildAt(i)))
    }
    private fun shows(a: MainActivity, text: String): Boolean = onMain {
        ArrayList<View>().also { a.window.decorView.findViewsWithText(it, text, View.FIND_VIEWS_WITH_TEXT) }.isNotEmpty()
    }

    @Test fun theTvLauncherOpensMainActivityWithTheBanner() {
        val pm = context.packageManager
        assertEquals(MainActivity::class.java.name, pm.getLeanbackLaunchIntentForPackage(context.packageName)?.component?.className)
        assertEquals(MainActivity::class.java.name, pm.getLaunchIntentForPackage(context.packageName)?.component?.className)
        assertNotEquals("A TV launcher shows the application's banner", 0, context.applicationInfo.banner)
        // What these tests expect of a TV follows the ui mode, and Android CI tells the
        // TV leg by the leanback feature. A TV image without a TV ui mode would pass the
        // TV leg with a phone's expectations.
        assertEquals("A TV, and only a TV, reports a TV ui mode", pm.hasSystemFeature(PackageManager.FEATURE_LEANBACK), isTv)
    }

    @Test fun everyControlIsAFocusStopWithAKeyAndARing() {
        // Reconnect shows only while traffic is blocked, and Settings' update action
        // only in an official build.
        serviceState(6)
        officialBuild()
        val a = open()
        leaveTouchMode(a)
        for (page in 0..4) {
            render(a, page)
            onMain {
                val controls = descendants(root(a)).filter { it.isShown && it.isClickable }.toList()
                assertTrue("Page $page shows no controls", controls.isNotEmpty())
                for (view in controls) {
                    // The log, selectable on a phone, is a control of its own there.
                    if (view is TextView && view.isTextSelectable) { assertFalse("Selectable text on a TV", isTv); continue }
                    val name = "${view.javaClass.simpleName} \"${(view as? TextView)?.text ?: view.contentDescription}\" on page $page"
                    assertTrue("$name is no focus stop", view.isFocusable)
                    assertTrue("$name has no key", view.tag is String)
                    // A text field on a phone shows its cursor instead.
                    if (isTv || !view.isFocusableInTouchMode)
                        assertTrue("$name shows no focus ring", view.foreground?.isStateful == true)
                }
            }
        }
        render(a, 0)
        assertNotNull(onMain { root(a).findViewWithTag<View>("home:reconnect") })
        render(a, 3)
        assertNotNull(onMain { root(a).findViewWithTag<View>("settings:update") })
    }

    @Test fun theArrowsReachEveryControl() {
        serviceState(6)
        officialBuild()
        val a = open()
        leaveTouchMode(a)
        for (page in 0..4) {
            render(a, page)
            onMain {
                val content = root(a)
                val keys = descendants(content).filter { it.isShown && it.isFocusable }.mapNotNull { it.tag as? String }.toSet()
                // Every control the arrows reach from the page's tab, one press at a time.
                val seen = HashSet<View>()
                val queue = ArrayDeque<View>().apply { add(content.findViewWithTag<View>("tab:$page")) }
                while (queue.isNotEmpty()) {
                    val view = queue.removeFirst()
                    if (!seen.add(view)) continue
                    for (direction in listOf(View.FOCUS_UP, View.FOCUS_DOWN, View.FOCUS_LEFT, View.FOCUS_RIGHT))
                        FocusFinder.getInstance().findNextFocus(content, view, direction)?.let { queue.add(it) }
                }
                assertEquals("Controls on page $page that the arrows never reach", emptySet<String>(),
                    keys - seen.mapNotNull { it.tag as? String }.toSet())
            }
        }
    }

    @Test fun aRemoteLandsOnThePagesMainControl() {
        serviceState(6)
        val a = open()
        leaveTouchMode(a)
        render(a, 1)
        focusOn(a, "config:1:edit")
        // Nothing on Home has that key, as after Back from Configurations on a TV.
        render(a, 0)
        eventually("Reconnect first, while traffic is blocked") { focusedKey(a) == "home:reconnect" }
        serviceState(0)
        onMain { field("vpnState").setInt(a, 0) }
        render(a, 0)
        eventually("then Connect") { focusedKey(a) == "home:toggle" }
        render(a, 2)
        eventually("and the page's tab elsewhere") { focusedKey(a) == "tab:2" }
        // A remote's keys.
        render(a, 1)
        focusOn(a, "config:0:select")
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_RIGHT)
        eventually("Right from Select") { focusedKey(a) == "config:0:edit" }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_DOWN)
        eventually("Down to the next configuration") { focusedKey(a) == "config:1:edit" }
    }

    @SdkSuppress(minSdkVersion = 29)
    @Test fun focusAndScrollSurviveARebuild() {
        seed(12)
        val a = open()
        leaveTouchMode(a)
        render(a, 1)
        focusOn(a, "config:10:edit")
        instrumentation.waitForIdleSync()
        fun scrollY() = onMain { (field("pageScroll").get(a) as ScrollView).scrollY }
        val y = scrollY()
        assertTrue("Twelve configurations must scroll, or this shows nothing", y > 0)
        // As a ping result, Select or a saved edit rebuilds it.
        render(a, 1)
        eventually("The focus must stay") { focusedKey(a) == "config:10:edit" }
        assertEquals(y, scrollY())
        // Twice before a frame, as the last ping result and the end of the ping: the
        // second finds the first one's views, not focused or scrolled yet.
        onMain { method("render").invoke(a); method("render").invoke(a) }
        instrumentation.waitForIdleSync()
        eventually("The focus must stay over two rebuilds in one frame") { focusedKey(a) == "config:10:edit" }
        assertEquals(y, scrollY())
        // Deleting the focused one moves the focus to the one before.
        focusOn(a, "config:11:remove")
        onMain { method("confirmRemove", Int::class.javaPrimitiveType).invoke(a, 11) }
        instrumentation.waitForIdleSync()
        onMain {
            WindowInspector.getGlobalWindowViews().last { it.findViewById<View>(android.R.id.button1) != null }
                .findViewById<Button>(android.R.id.button1).performClick()
        }
        eventually("The focus must move to the one before") { focusedKey(a) == "config:10:remove" }
        // A VPN state broadcast rebuilds Home.
        render(a, 0)
        focusOn(a, "home:config")
        val before = onMain { root(a) }
        context.sendBroadcast(Intent(FreeTunnelVpnService.ACTION_STATE).setPackage(context.packageName))
        eventually("The broadcast must rebuild Home") { onMain { root(a) } !== before }
        eventually("The focus must stay") { focusedKey(a) == "home:config" }
    }

    @Test fun inTouchModeARebuildFocusesNothing() {
        val a = open()
        if (touchModeBefore == null) touchModeBefore = inTouchMode(a)
        instrumentation.setInTouchMode(true)
        eventually("The window must be in touch mode") { inTouchMode(a) }
        render(a, 2)
        // A tap focuses the text field. Restored after a rebuild, the focus would
        // bring up the keyboard by itself.
        onMain { assertTrue(root(a).findViewWithTag<View>("domains:input").requestFocus()) }
        render(a, 2)
        assertNull(onMain { a.currentFocus })
    }

    @Test fun backGoesHomeFirstOnATv() {
        val a = open()
        render(a, 3)
        if (isTv) {
            leaveTouchMode(a)
            // Where OK on the Settings tab leaves it.
            focusOn(a, "tab:3")
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        instrumentation.waitForIdleSync()
        if (isTv) {
            assertEquals("Back on a TV goes to Home", 0, onMain { field("page").getInt(a) })
            eventually("on Home's own control: on the Settings tab, the next OK would go back") { focusedKey(a) == "home:toggle" }
            assertFalse(onMain { a.isFinishing })
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        }
        // From Home on a TV, and from any page on a phone, as before.
        eventually("Back must leave the app") { onMain { a.isFinishing || !a.hasWindowFocus() } }
    }

    @Test fun alwaysOnIsOfferedOnlyWhereAndroidHasIt() {
        val a = open()
        render(a, 3)
        val screen = context.packageManager.resolveActivity(Intent(Settings.ACTION_VPN_SETTINGS), 0) != null
        if (!isTv) assertTrue("Package visibility must not hide Android's VPN settings on a phone", screen)
        assertEquals(screen, shows(a, context.getString(R.string.text_android_vpn_settings)))
        assertEquals(!screen, shows(a, context.getString(R.string.text_no_always_on_here)))
    }

    @Test fun onATvContentKeepsClearOfTheEdges() {
        val a = open()
        render(a, 4)
        val padding = if (isTv) onMain { Math.round(48 * a.resources.displayMetrics.density) } else 0
        eventually("Overscan padding on a TV, none on a phone") { onMain { root(a).paddingLeft } == padding }
        val log = onMain { descendants(root(a)).filterIsInstance<TextView>().last() }
        assertEquals("On a TV, selectable text would take the arrows", !isTv, onMain { log.isTextSelectable })
    }

    @Test fun aMissingScreenIsReportedInsteadOfCrashing() {
        val a = open()
        render(a, 1)
        // Nothing handles this action, as a TV may have nothing for a file or VPN consent.
        val missing = Intent("org.freetunnel.android.test.NO_SUCH_SCREEN")
        assertEquals(false, onMain { method("startForResult", Intent::class.java, Int::class.javaPrimitiveType).invoke(a, missing, 99) })
        assertFalse(onMain { a.isFinishing })
        // The file picker asks for OPEN_DOCUMENT first, and GET_CONTENT only without it.
        // These monitors answer instead of the pickers, so no other app opens.
        fun picker(action: String) = instrumentation.addMonitor(
            IntentFilter(action, "*/*").apply { addCategory(Intent.CATEGORY_OPENABLE) },
            Instrumentation.ActivityResult(Activity.RESULT_CANCELED, null), true)
        val openDocument = picker(Intent.ACTION_OPEN_DOCUMENT)
        val getContent = picker(Intent.ACTION_GET_CONTENT)
        try {
            onMain { method("pickFile").invoke(a) }
            assertEquals(1, openDocument.hits)
            assertEquals(0, getContent.hits)
        } finally {
            instrumentation.removeMonitor(openDocument)
            instrumentation.removeMonitor(getContent)
        }
        // Where there is no OPEN_DOCUMENT, as on such a TV, starting it throws.
        val content = picker(Intent.ACTION_GET_CONTENT)
        val noDocuments = object : Instrumentation.ActivityMonitor() {
            override fun onStartActivity(intent: Intent): Instrumentation.ActivityResult? =
                if (intent.action == Intent.ACTION_OPEN_DOCUMENT) throw ActivityNotFoundException("No OPEN_DOCUMENT here") else null
        }.also { instrumentation.addMonitor(it) }
        try {
            onMain { method("pickFile").invoke(a) }
            assertEquals("GET_CONTENT where OPEN_DOCUMENT is missing", 1, content.hits)
            assertFalse(onMain { a.isFinishing })
        } finally {
            instrumentation.removeMonitor(content)
            instrumentation.removeMonitor(noDocuments)
        }
    }

    // The updater's guard (C1 in the TV plan): without the install permission, Install
    // update opens Android's screen for it, or says where the setting is on a TV that
    // has none, and stays available. Taking the permission away stops the app, and
    // these tests with it, so Android CI runs this alone before it grants the
    // permission, with withoutInstallPermission=true. With the permission it is skipped.
    @Test fun installWithoutThePermissionKeepsTheUpdateReady() {
        val allowed = context.packageManager.canRequestPackageInstalls()
        if (InstrumentationRegistry.getArguments().getString("withoutInstallPermission") == "true")
            assertFalse("Run this before granting REQUEST_INSTALL_PACKAGES", allowed)
        else assumeFalse("Runs only without the install permission", allowed)
        val resolved = shell("cmd package resolve-activity --brief -a android.settings.MANAGE_UNKNOWN_APP_SOURCES -d package:${context.packageName}")
        // Where Android has the screen, this answers instead of it, so that no Settings
        // window opens. Where it has none, starting it throws.
        val screen = if (resolved.contains("No activity found")) null else instrumentation.addMonitor(
            IntentFilter(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES).apply { addDataScheme("package") },
            Instrumentation.ActivityResult(Activity.RESULT_CANCELED, null), true)
        try {
            updater("state", AndroidUpdater.State.READY)
            val a = open()
            onMain { AndroidUpdater.install(a) }
            instrumentation.waitForIdleSync()
            screen?.let { assertEquals("Install update must open Android's screen for the permission", 1, it.hits) }
            assertFalse(onMain { a.isFinishing || a.isDestroyed })
            assertEquals("Install update stays available", AndroidUpdater.State.READY, AndroidUpdater.state)
        } finally {
            screen?.let { instrumentation.removeMonitor(it) }
        }
    }

    // With a remote, the focus must never land on "Show TOML and password" by itself,
    // where an OK shows the password to the room: not as the editor opens, and not
    // after a Save that the check refuses.
    @SdkSuppress(minSdkVersion = 29)
    @Test fun aRemoteNeverLandsOnShowTomlAndPassword() {
        val a = open()
        leaveTouchMode(a)
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 0) }
        instrumentation.waitForIdleSync()
        val editor = onMain { WindowInspector.getGlobalWindowViews().last { it.findViewById<View>(android.R.id.button1) != null } }
        // The check runs on the activity's worker and answers on the main thread.
        fun checked() {
            (field("validationWorker").get(a) as ExecutorService).submit {}.get(30, TimeUnit.SECONDS)
            instrumentation.waitForIdleSync()
        }
        fun requests() = onMain { field("validationRequest").getLong(a) }
        try {
            eventually("The editor must open on the name") {
                onMain { (editor.findFocus() as? EditText)?.text?.toString() == "Profile 0" }
            }
            val save = onMain { editor.findViewById<Button>(android.R.id.button1) }
            val before = requests()
            onMain {
                descendants(editor).filterIsInstance<EditText>().last().setText("not a configuration")
                assertTrue("Save must take the focus", save.requestFocus())
                save.performClick()
                save.performClick()
            }
            assertEquals("A second OK while the check runs starts no other", before + 1, requests())
            checked()
            onMain {
                assertTrue("A refused Save leaves the editor open", editor.isShown)
                assertSame("A refused Save keeps the focus", save, editor.findFocus())
                assertTrue(save.isEnabled)
                assertFalse(descendants(editor).filterIsInstance<CheckBox>().single().isChecked)
                save.performClick()
            }
            assertEquals("Once refused, Save checks again", before + 2, requests())
            checked()
            // A tap, as on a phone, greys Save out until the check answers.
            instrumentation.setInTouchMode(true)
            eventually("The editor must be in touch mode") { onMain { editor.isInTouchMode } }
            onMain { save.performClick(); assertFalse("Save must be greyed out during a check", save.isEnabled) }
            checked()
            assertTrue(onMain { save.isEnabled })
        } finally {
            onMain { editor.findViewById<Button>(android.R.id.button2).performClick() }
            instrumentation.waitForIdleSync()
        }
    }

    // While a check or an install runs, the update action ignores OK and keeps a
    // remote's focus. Disabled, it would send the focus to the top of the window.
    @Test fun theUpdateActionKeepsTheFocusWhileItWaits() {
        officialBuild()
        val a = open()
        leaveTouchMode(a)
        render(a, 3)
        focusOn(a, "settings:update")
        val action = onMain { root(a).findViewWithTag<TextView>("settings:update") }
        updater("committed", false)
        for (state in listOf(AndroidUpdater.State.CHECKING, AndroidUpdater.State.INSTALLING)) {
            updater("state", state)
            onMain { method("refreshUpdateControls").invoke(a) }
            instrumentation.waitForIdleSync()
            assertFalse("$state: the action must ignore OK", onMain { action.isClickable })
            assertEquals("$state: the focus must stay", "settings:update", focusedKey(a))
        }
        updater("state", AndroidUpdater.State.IDLE)
        onMain { method("refreshUpdateControls").invoke(a) }
        assertTrue(onMain { action.isClickable })
        assertEquals("settings:update", focusedKey(a))
    }

    // Dialogs stay dark in both themes, and a remote needs to see which button has the focus.
    @SdkSuppress(minSdkVersion = 29)
    @Test fun dialogButtonsAndCheckBoxesShowTheFocus() {
        assertTrue("Grant REQUEST_INSTALL_PACKAGES appop before device tests", context.packageManager.canRequestPackageInstalls())
        // With the VPN on, Install update warns first.
        serviceState(2)
        val a = open()
        val dialogs = listOf<Pair<String, () -> Any?>>(
            "Delete" to { method("confirmRemove", Int::class.javaPrimitiveType).invoke(a, 0) },
            "the editor" to { method("editConfig", Int::class.javaPrimitiveType).invoke(a, -1) },
            "the app picker" to { method("appPicker").invoke(a) },
            "Install update" to { method("installUpdate").invoke(a) })
        for ((name, start) in dialogs) {
            onMain { start() }
            instrumentation.waitForIdleSync()
            onMain {
                val window = WindowInspector.getGlobalWindowViews().last { it.findViewById<View>(android.R.id.button2) != null }
                try {
                    for (id in listOf(android.R.id.button1, android.R.id.button2))
                        assertTrue("$name: a button shows no focus ring", window.findViewById<Button>(id).foreground?.isStateful == true)
                    for (box in descendants(window).filterIsInstance<CheckBox>())
                        assertTrue("$name: \"${box.text}\" shows no focus ring", box.foreground?.isStateful == true)
                } finally {
                    window.findViewById<Button>(android.R.id.button2).performClick()
                }
            }
            instrumentation.waitForIdleSync()
        }
    }
}
