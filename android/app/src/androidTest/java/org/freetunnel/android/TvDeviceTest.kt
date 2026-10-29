package org.freetunnel.android

import android.app.Activity
import android.app.Instrumentation
import android.content.ActivityNotFoundException
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.StateListDrawable
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.provider.Settings
import android.view.FocusFinder
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.text.InputFilter
import android.text.TextUtils
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.ScrollView
import android.widget.TextView
import androidx.test.filters.SdkSuppress
import androidx.test.platform.app.InstrumentationRegistry
import io.nayuki.qrcodegen.QrCode
import org.junit.After
import org.junit.Assert.*
import org.junit.Assume.assumeFalse
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import java.net.ConnectException
import java.net.Socket
import java.net.URI
import java.net.URLEncoder
import java.util.concurrent.ExecutorService
import java.util.concurrent.TimeUnit

// Android TV: the TV launcher entry, a focus stop with a key and the desktop's focus
// look on every control, focus and scroll kept when render() rebuilds the views, the
// overlays (every dialog, menu, picker and confirmation of the app is one, a view of
// its own window found by its tag) that survive it and take the focus, Back, the
// screens TVs leave out, and Receive from phone. Both device legs run it: the TV leg for the TV, the phone leg
// for phones, which must work as before.
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
        updater("release", null); updater("failure", null); updater("failedCheck", false); updater("committed", false)
        updater("progress", 0)
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
        // An official build makes its own check 1.2 s after the app starts, unless it made one in the last 12 hours: these tests are
        // about what the line shows, and put the states there themselves.
        prefs.edit().putLong(AndroidUpdater.LAST_LAUNCH_CHECK, System.currentTimeMillis()).commit()
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
        // The input focus is the window's, and it comes after the activity is resumed (the splash screen is handed over first), or later while a
        // system dialog has it, such as an emulator launcher's "isn't responding". Keys go to the window that has it and Android answers an
        // app's read of the clipboard with nothing unless the app does, so no test begins before it is this one.
        eventually("The window must have the input focus", 30_000) { onMain { opened.hasWindowFocus() } }
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
    // The window's one view: the page, the overlays and the toast. pageRoot is the page alone.
    private fun root(a: MainActivity) = field("host").get(a) as ViewGroup
    private fun pageRoot(a: MainActivity) = field("pageRoot").get(a) as ViewGroup
    private fun theme(a: MainActivity) = Theme.of(field("dark").getBoolean(a))
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
    // pageRoot holds the bar of tabs, which render() makes first, and the page under it. This is the page alone.
    private fun pageBelowTheTabs(a: MainActivity): Sequence<View> =
        pageRoot(a).let { page -> (1 until page.childCount).asSequence().flatMap { descendants(page.getChildAt(it)) } }
    // A link or a button that is a control of its own. The selector's name is a Ui.LinkText as well, as the text inside the selector, which is
    // the control (it is the selector that is focusable and clicked, and the name has neither), so it is not one.
    private fun linksAndButtons(views: Sequence<View>): List<View> =
        views.filter { it.isShown && (it is Button || it is Ui.LinkText && (it.isFocusable || it.isClickable)) }.toList()
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

    // What a control looks like at rest, pressed and focused, read from its background: the fill
    // of each state, and the colour of the edge, which a border has no getter for, so it is the
    // pixel at the middle of the left edge of what the background draws in that state.
    private class FocusLook(val restFill: Int, val focusedFill: Int, val restEdge: Int, val focusedEdge: Int)
    private fun fillOf(drawable: Drawable) = (drawable as GradientDrawable).color?.defaultColor ?: Color.TRANSPARENT
    // What states.current returns is a state of the view's StateListDrawable, which fades its states in and out over 120 ms: the one it has just
    // entered is drawn at an alpha of about 0 for the first frames, which read as a transparent edge (0, or 3 a moment later). So a copy of it is
    // drawn, at its own alpha.
    private fun edgeOf(drawable: Drawable): Int {
        val copy = (drawable.constantState?.newDrawable()?.mutate() ?: drawable).also { it.alpha = 255 }
        val bitmap = Bitmap.createBitmap(160, 240, Bitmap.Config.ARGB_8888)
        copy.setBounds(0, 0, 160, 240)
        copy.draw(Canvas(bitmap))
        return bitmap.getPixel(0, 120)
    }
    private fun focusLook(view: View): FocusLook {
        assertNull("${view.javaClass.simpleName} \"${view.tag}\" has a foreground: a ring, not the desktop's look", view.foreground)
        val states = view.background as? StateListDrawable
        assertTrue("${view.javaClass.simpleName} \"${view.tag}\" has no background that changes with its state", states != null && states.isStateful)
        states!!
        val saved = states.state
        try {
            states.setState(intArrayOf())
            val rest = states.current
            val restFill = fillOf(rest)
            val restEdge = edgeOf(rest)
            states.setState(intArrayOf(android.R.attr.state_focused))
            val focused = states.current
            return FocusLook(restFill, fillOf(focused), restEdge, edgeOf(focused))
        } finally {
            states.setState(saved)
            view.refreshDrawableState()
        }
    }
    // A control shows its focus as the desktop shows the pointer on it, and no stronger: a background that changes with the state, no
    // foreground, and never the accent as a fill (the selected things are accent, and a focus is not a selection). By kind:
    //  - a text field: the border turns accent, on the fill it rests on; a box of text a remote scrolls: surfaceHover, with no border;
    //  - a select box (the editor's): the same, its border accent under the pointer;
    //  - a button or a chip: its hover colour, which is a fill the control rests on already or the next one to it;
    //  - a link, a dropdown's value, a back arrow, a ChipX, a toggle, a row's icon and the logo: no fill of their own;
    //  - a nav tile, a row, a menu row, the header's buttons and Home's +: surface (the current tab keeps infoBg).
    private fun assertFocusLook(view: View, name: String, theme: Theme) {
        val look = focusLook(view)
        val key = view.tag as String
        val surfaces = setOf(Color.TRANSPARENT, theme.surface, theme.surfaceHover, theme.infoBg, theme.bg)
        when {
            view is EditText -> {
                assertEquals("$name: the border of a focused field", theme.accent, look.focusedEdge)
                assertEquals("$name: the border of a field at rest", theme.inputBorder, look.restEdge)
                assertEquals("$name: its fill stays", look.restFill, look.focusedFill)
            }
            // A box of text a remote scrolls, such as the log: the surface, which goes to surfaceHover as any control that rests on
            // it does, with no border (the desktop's log box never draws one).
            key == "log:box" -> {
                assertEquals("$name: a focused box of text is surfaceHover", theme.surfaceHover, look.focusedFill)
                assertEquals("$name: with no border", theme.surfaceHover, look.focusedEdge)
                assertEquals("$name: a box of text at rest", theme.surface, look.restEdge)
            }
            // The editor's select boxes are fields that open a choice.
            key == "editor:profile" || key == "editor:protocol" -> {
                assertEquals("$name: the border of a focused select box", theme.accent, look.focusedEdge)
                assertEquals("$name: the border of a select box at rest", theme.inputBorder, look.restEdge)
                assertEquals("$name: its fill stays", look.restFill, look.focusedFill)
            }
            // The buttons: the hover colour of the kind.
            key == "confirm:ok" -> assertEquals("$name: the red button goes to the darker red", theme.destructiveHover, look.focusedFill)
            key == "editor:save" -> assertEquals("$name: Save goes to the darker accent", theme.primaryHover, look.focusedFill)
            // The editor's Cancel is the desktop's only button that dismisses a card (outlined on the page, surface under the pointer); the
            // Receive card has no button at all, and is closed by its back arrow, as the picker's is.
            key == "editor:cancel" -> assertEquals("$name: a card's dismissing button goes to surface", theme.surface, look.focusedFill)
            view is Button -> assertEquals("$name: a dialog's button goes to surfaceHover", theme.surfaceHover, look.focusedFill)
            // Chips: surfaceHover, and the selected one, which the desktop leaves as it is, a step darker.
            Regex("profile:\\d+|profile:add").matches(key) ->
                assertTrue("$name: a chip takes its hover colour, not a strong fill: ${Integer.toHexString(look.focusedFill)}",
                    look.focusedFill == theme.surfaceHover || look.focusedFill == theme.primaryHover && look.restFill == theme.accent)
            // Content that changes colour and no fill: links, values, arrows, ChipXs, row icons, toggles and the logo.
            key == "home:toggle" || key.startsWith("switch:") || key.startsWith("logs:autoscroll") || key == "split:enabled" || key == "editor:ipv6" || key == "editor:skip" || key == "editor:antidpi" ||
                Regex("(domain|app|route):\\d+|profile:\\d+:remove|config:\\d+:(export|edit|remove)").matches(key) ||
                view is Ui.LinkText || key.endsWith(":back") || key == "settings:theme" || key == "settings:language" || key == "split:mode" ->
                assertEquals("$name: no fill of its own", Color.TRANSPARENT, look.focusedFill)
            // Everything else that is filled is filled with the surface its hover is: a tile, a row, a menu row, the header's buttons.
            else -> {
                assertTrue("$name: its focus is a step from the page, as the pointer's hover is, not the accent: ${Integer.toHexString(look.focusedFill)}",
                    look.focusedFill in surfaces && look.focusedFill != theme.accent)
            }
        }
        // Whatever it is, the focus is never the accent as a fill, nor danger as one on something that is not the red button.
        if (view !is EditText && key != "confirm:ok") {
            assertNotEquals("$name: the accent is for what is selected", theme.accent, look.focusedFill)
            assertNotEquals("$name: danger is for the red button", theme.danger, look.focusedFill)
        }
    }

    @Test fun everyControlIsAFocusStopWithAKeyAndTheDesktopFocusLook() {
        // Home is looked at while traffic is blocked too (it has no more controls then), and Settings' update action
        // is only in an official build.
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
                    assertFocusLook(view, name, theme(a))
                }
            }
        }
        render(a, 0)
        // While traffic is blocked the desktop's Home has the logo, the selector and the +, as it has at any other time: no link of
        // Android's own to reconnect now (the service does that by itself, with its backoff).
        assertEquals(setOf("home:toggle", "home:config", "home:add"),
            onMain { pageBelowTheTabs(a).filter { it.isShown && it.isFocusable }.mapNotNull { it.tag as? String }.toSet() })
        assertEquals("and no link or button", emptyList<View>(), onMain { linksAndButtons(pageBelowTheTabs(a)) })
        render(a, 3)
        assertNotNull(onMain { root(a).findViewWithTag<View>("settings:update") })
    }

    // A tab rests on the page, and the current one on infoBg, over it; with the focus a tab is what the desktop's tile is under the pointer:
    // surface, grown to 1.08. The current tab, which the desktop leaves as it is, keeps its infoBg and only grows. The bar does not cut the
    // grown tile off at its padding.
    @Test fun theCurrentTabRestsOnInfoBgAndEveryTabFocusesAsItIsUnderThePointer() {
        val a = open()
        leaveTouchMode(a)
        for (page in 0..4) {
            render(a, page)
            onMain {
                val theme = theme(a)
                for (tab in 0..4) {
                    val look = focusLook(root(a).findViewWithTag<View>("tab:$tab"))
                    assertEquals("Tab $tab on page $page", if (tab == page) theme.infoBg else Color.TRANSPARENT, look.restFill)
                    assertEquals("Tab $tab on page $page focuses as it is under the pointer", if (tab == page) theme.infoBg else theme.surface, look.focusedFill)
                }
                val bar = root(a).findViewWithTag<View>("tab:0").parent as ViewGroup
                assertFalse("The bar clips what it holds to its padding, and cuts a grown tile off", bar.clipToPadding)
            }
        }
        render(a, 2)
        focusOn(a, "tab:2")
        eventually("The current tab grows to 1.08 with the focus") { near(1.08f, onMain { root(a).findViewWithTag<View>("tab:2").scaleX }, 0.01f) }
        focusOn(a, "tab:1")
        eventually("and the one that had it is its size again") { near(1f, onMain { root(a).findViewWithTag<View>("tab:2").scaleX }, 0.01f) }
        eventually("while the one that has it grows") { near(1.08f, onMain { root(a).findViewWithTag<View>("tab:1").scaleX }, 0.01f) }
    }

    // The focus is around what a control holds, never across the screen: Home's selector, the links
    // and the values of the dropdowns hug their words.
    @Test fun theSelectorAndTheLinksAreNoWiderThanTheirWords() {
        officialBuild()
        val a = open()
        leaveTouchMode(a)
        render(a, 0)
        onMain {
            val selector = root(a).findViewWithTag<View>("home:config")
            assertTrue("The selector is ${selector.width} wide on a ${root(a).width} wide screen", selector.width < root(a).width / 2)
        }
        var links = 0
        for (page in 0..4) {
            render(a, page)
            onMain {
                for (link in descendants(root(a)).filterIsInstance<Ui.LinkText>().filter { it.isShown }) {
                    links++
                    val words = Math.ceil(link.paint.measureText(link.text.toString()).toDouble()).toInt()
                    assertTrue("\"${link.text}\" is ${link.width} wide: its words are $words, its padding ${link.paddingLeft + link.paddingRight}",
                        link.width <= words + link.paddingLeft + link.paddingRight + 1)
                }
                for (key in listOf("settings:theme", "split:mode"))
                    root(a).findViewWithTag<View?>(key)?.let { assertTrue("$key spans the screen", it.width < root(a).width / 2) }
            }
        }
        assertTrue("There are links to check", links >= 5)
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
                val keys = descendants(content).filter { it.isShown && it.isFocusable }.mapNotNull { it.tag as? String }.toMutableSet()
                // On a phone the log's text is selectable, so a stop of its own inside the box, and a ScrollView is offered to the arrows only
                // where none of what is in it is a stop: the text is the stop there, and the box is on a TV.
                if (content.findViewWithTag<TextView?>("log:text")?.isTextSelectable == true) keys.remove("log:box")
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
        eventually("The logo first, while traffic is blocked as at any other time") { focusedKey(a) == "home:toggle" }
        serviceState(0)
        onMain { field("vpnState").setInt(a, 0) }
        render(a, 0)
        eventually("and with no session") { focusedKey(a) == "home:toggle" }
        render(a, 2)
        eventually("and the page's tab elsewhere") { focusedKey(a) == "tab:2" }
        // A remote's keys: Right from a row reaches its icons one after another, Left comes back, and Down is the next row.
        render(a, 1)
        focusOn(a, "config:0:select")
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_RIGHT)
        eventually("Right from a row reaches its export icon") { focusedKey(a) == "config:0:export" }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_RIGHT)
        eventually("then edit") { focusedKey(a) == "config:0:edit" }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_RIGHT)
        eventually("then remove") { focusedKey(a) == "config:0:remove" }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_LEFT)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_LEFT)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_LEFT)
        eventually("Left three times is back on the row") { focusedKey(a) == "config:0:select" }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_DOWN)
        eventually("Down from a row to the next row") { focusedKey(a) == "config:1:select" }
    }

    // ---- Home: the desktop's. The logo is the control, with the line, the selector, + and the speed tiles ----

    private fun hero(a: MainActivity) = field("hero").get(a) as HeroMotion
    // Home as it is for a state of the VPN; stopping is the time between Disconnect and the service saying it is off.
    private fun vpnShows(a: MainActivity, state: Int, stopping: Boolean = false, page: Int = 0) {
        serviceState(state)
        onMain { field("vpnState").setInt(a, state); field("stopping").setBoolean(a, stopping) }
        render(a, page)
    }
    private fun statusLine(a: MainActivity): TextView = onMain { root(a).findViewWithTag<TextView>("home:status") }
    private fun logoOf(a: MainActivity): ImageView = onMain { (root(a).findViewWithTag<ViewGroup>("home:toggle")).getChildAt(0) as ImageView }
    private fun selectorName(a: MainActivity): String = onMain { (root(a).findViewWithTag<ViewGroup>("home:config").getChildAt(0) as TextView).text.toString() }
    private fun near(expected: Float, actual: Float, delta: Float = 0.02f) = Math.abs(expected - actual) <= delta
    private fun dp(a: MainActivity, n: Float) = n * a.resources.displayMetrics.density
    private fun topOf(view: View): Int = IntArray(2).also { view.getLocationInWindow(it) }[1]

    @Test fun theLogoIsHomesControlAndNothingElseOnItIsAButtonOrAWordForTheState() {
        val a = open()
        leaveTouchMode(a)
        render(a, 0)
        eventually("The logo has the first focus") { focusedKey(a) == "home:toggle" }
        onMain {
            val logo = root(a).findViewWithTag<View>("home:toggle")
            assertTrue("The logo is a control", logo.isFocusable && logo.isClickable)
            assertEquals("132 across", Math.round(dp(a, 132f)), logo.width)
            assertEquals("and as high", logo.width, logo.height)
            assertFalse("The logo is no Button", logo is Button)
            assertEquals(context.getString(R.string.text_connect_freetunnel), logo.contentDescription)
            val words = descendants(pageRoot(a)).filterIsInstance<TextView>().map { it.text.toString() }
            for (word in listOf("Connect", "Disconnect", "Connected", "Disconnected", "Подключить", "Отключить", "Подключено", "Отключено"))
                assertFalse("\"$word\" is on Home", word in words)
            assertEquals("Only the tabs, the logo, the selector and its + are controls", setOf("tab:0", "tab:1", "tab:2", "tab:3", "tab:4", "home:toggle", "home:config", "home:add"),
                descendants(pageRoot(a)).filter { it.isFocusable }.mapNotNull { it.tag as? String }.toSet())
        }
        // + and the selector with a config lead to the add menu and the dropdown.
        click(a, "home:add")
        eventually("+ opens Configs with the add menu") { onMain { field("page").getInt(a) } == 1 && overlayTag(a, "addmenu") != null }
    }

    @Test fun withNoConfigsTheSelectorAndTheLogoLeadToTheAddMenu() {
        seed(0)
        val a = open()
        render(a, 0)
        onMain {
            val selector = root(a).findViewWithTag<ViewGroup>("home:config")
            assertEquals(context.getString(R.string.text_add_configuration), (selector.getChildAt(0) as TextView).text.toString())
            assertEquals("It has no ▾", 1, selector.childCount)
            assertNull("and no +", root(a).findViewWithTag<View?>("home:add"))
        }
        click(a, "home:toggle")
        eventually("The logo opens Configs with its add menu") { onMain { field("page").getInt(a) } == 1 && overlayTag(a, "addmenu") != null }
        assertNull("Configs has no ping with nothing to ping", onMain { root(a).findViewWithTag<View?>("configs:ping") })
        assertEquals("The words in the middle of the page are hidden under their own menu", View.INVISIBLE,
            onMain { root(a).findViewWithTag<View>("configs:hint").visibility })
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        eventually("Back closes the menu") { overlayTag(a, "addmenu") == null }
        assertEquals("and shows them again", View.VISIBLE, onMain { root(a).findViewWithTag<View>("configs:hint").visibility })
        render(a, 0)
        click(a, "home:config")
        eventually("The words on Home lead there as well") { onMain { field("page").getInt(a) } == 1 && overlayTag(a, "addmenu") != null }
    }

    // The logo's opacity and the line under it are the state's: nothing off, Connecting… (also while traffic is blocked and the service
    // reconnects by itself: the desktop has no such state, and nothing but the logo is a button on Home), the time, and Disconnecting…
    @Test fun theLogoAndTheLineUnderItFollowTheStateOfTheVpn() {
        val a = open()
        val theme = theme(a)
        vpnShows(a, 0)
        assertEquals("Nothing under the logo while it is off", "", statusLine(a).text.toString())
        eventually("and it is dim") { near(0.5f, logoOf(a).alpha) }
        assertTrue(near(0f, statusLine(a).alpha))
        // 6 is traffic blocked by the kill switch while the service reconnects by itself: the desktop's backend is "connecting" while its tunnel
        // reconnects, and its Home says Connecting… in textDim with the logo pulsing, so this does too.
        for (state in listOf(1, 3, 4, 5, 6)) {
            vpnShows(a, state)
            assertEquals("State $state", context.getString(R.string.text_connecting), statusLine(a).text.toString())
            assertEquals(theme.textDim, statusLine(a).currentTextColor)
            eventually("State $state is 0.7") { near(0.7f, logoOf(a).alpha) && near(1f, statusLine(a).alpha) }
        }
        // Up: the time since the service said it was connected, in the desktop's form, and the logo at full strength.
        val started = FreeTunnelVpnService::class.java.getDeclaredField("sessionStartedAt").apply { isAccessible = true }
        started.setLong(null, SystemClock.elapsedRealtime() - 65_000)
        try {
            vpnShows(a, 2)
            assertTrue("The time is HH:MM:SS: " + statusLine(a).text, Regex("00:01:0[5-9]").matches(statusLine(a).text))
        } finally {
            started.setLong(null, 0L)
        }
        eventually("Connected is 1.0") { near(1f, logoOf(a).alpha) }
        // Blocked is one of those: no link to reconnect now, and no button either, for the logo is the only control.
        vpnShows(a, 6)
        assertEquals("Nothing on Home is a button or a link: the logo, the selector and its + are all", emptyList<View>(), onMain { linksAndButtons(pageBelowTheTabs(a)) })
        eventually("Blocked is 0.7") { near(0.7f, logoOf(a).alpha) }
        eventually("and the logo pulses, as when it connects") { onMain { hero(a).isPulsing } }
        // From Disconnect until the service says it is off.
        vpnShows(a, 2, stopping = true)
        assertEquals(context.getString(R.string.text_disconnecting), statusLine(a).text.toString())
        eventually("Disconnecting is 0.7") { near(0.7f, logoOf(a).alpha) }
    }

    // The logo rises when a session starts and the line takes its room over the same 280 ms; with none it sits 12 lower.
    @Test fun theLogoSitsTwelveLowerWithNoSessionAndRisesWhenOneStarts() {
        val a = open()
        vpnShows(a, 0)
        val down = Math.round(dp(a, 12f)).toFloat()
        eventually("Idle, 12 lower") { near(down, onMain { root(a).findViewWithTag<View>("home:toggle").translationY }, 1f) }
        assertEquals("No room under it", 0, onMain { hero(a).roomNow() })
        vpnShows(a, 1)
        eventually("It rises to its place") { near(0f, onMain { root(a).findViewWithTag<View>("home:toggle").translationY }, 1f) }
        eventually("and the line has its room") { onMain { hero(a).roomNow() } > Math.round(dp(a, 20f)) }
        vpnShows(a, 0)
        eventually("It sinks back when the session ends") { near(down, onMain { root(a).findViewWithTag<View>("home:toggle").translationY }, 1f) }
        eventually("and the line gives its room back") { onMain { hero(a).roomNow() } == 0 }
    }

    // While connecting or disconnecting it swings between 0.97 and 1.05 and comes back to 1.0 when that ends. With a remote's focus, which a
    // TV's logo has, the whole of it is 1.05 times larger (the focus is its size, and the pulse goes on under it). The swing itself is
    // sampled only where animators run: Android CI turns them off (animator_duration_scale 0), where a pulse ends in its first frame and the
    // loop is not repeated; HeroValuesTest holds the swing on the JVM.
    @Test fun theLogoPulsesWhileConnectingAndSettlesWhenItStops() {
        val a = open()
        vpnShows(a, 1)
        eventually("It pulses while connecting") { onMain { hero(a).isPulsing } }
        if (android.animation.ValueAnimator.areAnimatorsEnabled()) {
            val scales = ArrayList<Float>()
            val deadline = SystemClock.elapsedRealtime() + 3_000
            while (SystemClock.elapsedRealtime() < deadline) {
                // Its own swing, without the 1.05 a remote's focus holds it at.
                scales.add(onMain { hero(a).scaleNow() / hero(a).heldNow() })
                SystemClock.sleep(50)
            }
            assertTrue("Never beyond its swing: $scales", scales.all { it in 0.965f..1.055f })
            assertTrue("It goes up to 1.05: $scales", scales.maxOrNull()!! > 1.035f)
            assertTrue("and down to 0.97: $scales", scales.minOrNull()!! < 0.985f)
        }
        // A render() of the same state does not start it again from the beginning.
        render(a, 0)
        assertTrue(onMain { hero(a).isPulsing })
        vpnShows(a, 2)
        eventually("It settles to its size when connected") { onMain { !hero(a).isPulsing && near(1f, hero(a).scaleNow() / hero(a).heldNow(), 0.005f) } }
        vpnShows(a, 2, stopping = true)
        eventually("and pulses again while disconnecting") { onMain { hero(a).isPulsing } }
        vpnShows(a, 0)
        eventually("and settles when it is off") { onMain { !hero(a).isPulsing && near(1f, hero(a).scaleNow() / hero(a).heldNow(), 0.005f) } }
    }

    // A remote's focus on the logo holds it at 1.05, the pulse's peak, in place of a fill behind it, which would wash its greys out; the
    // box has no background to fill. Letting go of the focus returns it to its size.
    @Test fun theLogoIsHeldAtThePulsesPeakWhileItHasTheFocus() {
        val a = open()
        leaveTouchMode(a)
        vpnShows(a, 0)
        eventually("The logo has the first focus") { focusedKey(a) == "home:toggle" }
        eventually("and is held at 1.05") { near(1.05f, onMain { hero(a).scaleNow() }, 0.005f) }
        eventually("on the view itself") { near(1.05f, onMain { root(a).findViewWithTag<View>("home:toggle").scaleX }, 0.005f) }
        onMain {
            val box = root(a).findViewWithTag<View>("home:toggle")
            val look = focusLook(box)
            assertEquals("Nothing behind it at rest", Color.TRANSPARENT, look.restFill)
            assertEquals("and nothing behind it with the focus", Color.TRANSPARENT, look.focusedFill)
        }
        focusOn(a, "home:config")
        eventually("Focus gone, it is its size") { near(1f, onMain { hero(a).scaleNow() }, 0.005f) }
        // A rebuild gives the focus back to the new logo, which is as large as the old one was.
        focusOn(a, "home:toggle")
        render(a, 0)
        eventually("The focus is back on the logo after a rebuild") { focusedKey(a) == "home:toggle" }
        eventually("and it is held again") { near(1.05f, onMain { root(a).findViewWithTag<View>("home:toggle").scaleX }, 0.005f) }
        // Leaving Home leaves nothing of it for the next time the logo is shown without the focus.
        render(a, 2)
        onMain { assertEquals(1f, hero(a).heldNow(), 0f) }
    }

    // OK on a remote shrinks the logo to 0.96 while it is down; the click is Connect, which is held back here, before the VPN consent.
    @Test fun aPressShrinksTheLogoAndItsClickConnects() {
        val a = open()
        leaveTouchMode(a)
        render(a, 0)
        focusOn(a, "home:toggle")
        // Nothing a click starts gets as far as asking for the VPN: its check waits behind this, and is dropped, as a newer check
        // would drop it, before it can run on.
        val gate = java.util.concurrent.CountDownLatch(1)
        (field("validationWorker").get(a) as ExecutorService).execute { gate.await() }
        fun requests() = onMain { field("validationRequest").getLong(a) }
        fun dropWhatWasAsked() = onMain { field("validationRequest").setLong(a, field("validationRequest").getLong(a) + 1) }
        val before = requests()
        try {
            instrumentation.sendKeySync(KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DPAD_CENTER))
            // The press and the focus the logo has under OK multiply: 0.96 of its focused size.
            eventually("OK presses the logo to 0.96") { near(0.96f * onMain { hero(a).heldNow() }, onMain { hero(a).scaleNow() }, 0.01f) }
            instrumentation.sendKeySync(KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_DPAD_CENTER))
            eventually("and it comes back when OK is let go") { near(onMain { hero(a).heldNow() }, onMain { hero(a).scaleNow() }, 0.01f) }
            eventually("The click connects: a check of the config begins") { requests() > before }
            // The desktop's notices are errors and what an import did; the logo's pulse and its line are the progress of a connection.
            assertEquals("No notice for the check itself", emptyList<String>(), toastTexts(a))
            dropWhatWasAsked()
            // A double tap counts once: the second is not a second toggle.
            SystemClock.sleep(500)
            val asked = requests()
            click(a, "home:toggle")
            click(a, "home:toggle")
            assertEquals("Two clicks together are one", asked + 1, requests())
            dropWhatWasAsked()
        } finally {
            gate.countDown()
        }
    }

    // The line under the logo and the speeds change once a second without the page being built again, so the focus stays where it is.
    @Test fun theTimerTicksWithoutRebuildingThePage() {
        val a = open()
        leaveTouchMode(a)
        val started = FreeTunnelVpnService::class.java.getDeclaredField("sessionStartedAt").apply { isAccessible = true }
        started.setLong(null, SystemClock.elapsedRealtime() - 1_000)
        try {
            vpnShows(a, 2)
            focusOn(a, "home:config")
            val before = onMain { root(a).findViewWithTag<View>("home:config") }
            val first = statusLine(a).text.toString()
            eventually("The time moves on", 5_000) { statusLine(a).text.toString() != first }
            SystemClock.sleep(1_500)
            assertSame("The page is not built again for it", before, onMain { root(a).findViewWithTag<View>("home:config") })
            assertEquals("and the focus stays", "home:config", focusedKey(a))
            // The two tiles are not controls, and say MB/s.
            onMain {
                val tiles = descendants(pageRoot(a)).filterIsInstance<TextView>().filter { it.text.endsWith(context.getString(R.string.text_mb_per_s)) }.toList()
                assertEquals(2, tiles.size)
                for (tile in tiles) {
                    assertTrue("\"${tile.text}\" is a number with one decimal", Regex("\\d+\\.\\d MB/s|\\d+\\.\\d МБ/с").matches(tile.text))
                    val box = tile.parent as View
                    assertFalse("A speed tile is no control", box.isFocusable || box.isClickable)
                    assertEquals("116 by 44", Math.round(dp(a, 116f)), box.width)
                    assertEquals(Math.round(dp(a, 44f)), box.height)
                }
                // ↓ and ↑ are drawn, as Inter draws them at the desktop's 14 px (8.9 by 10.3), in the colour of the speed: success and textDim.
                val arrows = tiles.map { (it.parent as ViewGroup).getChildAt(0) as ImageView }
                assertEquals(listOf(theme(a).success, theme(a).textDim), arrows.map { it.imageTintList!!.defaultColor })
                // A pixel either way is the rounding of a width that is not a whole number of dp.
                for (arrow in arrows) assertTrue("an arrow is 8.9 across", Math.abs(Math.round(dp(a, 8.9f)) - arrow.drawable.intrinsicWidth) <= 1)
            }
        } finally {
            started.setLong(null, 0L)
        }
    }

    // Home's server choice: a dropdown right on Home, under the selector, with the selected config in accent and the first focus.
    // A row selects it and closes the dropdown, and the page is still Home.
    @Test fun aRemoteChoosesAServerOnHomeWithoutLeavingThePage() {
        val a = open()
        leaveTouchMode(a)
        render(a, 0)
        focusOn(a, "home:config")
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_CENTER)
        eventually("OK opens the dropdown right on Home") { overlayTag(a, "homepicker") != null }
        assertEquals("The page is still Home", 0, onMain { field("page").getInt(a) })
        eventually("The selected config has the focus") { focusedKey(a) == "home:pick:0" }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_DOWN)
        eventually("Down reaches the next config") { focusedKey(a) == "home:pick:1" }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_CENTER)
        eventually("A pick closes the dropdown") { overlayTag(a, "homepicker") == null }
        assertEquals(0, onMain { field("page").getInt(a) })
        assertEquals("The pick is the selected config", 1, onMain { field("activeIndex").getInt(a) })
        assertEquals("and it is saved", 1, prefs.getInt("active", 0))
        eventually("The selector says so") { selectorName(a) == "Profile 1" }
        eventually("The focus is back on the selector") { focusedKey(a) == "home:config" }
        // The one that is selected already does nothing.
        click(a, "home:config")
        eventually("The dropdown opens") { overlayTag(a, "homepicker") != null }
        val requests = onMain { field("validationRequest").getLong(a) }
        click(a, "home:pick:1")
        eventually("It closes") { overlayTag(a, "homepicker") == null }
        assertEquals("and changes nothing", requests, onMain { field("validationRequest").getLong(a) })
    }

    // Back closes the dropdown before anything else: it stays on Home, and in the app; so does a tap outside it.
    @Test fun backAndATapOutsideCloseTheHomeDropdownFirst() {
        val a = open()
        render(a, 0)
        click(a, "home:config")
        eventually("The dropdown opens") { overlayTag(a, "homepicker") != null }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        eventually("Back closes it") { overlayTag(a, "homepicker") == null }
        assertFalse("and does not leave the app", onMain { a.isFinishing })
        assertEquals(0, onMain { field("page").getInt(a) })
        click(a, "home:config")
        eventually("The dropdown opens") { overlayTag(a, "homepicker") != null }
        // The backdrop is what a tap outside lands on.
        val layer = overlayTag(a, "homepicker") as ViewGroup
        onMain { assertTrue(layer.getChildAt(0).performClick()) }
        eventually("A tap outside closes it") { overlayTag(a, "homepicker") == null }
        // And a page that is no longer Home takes it away.
        click(a, "home:config")
        eventually("The dropdown opens") { overlayTag(a, "homepicker") != null }
        render(a, 3)
        eventually("Another page closes it") { overlayTag(a, "homepicker") == null }
    }

    // As wide as the widest name at 14 plus 30 (140 at least), a row of 40 for each, three showing, under the selector 6 down and
    // centred on it, the selected one in accent.
    @Test fun theHomeDropdownIsPlacedAndSizedAsTheDesktopsIs() {
        seed(5)
        val a = open()
        render(a, 0)
        click(a, "home:config")
        eventually("The dropdown opens") { overlayTag(a, "homepicker") != null }
        instrumentation.waitForIdleSync()
        onMain {
            val layer = root(a).findViewWithTag<ViewGroup>("overlay:homepicker")
            // The panel is the layer's last child (its shadow is the one before it).
            val panel = layer.getChildAt(layer.childCount - 1)
            // Under and centred on the whole row, the + included, which is 24 high; the selector alone is the name and its ▾.
            val selector = root(a).findViewWithTag<View>("home:selection")
            assertTrue("The row is 24 high: ${selector.height}", Math.abs(selector.height - dp(a, 24f)) <= 1.5f)
            val rows = descendants(panel).filter { (it.tag as? String)?.startsWith("home:pick:") == true }.toList()
            assertEquals("A row for each config", 5, rows.size)
            for (row in rows) assertTrue("A row is 40 high", Math.abs(row.height - dp(a, 40f)) <= 1.5f)
            // 5 above the rows and 7 under them: 12 more than the list, as the desktop's card is.
            assertTrue("Three rows show: ${panel.height}", Math.abs(panel.height - dp(a, 3 * 40f + 12f)) <= 3f)
            assertTrue("140 across for short names: ${panel.width}", Math.abs(panel.width - dp(a, 140f)) <= 3f)
            assertTrue("Under the selector, 6 down", Math.abs((topOf(panel) - (topOf(selector) + selector.height)) - dp(a, 6f)) <= 3f)
            val panelAt = IntArray(2).also { panel.getLocationInWindow(it) }
            val selectorAt = IntArray(2).also { selector.getLocationInWindow(it) }
            assertTrue("Centred on it", Math.abs((panelAt[0] + panel.width / 2f) - (selectorAt[0] + selector.width / 2f)) <= 3f)
            // Not on the name alone: the selector's centre is 14 left of the row's.
            val name = root(a).findViewWithTag<View>("home:config")
            val nameAt = IntArray(2).also { name.getLocationInWindow(it) }
            assertTrue("and not on the name and its arrow", Math.abs((panelAt[0] + panel.width / 2f) - (nameAt[0] + name.width / 2f)) > dp(a, 8f))
            assertEquals("It casts a shadow of its own", true, layer.let { (it as OverlayLayer).shadow() != null })
            val colors = rows.map { ((it as ViewGroup).getChildAt(0) as TextView).textColors.defaultColor }
            assertEquals("The selected one is accent, the others the text colour", listOf(theme(a).accent) + List(4) { theme(a).text }, colors)
        }
    }

    // The desktop's cfgPopup is at the selector's bottom plus 6 whatever room is left under it. A popup of the usual kind turns over above
    // its control when it does not fit under it before the overscan: on a TV 540 dp high, with a session's line under the logo, it covered
    // the logo and the timer instead of the speed tiles.
    @Test fun theHomeDropdownIsNeverTurnedOverAboveItsSelector() {
        seed(5)
        val a = open()
        vpnShows(a, 2)
        click(a, "home:config")
        eventually("The dropdown opens") { overlayTag(a, "homepicker") != null }
        instrumentation.waitForIdleSync()
        onMain {
            val layer = root(a).findViewWithTag<ViewGroup>("overlay:homepicker")
            val panel = layer.getChildAt(layer.childCount - 1)
            val selector = root(a).findViewWithTag<View>("home:selection")
            val gap = topOf(panel) - (topOf(selector) + selector.height)
            assertTrue("Under the selector, 6 down, with a session up: $gap", Math.abs(gap - dp(a, 6f)) <= 3f)
        }
    }

    // The logo, its line and the selector are one column in the middle of the room above the speed tiles, 6 below the middle (HomePage.qml:
    // verticalCenterOffset). A FrameLayout puts a centred child at the middle plus its top margin less its bottom margin, so a margin of 12
    // put it 12 below, and the toast and the dropdown with it.
    @Test fun homesColumnIsSixBelowTheMiddleOfTheRoomAboveTheSpeedTiles() {
        val a = open()
        vpnShows(a, 0)
        onMain {
            val column = root(a).findViewWithTag<View>("home:toggle").parent as View
            val area = column.parent as View
            val offset = (topOf(column) + column.height / 2f) - (topOf(area) + area.height / 2f)
            assertEquals("6 below the middle", dp(a, 6f), offset, dp(a, 1.5f))
        }
    }

    // The empty Configs page's "Add a config" is in the middle of the whole page, the header included (anchors.centerIn: parent), not 11 above it.
    @Test fun theEmptyConfigsPagesWordsAreInTheMiddleOfThePage() {
        assertTrue(ConfigStore.write(prefs, "[]"))
        val a = open()
        render(a, 1)
        onMain {
            val hint = root(a).findViewWithTag<View>("configs:hint")
            val page = field("pageScroll").get(a) as ScrollView
            val offset = (topOf(hint) + hint.height / 2f) - (topOf(page) + page.height / 2f)
            assertEquals("In the middle of the page", 0f, offset, dp(a, 1.5f))
        }
    }

    // The toast on Home sits in the middle of the gap between the selector and the speed tiles.
    @Test fun aToastOnHomeSitsBetweenTheSelectorAndTheSpeedTiles() {
        val a = open()
        render(a, 0)
        onMain { method("showToast", CharSequence::class.java).invoke(a, "Config added: Profile 1") }
        instrumentation.waitForIdleSync()
        eventually("The toast lies in the gap, in the middle of it") {
            onMain {
                val toast = (field("toastLayer").get(a) as ViewGroup).getChildAt(0)
                val selector = root(a).findViewWithTag<View>("home:config")
                val tiles = field("homeSpeedRow").get(a) as View
                val above = topOf(toast) - (topOf(selector) + selector.height)
                val below = topOf(tiles) - (topOf(toast) + toast.height)
                toast.height > 0 && above >= 0 && below >= 0 && Math.abs(above - below) <= dp(a, 4f)
            }
        }
    }

    // ---- Configs: rows, with icons for what the desktop's have icons for ----

    private fun rowTexts(a: MainActivity, index: Int): List<String> = onMain {
        val row = root(a).findViewWithTag<ViewGroup>("config:$index:select")
        (0 until row.childCount).map { row.getChildAt(it) }.filterIsInstance<TextView>().map { it.text.toString() }
    }
    private fun pingOf(a: MainActivity, index: Int): String =
        rowTexts(a, index).firstOrNull { it == "…" || it == "—" || Regex("\\d+ \\S+").matches(it) } ?: ""

    @Test fun aConfigRowHasIconsAndNoTextButtons() {
        val a = open()
        render(a, 1)
        onMain {
            val words = descendants(pageRoot(a)).filterIsInstance<TextView>().map { it.text.toString() }
            for (word in listOf("Select", "Edit", "✕", "●  selected", "TrustTunnel  ·  TOML", "Выбрать", "Изменить"))
                assertFalse("\"$word\" is on Configs", word in words)
            assertTrue("No Button on the page", descendants(pageRoot(a)).none { it is Button })
            val row0 = root(a).findViewWithTag<ViewGroup>("config:0:select")
            val row1 = root(a).findViewWithTag<ViewGroup>("config:1:select")
            assertTrue("The row is 48 high", Math.abs(row0.height - dp(a, 48f)) <= 1.5f)
            assertTrue("and the next is 56 down", Math.abs((topOf(row1) - topOf(row0)) - dp(a, 56f)) <= 1.5f)
            // Its logo is whole for the selected config and at 0.4 for the others; its name is Medium for the selected one.
            assertTrue(near(1f, (row0.getChildAt(0) as ImageView).alpha))
            assertTrue(near(0.4f, (row1.getChildAt(0) as ImageView).alpha))
            assertSame(Fonts.medium(context), (row0.getChildAt(1) as TextView).typeface)
            assertSame("and Regular for the others", Fonts.regular(context), (row1.getChildAt(1) as TextView).typeface)
            // The three icons, 26 wide each.
            for (kind in listOf("export", "edit", "remove")) {
                val icon = root(a).findViewWithTag<View>("config:0:$kind")
                assertTrue("The $kind icon is a control", icon.isFocusable && icon.isClickable)
                assertTrue("26 wide: ${icon.width}", Math.abs(icon.width - dp(a, 26f)) <= 1.5f)
            }
        }
    }

    // A row fills with surface while one of its icons has the focus, as the desktop's does under the pointer, and the icon alone changes
    // its colour (the text colour, danger for close). With the row itself focused it is surface too, and the icons stay as they are.
    @Test fun aFocusedIconSitsOnAnActivatedRowAndAFocusedRowIsSurface() {
        val a = open()
        leaveTouchMode(a)
        render(a, 1)
        val theme = theme(a)
        // The colour an icon is drawn in now: its tint for the state of its cell, which it takes (duplicateParentState).
        fun tintOf(key: String): Int {
            val cell = root(a).findViewWithTag<ViewGroup>(key)
            return (cell.getChildAt(0) as ImageView).imageTintList!!.getColorForState(cell.drawableState, 0)
        }
        focusOn(a, "config:1:edit")
        onMain {
            val row = root(a).findViewWithTag<View>("config:1:select")
            assertTrue("The row is activated while its icon has the focus", row.isActivated)
            assertEquals("and rests on surface", theme.surface, fillOf((row.background as StateListDrawable).current))
            val cell = root(a).findViewWithTag<View>("config:1:edit")
            assertEquals("The icon's cell has no fill of its own", Color.TRANSPARENT, fillOf((cell.background as StateListDrawable).current))
            assertEquals("its icon turns to the text colour", theme.text, tintOf("config:1:edit"))
        }
        focusOn(a, "config:1:remove")
        onMain {
            assertEquals("Close turns danger, on no fill", theme.danger, tintOf("config:1:remove"))
            assertEquals(Color.TRANSPARENT, fillOf((root(a).findViewWithTag<View>("config:1:remove").background as StateListDrawable).current))
        }
        focusOn(a, "config:1:select")
        onMain {
            val row = root(a).findViewWithTag<View>("config:1:select")
            assertFalse(row.isActivated)
            assertEquals("A focused row is surface, not a strong fill", theme.surface, fillOf((row.background as StateListDrawable).current))
            for (kind in listOf("export", "edit", "remove"))
                assertEquals("$kind stays textDim on it, as on the desktop's row under the pointer", theme.textDim, tintOf("config:1:$kind"))
        }
        focusOn(a, "config:0:select")
        onMain { assertFalse("and the other row is not filled", root(a).findViewWithTag<View>("config:1:select").isActivated) }
    }

    // The selected config's badge: connected, or connecting… for every state on the way, in the same pill; not while disconnecting.
    @Test fun theBadgeIsOnTheSelectedConfigWhileItIsConnectedOrConnecting() {
        val a = open()
        val theme = theme(a)
        val connected = context.getString(R.string.config_badge_connected)
        val connecting = context.getString(R.string.config_badge_connecting)
        fun badges(index: Int) = rowTexts(a, index).filter { it == connected || it == connecting }
        vpnShows(a, 0, page = 1)
        assertEquals(emptyList<String>(), badges(0))
        vpnShows(a, 2, page = 1)
        assertEquals(listOf(connected), badges(0))
        assertEquals("Only on the selected one", emptyList<String>(), badges(1))
        onMain {
            val badge = descendants(root(a).findViewWithTag<View>("config:0:select")).filterIsInstance<TextView>().first { it.text.toString() == connected }
            assertEquals(theme.success, badge.textColors.defaultColor)
            assertTrue("A pill 20 high", Math.abs(badge.height - dp(a, 20f)) <= 1.5f)
        }
        for (state in listOf(1, 3, 4, 5, 6)) {
            vpnShows(a, state, page = 1)
            assertEquals("State $state", listOf(connecting), badges(0))
        }
        onMain {
            val badge = descendants(root(a).findViewWithTag<View>("config:0:select")).filterIsInstance<TextView>().first { it.text.toString() == connecting }
            assertEquals(theme.textDim, badge.textColors.defaultColor)
        }
        vpnShows(a, 2, stopping = true, page = 1)
        assertEquals("Gone while it disconnects", emptyList<String>(), badges(0))
    }

    // The export icon opens a menu under it with the two ways out, without a check mark; the link goes to the clipboard, the notice says
    // it holds the password, and the link is the config's own. Without a screen to save to, the file says so.
    @Test fun theExportIconOffersAFileAndALinkAndTheLinkIsTheConfigs() {
        val a = open()
        render(a, 1)
        click(a, "config:0:export")
        eventually("The export menu opens") { overlayTag(a, "select") != null }
        val menu = overlayTag(a, "select")!!
        val texts = onMain { descendants(menu).filterIsInstance<TextView>().map { it.text.toString() }.toList() }
        assertEquals("Two rows, and no check mark", listOf(context.getString(R.string.export_toml), context.getString(R.string.export_deep_link)), texts)
        click(a, "select:1")
        eventually("The menu closes") { overlayTag(a, "select") == null }
        eventually("The notice says the link holds the password") { context.getString(R.string.deep_link_copied) in toastTexts(a) }
        fun clip(): String? = onMain {
            (a.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager).primaryClip?.getItemAt(0)?.text?.toString()
        }
        eventually("The clipboard holds a link") { clip()?.startsWith("tt://?") == true }
        val imported = DeepLinkImport.parse(clip()!!)
        assertEquals("Profile 0", imported.name)
        assertTrue(imported.toml, imported.toml.contains("hostname = \"vpn.invalid\"") && imported.toml.contains("password = \"test\""))
        if (android.os.Build.VERSION.SDK_INT >= 33) {
            val clipboard = a.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            assertTrue("Marked as sensitive, so a keyboard's history leaves it out",
                onMain { clipboard.primaryClip?.description?.extras?.getBoolean(android.content.ClipDescription.EXTRA_IS_SENSITIVE) == true })
        }
        // The file: a phone's screen to save to is not driven from here; a TV without one says so.
        val picker = context.packageManager.resolveActivity(Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
            .setType("application/octet-stream"), 0)
        if (picker == null) {
            click(a, "config:0:export")
            eventually("The export menu opens again") { overlayTag(a, "select") != null }
            click(a, "select:0")
            eventually("With no screen to save to, it says so") { context.getString(R.string.export_no_picker) in toastTexts(a) }
        }
    }

    // The probes run side by side, held back here so that "…" can be seen: "…" while pending, then the time or a dash for any failure.
    @Test fun theProbesShowPendingThenTheTimeOrADash() {
        // The configs say 127.0.0.1. On Android getLoopbackAddress() is ::1, which a socket bound to it answers nobody who connects to 127.0.0.1.
        val loopback = java.net.InetAddress.getByName("127.0.0.1")
        val up = java.net.ServerSocket(0, 1, loopback)
        val closedPort = java.net.ServerSocket(0, 1, loopback).use { it.localPort }
        try {
            val profiles = org.json.JSONArray()
            profiles.put(org.json.JSONObject().put("name", "Up").put("toml", source.replace("192.0.2.1:443", "127.0.0.1:${up.localPort}")))
            profiles.put(org.json.JSONObject().put("name", "Down").put("toml", source.replace("192.0.2.1:443", "127.0.0.1:$closedPort")))
            assertTrue(ConfigStore.write(prefs, profiles.toString()))
            val a = open()
            leaveTouchMode(a)
            render(a, 1)
            focusOn(a, "config:1:edit")
            assertEquals("Nothing is known yet", listOf("", ""), listOf(pingOf(a, 0), pingOf(a, 1)))
            val gate = java.util.concurrent.CountDownLatch(1)
            val pool = field("pingWorker").get(a) as ExecutorService
            repeat(4) { pool.execute { gate.await() } }
            click(a, "configs:ping")
            assertEquals("Pending while it is waiting", listOf("…", "…"), listOf(pingOf(a, 0), pingOf(a, 1)))
            // Each result builds the page again, and the focus is where it was.
            eventually("The focus stays through the rebuild that shows the pending marks") { focusedKey(a) == "config:1:edit" }
            gate.countDown()
            eventually("Both answer") { pingOf(a, 0) != "…" && pingOf(a, 1) != "…" }
            assertTrue("A time: " + pingOf(a, 0), Regex("\\d+ (ms|мс)").matches(pingOf(a, 0)))
            assertEquals("A dash for a refusal", "—", pingOf(a, 1))
            instrumentation.waitForIdleSync()
            eventually("and stays through the results") { focusedKey(a) == "config:1:edit" }
        } finally {
            up.close()
        }
    }

    // On a phone a row is moved by pressing and holding it, then dragging: it lands in the slot it was dropped on, the order is saved, and the
    // selection stays on the same config.
    @Test fun aPhoneReordersARowByPressingAndDragging() {
        assumeFalse("A TV has no finger, and the desktop has no keys to move a row", isTv)
        seed(3)
        val a = open()
        render(a, 1)
        val row = byKey(a, "config:0:select")
        val at = IntArray(2)
        onMain { row.getLocationOnScreen(at) }
        val x = at[0] + row.width / 3f
        val y = at[1] + row.height / 2f
        val pitch = onMain { Math.round(56 * a.resources.displayMetrics.density) }
        val down = SystemClock.uptimeMillis()
        fun send(action: Int, atY: Float) {
            instrumentation.sendPointerSync(MotionEvent.obtain(down, SystemClock.uptimeMillis(), action, x, atY, 0))
        }
        send(MotionEvent.ACTION_DOWN, y)
        SystemClock.sleep(android.view.ViewConfiguration.getLongPressTimeout() + 400L)
        for (step in 1..8) { send(MotionEvent.ACTION_MOVE, y + 2f * pitch * step / 8f); SystemClock.sleep(30) }
        send(MotionEvent.ACTION_UP, y + 2f * pitch)
        eventually("The row lands in the slot it was dropped on, and it is saved") { saved().map { it.getString("name") } == listOf("Profile 1", "Profile 2", "Profile 0") }
        assertEquals("The selection is on the same config", 2, onMain { field("activeIndex").getInt(a) })
        eventually("and the page shows it") { rowTexts(a, 2).contains("Profile 0") }
    }

    // A tap on a row selects it, and the page stays where it is; the one selected already does nothing.
    @Test fun aTapOnARowSelectsItWithoutLeavingThePage() {
        val a = open()
        render(a, 1)
        click(a, "config:1:select")
        assertEquals("The selected config", 1, onMain { field("activeIndex").getInt(a) })
        assertEquals("is saved", 1, prefs.getInt("active", 0))
        assertEquals("on the same page", 1, onMain { field("page").getInt(a) })
        val requests = onMain { field("validationRequest").getLong(a) }
        click(a, "config:1:select")
        assertEquals("Selecting the selected one does nothing", requests, onMain { field("validationRequest").getLong(a) })
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
        answerYes(a)
        eventually("The focus must move to the one before") { focusedKey(a) == "config:10:remove" }
        // A VPN state broadcast rebuilds Home.
        render(a, 0)
        focusOn(a, "home:config")
        val before = onMain { root(a).findViewWithTag<View>("home:config") }
        context.sendBroadcast(Intent(FreeTunnelVpnService.ACTION_STATE).setPackage(context.packageName))
        eventually("The broadcast must rebuild Home") { onMain { root(a).findViewWithTag<View>("home:config") } !== before }
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

    // The one rebuild that does give a field the focus in touch mode is the one an Enter asks for, for the next entry: the page names the
    // field to have the cursor again. Any other rebuild asks for nothing (the test above).
    @Test fun inTouchModeAnEnterLeavesTheCursorInTheDomainsFieldForTheNext() {
        val a = open()
        if (touchModeBefore == null) touchModeBefore = inTouchMode(a)
        instrumentation.setInTouchMode(true)
        eventually("The window must be in touch mode") { inTouchMode(a) }
        render(a, 2)
        onMain { assertTrue(root(a).findViewWithTag<View>("domains:input").requestFocus()) }
        val added = onMain { method("enterDomains", String::class.java).invoke(a, "one.example") as Boolean }
        assertTrue("The domain is added", added)
        eventually("The field has the cursor again, for the next one") { focusedKey(a) == "domains:input" }
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

    @Test fun onATvContentKeepsClearOfTheEdges() {
        val a = open()
        render(a, 4)
        val padding = if (isTv) onMain { Math.round(48 * a.resources.displayMetrics.density) } else 0
        eventually("Overscan padding on a TV, none on a phone") { onMain { root(a).paddingLeft } == padding }
        val log = onMain { root(a).findViewWithTag<TextView>("log:text") }
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

    // The editor opens on the name with a remote, and Save answers once while its check runs. A form the desktop's checks refuse is said so in a
    // toast, in the desktop's words, with no check started and the focus where it was; one that passes is saved over the config it was opened on.
    @Test fun aRemoteStartsOnTheNameAndSaveIsAnsweredOnceWhileItChecks() {
        val a = open()
        leaveTouchMode(a)
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 0) }
        instrumentation.waitForIdleSync()
        // The check runs on the activity's worker and answers on the main thread.
        fun checked() {
            (field("validationWorker").get(a) as ExecutorService).submit {}.get(30, TimeUnit.SECONDS)
            instrumentation.waitForIdleSync()
        }
        fun requests() = onMain { field("validationRequest").getLong(a) }
        val name = byKey(a, "editor:name") as EditText
        val host = byKey(a, "editor:host") as EditText
        val save = byKey(a, "editor:save")
        eventually("The editor must open on the name") { focusedKey(a) == "editor:name" }
        assertEquals("Profile 0", onMain { name.text.toString() })
        assertEquals("vpn.invalid", onMain { host.text.toString() })
        val before = requests()
        onMain {
            host.setText("")
            assertTrue("Save must take the focus", save.requestFocus())
            save.performClick()
        }
        assertEquals("A form the check refuses starts no other", before, requests())
        assertEquals(listOf(context.getString(R.string.editor_error_missing)), toastTexts(a))
        onMain {
            assertNotNull("and leaves the editor open", root(a).findViewWithTag<View?>("overlay:editor"))
            assertSame("with the focus on Save", save, a.currentFocus)
            assertTrue(save.isEnabled)
        }
        // A name typed and the host put back: the check, once, however many times OK is pressed while it runs.
        onMain {
            host.setText("vpn.invalid")
            name.setText("Renamed")
            save.performClick()
            save.performClick()
        }
        assertEquals("A second OK while the check runs starts no other", before + 1, requests())
        checked()
        eventually("A form that passes is saved, and the editor closes") { overlayTag(a, "editor") == null }
        assertEquals("Renamed", saved().first().getString("name"))
        assertEquals("over the config it was opened on, and no other", listOf("Renamed", "Profile 1"), saved().map { it.getString("name") })
    }

    // On a phone a tap greys Save out until the check answers.
    @Test fun aTapOnSaveGreysItOutWhileItChecks() {
        val a = open()
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 0) }
        instrumentation.waitForIdleSync()
        instrumentation.setInTouchMode(true)
        eventually("The editor must be in touch mode") { inTouchMode(a) }
        val name = byKey(a, "editor:name") as EditText
        val save = byKey(a, "editor:save")
        onMain {
            name.setText("Tapped")
            save.performClick()
            assertFalse("Save must be greyed out during a check", save.isEnabled)
        }
        (field("validationWorker").get(a) as ExecutorService).submit {}.get(30, TimeUnit.SECONDS)
        eventually("The check closes the editor") { overlayTag(a, "editor") == null }
        assertEquals("Tapped", saved().first().getString("name"))
    }

    // The editor is the desktop's form, field by field and in its order (qml/CreateConfigOverlay.qml), with what the config holds in it: the
    // password as dots, the protocol and the split profile as select boxes, the three switches, the certificate in a box 70 high, and
    // Cancel and Save as the last row of what scrolls, not pinned under it. There is no box of TOML in it.
    @Test fun theEditorIsTheDesktopsFormWithTheConfigsValuesInIt() {
        val a = open()
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 0) }
        instrumentation.waitForIdleSync()
        val keys = listOf("editor:name", "editor:host", "editor:addresses", "editor:username", "editor:password", "editor:protocol", "editor:dns",
            "editor:sni", "editor:random", "editor:profile", "editor:ipv6", "editor:skip", "editor:antidpi", "editor:cert", "editor:certfile",
            "editor:cancel", "editor:save")
        onMain {
            val layer = root(a).findViewWithTag<View>("overlay:editor")
            assertNull("No box of TOML", root(a).findViewWithTag<View?>("editor:toml"))
            assertNull("and no switch that shows it", root(a).findViewWithTag<View?>("editor:show"))
            fun at(key: String) = IntArray(2).also { root(a).findViewWithTag<View>(key).getLocationInWindow(it) }
            fun width(key: String) = root(a).findViewWithTag<View>(key).width
            // Top to bottom as the desktop's: the name, the host, the addresses; then two to a line, the user name beside the password,
            // the protocol beside DNS, SNI beside the client random; the profile; the switches; the certificate; the buttons.
            for ((upper, lower) in listOf("editor:name" to "editor:host", "editor:host" to "editor:addresses", "editor:addresses" to "editor:username",
                    "editor:username" to "editor:protocol", "editor:protocol" to "editor:sni", "editor:sni" to "editor:profile",
                    "editor:profile" to "editor:ipv6", "editor:ipv6" to "editor:skip", "editor:skip" to "editor:antidpi", "editor:antidpi" to "editor:cert"))
                assertTrue("$upper is above $lower", at(upper)[1] < at(lower)[1])
            for ((left, right) in listOf("editor:username" to "editor:password", "editor:protocol" to "editor:dns", "editor:sni" to "editor:random")) {
                assertTrue("$left is beside $right", Math.abs(at(left)[1] - at(right)[1]) <= 2 && at(left)[0] < at(right)[0])
                assertTrue("and as wide as it, 10 apart", Math.abs(width(left) - width(right)) <= 2)
                assertTrue("10 apart: ${at(right)[0] - (at(left)[0] + width(left))}",
                    Math.abs(at(right)[0] - (at(left)[0] + width(left)) - Math.round(10 * a.resources.displayMetrics.density)) <= 2)
            }
            for (key in keys) assertNotNull("$key is in the editor", layer.findViewWithTag<View?>(key))
            // Prefilled from the config.
            fun text(key: String) = (root(a).findViewWithTag<View>(key) as EditText).text.toString()
            assertEquals("Profile 0", text("editor:name"))
            assertEquals("vpn.invalid", text("editor:host"))
            assertEquals("192.0.2.1:443", text("editor:addresses"))
            assertEquals("test", text("editor:username"))
            assertEquals("test", text("editor:password"))
            val password = root(a).findViewWithTag<EditText>("editor:password")
            assertTrue("The password is dots", password.transformationMethod is android.text.method.PasswordTransformationMethod)
            // The desktop's ●, one for each character, and never the one just typed shown as itself.
            assertEquals("each character is the desktop's ●", "\u25CF\u25CF\u25CF\u25CF",
                password.transformationMethod.getTransformation(password.text, password).toString())
            // A field of the desktop's keeps the caret at the end of what it was given.
            for (key in listOf("editor:name", "editor:host", "editor:addresses", "editor:username", "editor:password"))
                assertEquals("$key has its caret at the end", text(key).length, (root(a).findViewWithTag<View>(key) as EditText).selectionStart)
            // setInputType makes a password field monospace; the desktop's dots are in the font of its other fields: Inter Regular.
            assertSame("in the font of the other fields, not monospace", Fonts.regular(context), password.typeface)
            for (key in listOf("editor:name", "editor:host", "editor:addresses", "editor:username", "editor:dns", "editor:sni", "editor:random"))
                assertSame("$key is in Inter Regular", Fonts.regular(context), root(a).findViewWithTag<EditText>(key).typeface)
            for (key in listOf("editor:dns", "editor:sni", "editor:random", "editor:cert")) assertEquals("$key is empty: the config has none", "", text(key))
            assertFalse("IPv6 is off: the config says so", root(a).findViewWithTag<ToggleView>("editor:ipv6").isChecked)
            assertFalse(root(a).findViewWithTag<ToggleView>("editor:skip").isChecked)
            assertFalse(root(a).findViewWithTag<ToggleView>("editor:antidpi").isChecked)
            val protocol = root(a).findViewWithTag<ViewGroup>("editor:protocol")
            assertEquals("HTTP/2", (protocol.getChildAt(0) as TextView).text.toString())
            // The certificate box is 70 high and monospace.
            val cert = root(a).findViewWithTag<EditText>("editor:cert")
            assertEquals(Math.round(70 * a.resources.displayMetrics.density), cert.height)
            assertEquals(android.graphics.Typeface.MONOSPACE, cert.typeface)
            // Cancel and Save scroll with the form: in the ScrollView, in the form's last row, not under it.
            val save = root(a).findViewWithTag<View>("editor:save")
            val scroll = generateSequence(save as View?) { it.parent as? View }.filterIsInstance<ScrollView>().first()
            assertTrue("Save is in what scrolls", scroll.getChildAt(0).let { form -> generateSequence(save as View?) { it.parent as? View }.any { it === form } })
            // The labels are the desktop's.
            for (label in listOf(R.string.editor_name, R.string.editor_host, R.string.editor_addresses, R.string.editor_username, R.string.editor_password,
                    R.string.editor_protocol, R.string.editor_dns, R.string.editor_sni, R.string.editor_random, R.string.editor_split_profile,
                    R.string.editor_ipv6, R.string.editor_skip_verification, R.string.editor_anti_dpi, R.string.editor_certificate, R.string.editor_load_certificate))
                assertTrue("The editor says \"${context.getString(label)}\"", descendants(layer).filterIsInstance<TextView>().any { it.text.toString() == context.getString(label) })
            val placeholders = mapOf("editor:name" to context.getString(R.string.editor_name_placeholder), "editor:addresses" to "1.2.3.4:443", "editor:dns" to "1.1.1.1, 8.8.8.8")
            // A placeholder shows while the field is empty and has not the focus (Field.qml): a TV starts the editor on the name, whose
            // placeholder waits until the focus has gone on.
            for ((key, hint) in placeholders) {
                val field = root(a).findViewWithTag<EditText>(key)
                if (field.hasFocus()) assertNull("$key has the focus: its placeholder waits", field.hint)
                else assertEquals(hint, field.hint.toString())
            }
            val started = placeholders.keys.map { root(a).findViewWithTag<EditText>(it) }.firstOrNull { it.hasFocus() }
            started?.let { field ->
                assertTrue(root(a).findViewWithTag<View>("editor:host").requestFocus())
                assertEquals("and returns when the focus leaves", placeholders.getValue(field.tag as String), field.hint?.toString())
            }
        }
    }

    // A form that passes is written into the config, and what the form has no field for is left as the file had it: the tunnel's routes, the
    // rules it brings, every other key (the desktop's keepWhatTheFormDoesNotShow). A new config is made of the form alone.
    @Test fun theEditorWritesTheFormIntoTheConfigAndKeepsTheRest() {
        val a = open()
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 0) }
        instrumentation.waitForIdleSync()
        onMain {
            fun put(key: String, text: String) = root(a).findViewWithTag<EditText>(key).setText(text)
            put("editor:host", "new.example.com")
            put("editor:addresses", "5.6.7.8:443, 9.9.9.9:8443")
            put("editor:username", "alice")
            put("editor:password", "p\"w\\d")
            put("editor:dns", "1.1.1.1 tls://dns.example.com")
            put("editor:sni", "front.example.com")
            put("editor:random", "aabbcc/ff00ff")
            root(a).findViewWithTag<ToggleView>("editor:ipv6").performClick()
            root(a).findViewWithTag<ToggleView>("editor:antidpi").performClick()
        }
        click(a, "editor:save")
        (field("validationWorker").get(a) as ExecutorService).submit {}.get(30, TimeUnit.SECONDS)
        eventually("It is saved and the editor closes") { overlayTag(a, "editor") == null }
        val toml = saved().first().getString("toml")
        val parsed = org.tomlj.Toml.parse(toml)
        assertFalse(toml, parsed.hasErrors())
        assertEquals("new.example.com", parsed.getString("endpoint.hostname"))
        assertEquals(listOf("5.6.7.8:443", "9.9.9.9:8443"), parsed.getArray("endpoint.addresses")!!.let { x -> (0 until x.size()).map { x.getString(it) } })
        assertEquals("alice", parsed.getString("endpoint.username"))
        assertEquals("p\"w\\d", parsed.getString("endpoint.password"))
        assertEquals(listOf("1.1.1.1", "tls://dns.example.com"), parsed.getArray("endpoint.dns_upstreams")!!.let { x -> (0 until x.size()).map { x.getString(it) } })
        assertEquals("front.example.com", parsed.getString("endpoint.custom_sni"))
        assertEquals("aabbcc/ff00ff", parsed.getString("endpoint.client_random"))
        assertEquals("IPv6 was off and is on", true, parsed.getBoolean("endpoint.has_ipv6"))
        assertEquals(true, parsed.getBoolean("endpoint.anti_dpi"))
        assertEquals(false, parsed.getBoolean("endpoint.skip_verification"))
        assertEquals("The protocol is as it was", "http2", parsed.getString("endpoint.upstream_protocol"))
        // What the form does not show is the file's: the routes of the tunnel, and the rules.
        assertEquals(1350L, parsed.getLong("listener.tun.mtu_size"))
        assertEquals("vpn_mode = \"general\"", toml.lines().first { it.startsWith("vpn_mode") })
        assertTrue(toml, toml.contains("exclusions = []"))
        assertEquals("The other config is as it was", source, saved()[1].getString("toml"))
        assertEquals("and the name too", "Profile 0", saved().first().getString("name"))
    }

    @Test fun aNewConfigIsWhatTheFormHoldsAndTheDesktopsChecksRefuseWhatIsNot() {
        val a = open()
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, -1) }
        instrumentation.waitForIdleSync()
        fun typeInto(key: String, text: String) = onMain { root(a).findViewWithTag<EditText>(key).setText(text) }
        fun save() = click(a, "editor:save")
        fun said(): List<String> = toastTexts(a)
        val requestsBefore = onMain { field("validationRequest").getLong(a) }
        save()
        assertEquals("An empty form is missing what a server needs", listOf(context.getString(R.string.editor_error_missing)), said())
        typeInto("editor:host", "vpn.example.com"); typeInto("editor:addresses", "1.2.3.4"); typeInto("editor:username", "u"); typeInto("editor:password", "p")
        save()
        assertEquals(listOf(context.getString(R.string.editor_error_address)), said())
        typeInto("editor:addresses", "1.2.3.4:443"); typeInto("editor:dns", "dns.example.com")
        save()
        assertEquals(listOf(context.getString(R.string.editor_error_dns)), said())
        typeInto("editor:dns", ""); typeInto("editor:random", "xyz")
        save()
        assertEquals(listOf(context.getString(R.string.editor_error_random)), said())
        typeInto("editor:random", ""); typeInto("editor:cert", "not a certificate")
        save()
        assertEquals(listOf(context.getString(R.string.editor_error_certificate)), said())
        assertEquals("A refusal starts no check", requestsBefore, onMain { field("validationRequest").getLong(a) })
        assertNotNull("and the editor stays", overlayTag(a, "editor"))
        typeInto("editor:cert", "")
        // No name: the host's, as on the desktop.
        save()
        (field("validationWorker").get(a) as ExecutorService).submit {}.get(30, TimeUnit.SECONDS)
        eventually("It is saved") { overlayTag(a, "editor") == null }
        assertEquals(3, saved().size)
        assertEquals("vpn.example.com", saved().last().getString("name"))
        val parsed = org.tomlj.Toml.parse(saved().last().getString("toml"))
        assertEquals("vpn.example.com", parsed.getString("endpoint.hostname"))
        assertEquals("IPv6 is on by default, as on the desktop", true, parsed.getBoolean("endpoint.has_ipv6"))
        assertEquals("p", parsed.getString("endpoint.password"))
        assertNotNull("A tunnel to put it through", parsed.getTable("listener.tun"))
    }

    // A typed name over 50 characters is refused with the desktop's words (the name is a file's name there); a host standing in for a name
    // that was left empty is cut to 50, as a name nobody typed is.
    @Test fun aTypedNameOverFiftyCharactersIsRefusedAndAHostStandingInForOneIsClipped() {
        val a = open()
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, -1) }
        instrumentation.waitForIdleSync()
        fun typeInto(key: String, text: String) = onMain { root(a).findViewWithTag<EditText>(key).setText(text) }
        typeInto("editor:host", "a".repeat(60) + ".example.com"); typeInto("editor:addresses", "1.2.3.4:443")
        typeInto("editor:username", "u"); typeInto("editor:password", "p")
        typeInto("editor:name", "n".repeat(51))
        val before = saved().size
        click(a, "editor:save")
        assertEquals(listOf(context.getString(R.string.editor_error_name_too_long, 50)), toastTexts(a))
        assertNotNull("A refusal keeps the editor", overlayTag(a, "editor"))
        typeInto("editor:name", "n".repeat(50))
        click(a, "editor:save")
        (field("validationWorker").get(a) as ExecutorService).submit {}.get(30, TimeUnit.SECONDS)
        eventually("Fifty is allowed") { overlayTag(a, "editor") == null }
        assertEquals("n".repeat(50), saved().last().getString("name"))
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, -1) }
        instrumentation.waitForIdleSync()
        typeInto("editor:host", "a".repeat(60) + ".example.com"); typeInto("editor:addresses", "1.2.3.4:443")
        typeInto("editor:username", "u"); typeInto("editor:password", "p")
        click(a, "editor:save")
        (field("validationWorker").get(a) as ExecutorService).submit {}.get(30, TimeUnit.SECONDS)
        eventually("A host is cut, not refused") { overlayTag(a, "editor") == null }
        assertEquals(before + 2, saved().size)
        assertEquals("a".repeat(50), saved().last().getString("name"))
    }

    // A file whose text the editor cannot carry over whole (here a key with an escape in its name, which its line reader does not read) is
    // not rewritten from the form behind the user's back: the save is refused, with a message, and the config is as it was.
    @Test fun aConfigTheEditorCannotWriteBackWholeIsLeftAsItIsWithAMessage() {
        val unreadable = "\"a\\\"b\" = 1\n" + source
        val profiles = org.json.JSONArray().put(org.json.JSONObject().put("name", "Odd").put("toml", unreadable))
        assertTrue(ConfigStore.write(prefs, profiles.toString()))
        val a = open()
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 0) }
        instrumentation.waitForIdleSync()
        onMain { root(a).findViewWithTag<EditText>("editor:host").setText("other.example.com") }
        click(a, "editor:save")
        assertEquals(listOf(context.getString(R.string.editor_error_unwritable)), toastTexts(a))
        assertNotNull("The editor stays", overlayTag(a, "editor"))
        assertEquals("and the config is as it was", unreadable, saved().first().getString("toml"))
    }

    // "Load from file…" puts what a file holds in the certificate box; a file that is empty or too large changes nothing and says why. The
    // file is read on the worker that writes exports (a document provider can keep a reader waiting), and the box is filled on the main thread.
    @Test fun aCertificateFileFillsTheCertificateBoxAndAnEmptyOneChangesNothing() {
        val a = open()
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, -1) }
        instrumentation.waitForIdleSync()
        val dir = java.io.File(context.cacheDir, "certificate-test").apply { mkdirs() }
        val pem = "-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n"
        val full = java.io.File(dir, "full.pem").apply { writeText(pem) }
        val empty = java.io.File(dir, "empty.pem").apply { writeText("  \n") }
        val read = method("readCertificate", android.net.Uri::class.java, EditText::class.java)
        val box = byKey(a, "editor:cert") as EditText
        fun load(file: java.io.File) {
            onMain { read.invoke(a, android.net.Uri.fromFile(file), box) }
            (field("ioWorker").get(a) as ExecutorService).submit {}.get(30, TimeUnit.SECONDS)
            instrumentation.waitForIdleSync()
        }
        try {
            load(full)
            assertEquals(pem, onMain { box.text.toString() })
            assertEquals("Nothing is said of a file that is read", emptyList<String>(), toastTexts(a))
            load(empty)
            assertEquals("An empty file wipes nothing", pem, onMain { box.text.toString() })
            // and says so, in the desktop's words (Backend::readTextFile)
            assertEquals(listOf(context.getString(R.string.editor_file_empty)), toastTexts(a))
            // The desktop's limit is 1 MB, and its words say that.
            val large = java.io.File(dir, "large.pem").apply { writeBytes(ByteArray(1024 * 1024 + 1) { 97 }) }
            load(large)
            assertEquals("A file over 1 MB changes nothing", pem, onMain { box.text.toString() })
            assertEquals(listOf(context.getString(R.string.editor_file_too_large)), toastTexts(a))
            // A file that is not there is no crash either.
            load(java.io.File(dir, "missing.pem"))
            assertEquals("A file that cannot be opened changes nothing", pem, onMain { box.text.toString() })
            assertEquals(listOf(context.getString(R.string.text_could_not_open_file)), toastTexts(a))
            // 260 KB, which was refused at 256: a certificate chain as a file has it, lines of 64.
            val big = java.io.File(dir, "big.pem").apply { writeText(("b".repeat(63) + "\n").repeat(260 * 1024 / 64)) }
            assertEquals(260 * 1024L, big.length())
            load(big)
            assertEquals("and one of 260 KB, which was refused at 256, is read", 260 * 1024, onMain { box.text.length })
        } finally {
            dir.deleteRecursively()
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
            // While it does nothing it has no pointer look, as on the desktop, but the focus that stays on it must show: the hover colour,
            // underlined. A focus that looks like no focus is a lost one.
            onMain {
                assertTrue("$state: the focus must show as an underline", action.paintFlags and android.graphics.Paint.UNDERLINE_TEXT_FLAG != 0)
                assertEquals("$state: in the colour of the focus", theme(a).accent, action.currentTextColor)
            }
        }
        updater("state", AndroidUpdater.State.IDLE)
        onMain { method("refreshUpdateControls").invoke(a) }
        assertTrue(onMain { action.isClickable })
        assertEquals("settings:update", focusedKey(a))
    }

    // An overlay of the app's own shows the focus as the desktop's controls do, in the theme's colours: no foreground, a
    // background that changes with the state, each control's hover look with the focus (the darker red for the red button, the
    // darker accent for Save), an accent border for a field, a select box or a box of text; and it is a view of the activity's
    // window, found by its tag.
    private class Shown(val what: String, val tag: String, val start: () -> Any?)

    @Test fun overlayButtonsShowTheDesktopFocusLook() {
        assertTrue("Grant REQUEST_INSTALL_PACKAGES appop before device tests", context.packageManager.canRequestPackageInstalls())
        // With the VPN on, Install update warns first.
        serviceState(2)
        val a = open()
        leaveTouchMode(a)
        val overlays = listOf(
            Shown("Delete", "confirm") { method("confirmRemove", Int::class.javaPrimitiveType).invoke(a, 0) },
            Shown("the editor", "editor") { method("editConfig", Int::class.javaPrimitiveType).invoke(a, -1) },
            Shown("the app picker", "picker") { method("appPicker").invoke(a) },
            Shown("Install update", "confirm") { method("installUpdate").invoke(a) },
            Shown("Receive from phone", "receive") { method("receiveFromPhone").invoke(a) })
        for (shown in overlays) {
            onMain { shown.start() }
            instrumentation.waitForIdleSync()
            if (shown.tag == "picker") eventually("The apps must be listed") { onMain { root(a).findViewWithTag<View?>("picker:status") == null } }
            onMain {
                val layer = root(a).findViewWithTag<View?>("overlay:${shown.tag}")
                assertNotNull("${shown.what} must be an overlay of the window", layer)
                val controls = descendants(layer!!).filter { it.isShown && it.isFocusable }.toList()
                assertTrue("${shown.what} shows no controls", controls.isNotEmpty())
                for (view in controls) {
                    val name = "${view.javaClass.simpleName} \"${view.tag}\" in ${shown.what}"
                    assertTrue("$name has no key", view.tag is String)
                    assertFocusLook(view, name, theme(a))
                }
            }
            closeWithBack(a, shown.tag, shown.what)
        }
    }

    // The ← of a card is a focus stop that shows its focus the way the desktop's shows the pointer: it turns from textDim to the text colour. It
    // is a control of its own, so its tint follows its own states and not the header's, which is never focused, pressed or hovered. Where
    // another control takes the focus first (the editor's name, the picker's search) the arrow is seen at rest in textDim, and then
    // focused. Receive from phone has no other control, so a remote's focus is on its arrow from the moment the card opens, and it cannot
    // be seen at rest: there the arrow has the focus on opening, and is in the text colour.
    @Test fun theBackArrowOfACardTurnsToTheTextColourWithTheFocus() {
        val a = open()
        leaveTouchMode(a)
        val cards = listOf(
            Shown("the editor", "editor") { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 0) },
            Shown("the app picker", "picker") { method("appPicker").invoke(a) },
            Shown("Receive from phone", "receive") { method("receiveFromPhone").invoke(a) })
        // The card whose only stop is its arrow, and which a remote therefore starts on.
        val opensOnTheArrow = setOf("receive")
        for (card in cards) {
            onMain { card.start() }
            instrumentation.waitForIdleSync()
            val key = "${card.tag}:back"
            val theme = theme(a)
            if (card.tag in opensOnTheArrow) {
                // The focus is given to a card's first stop once it is up, not as it is made.
                eventually("${card.what}: the arrow has the focus when the card opens") {
                    onMain { root(a).findViewWithTag<ImageView?>(key)?.isFocused == true }
                }
            } else {
                onMain {
                    val arrow = root(a).findViewWithTag<ImageView>(key)
                    assertNotNull("${card.what} has a ← with the key $key", arrow)
                    assertFalse("${card.what}: the arrow does not rest focused", arrow.isFocused)
                    assertEquals("${card.what}: the arrow rests in textDim", theme.textDim, arrow.imageTintList!!.getColorForState(arrow.drawableState, 0))
                }
                focusOn(a, key)
            }
            onMain {
                val arrow = root(a).findViewWithTag<ImageView>(key)
                assertNotNull("${card.what} has a ← with the key $key", arrow)
                assertTrue("${card.what}: the arrow has the focus", arrow.isFocused)
                assertTrue("${card.what}: and its state says so", arrow.drawableState.contains(android.R.attr.state_focused))
                assertEquals("${card.what}: the focused arrow is in the text colour", theme.text, arrow.imageTintList!!.getColorForState(arrow.drawableState, 0))
            }
            closeWithBack(a, card.tag, card.what)
        }
    }

    // A remote reaches every control of an overlay with the arrows, from the one that has the focus first, and none leaves it.
    @Test fun theArrowsReachEveryControlOfAnOverlay() {
        val a = open()
        leaveTouchMode(a)
        val overlays = listOf(
            Shown("the editor", "editor") { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 0) },
            Shown("the app picker", "picker") { method("appPicker").invoke(a) },
            Shown("a confirmation with an alternative", "confirm") {
                method("showConfirm", CharSequence::class.java, CharSequence::class.java, CharSequence::class.java,
                    Runnable::class.java, Runnable::class.java, Runnable::class.java)
                    .invoke(a, "Replace it?", "Replace", "Add copy", null, null, null)
            })
        for (shown in overlays) {
            onMain { shown.start() }
            instrumentation.waitForIdleSync()
            if (shown.tag == "picker") eventually("The apps must be listed") { onMain { root(a).findViewWithTag<View?>("picker:status") == null } }
            // The arrows look for the next control from where the controls are laid out, and in a window a remote drives (outside touch
            // mode, where only a field is a stop): the search is made when both are so.
            leaveTouchMode(a)
            eventually("${shown.what} is laid out") {
                onMain {
                    val laid = root(a).findViewWithTag<ViewGroup?>("overlay:${shown.tag}")
                    laid != null && laid.isLaidOut && descendants(laid).filter { it.isShown && it.isFocusable }.all { it.width > 0 && it.height > 0 }
                }
            }
            onMain {
                val layer = root(a).findViewWithTag<ViewGroup>("overlay:${shown.tag}")
                val keys = descendants(layer).filter { it.isShown && it.isFocusable }.mapNotNull { it.tag as? String }.toSet()
                assertTrue("${shown.what} has controls", keys.isNotEmpty())
                val seen = HashSet<View>()
                // From the control that has the focus, if it is one of the overlay's.
                val focus = a.currentFocus?.takeIf { generateSequence(it as View?) { v -> v.parent as? View }.any { v -> v === layer } }
                val queue = ArrayDeque<View>().apply { add(focus ?: layer.findViewWithTag<View>(keys.first())) }
                while (queue.isNotEmpty()) {
                    val view = queue.removeFirst()
                    if (!seen.add(view)) continue
                    for (direction in listOf(View.FOCUS_UP, View.FOCUS_DOWN, View.FOCUS_LEFT, View.FOCUS_RIGHT)) {
                        val next = FocusFinder.getInstance().findNextFocus(layer, view, direction)
                        if (next != null) {
                            assertTrue("An arrow leaves ${shown.what} for ${next.tag}", (next.tag as? String) in keys)
                            queue.add(next)
                        }
                    }
                }
                // Where each control is and whether the window is outside touch mode, so that a failure says what the search had to go on.
                val where = keys.joinToString("; ") { key ->
                    val v = layer.findViewWithTag<View>(key)
                    "$key at ${v.left},${v.top},${v.right},${v.bottom} in ${(v.parent as View).left},${(v.parent as View).top}"
                } + "; touch mode ${a.window.decorView.isInTouchMode}; the focus is on ${a.currentFocus?.tag}"
                assertEquals("Controls of ${shown.what} that the arrows never reach ($where)", emptySet<String>(),
                    keys - seen.mapNotNull { it.tag as? String }.toSet())
            }
            closeWithBack(a, shown.tag, shown.what)
        }
    }

    // Receive from phone. The link is DeepLinkImportTest's V2_PLAIN: vpn.example.com,
    // with no name of its own.
    private val link = "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQz"
    private fun receiver(a: MainActivity) = onMain { field("linkReceiver").get(a) as LinkReceiver? }
    private fun saved() = org.json.JSONArray(ConfigStore.read(prefs)).let { list -> (0 until list.length()).map { list.getJSONObject(it) } }
    // As a phone's browser sends it, over a raw socket: this test runs in the app's
    // process, where the default network security config refuses cleartext HTTP to
    // HttpURLConnection. request() is given the Host and the page's path.
    private fun http(url: String, request: (host: String, path: String) -> String): String =
        httpBytes(url) { host, path -> request(host, path).toByteArray() }
    private fun httpBytes(url: String, request: (host: String, path: String) -> ByteArray): String {
        val page = URI(url)
        return Socket(page.host, page.port).use { socket ->
            socket.soTimeout = 10_000
            socket.getOutputStream().write(request("${page.host}:${page.port}", page.rawPath))
            socket.getInputStream().readBytes().toString(Charsets.UTF_8)
        }
    }
    // The urlencoded form the page sent before it took files, which the TV still takes.
    private fun formPost(host: String, path: String, text: String): String {
        val body = "link=" + URLEncoder.encode(text, "UTF-8")
        return "POST $path HTTP/1.1\r\nHost: $host\r\nContent-Type: application/x-www-form-urlencoded\r\n" +
            "Content-Length: ${body.length}\r\n\r\n$body"
    }
    // The page's own form as Chrome sends it, with a file chosen and nothing pasted
    // unless text is given: the text field, then the file field with its name, as the
    // browser writes it, and its content. With no file chosen, both are empty.
    private fun filePost(host: String, path: String, fileName: String, content: ByteArray, text: String = ""): ByteArray {
        val boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW"
        val body = ("--$boundary\r\nContent-Disposition: form-data; name=\"link\"\r\n\r\n$text\r\n" +
            "--$boundary\r\nContent-Disposition: form-data; name=\"file\"; filename=\"$fileName\"\r\n" +
            "Content-Type: application/octet-stream\r\n\r\n").toByteArray() + content + "\r\n--$boundary--\r\n".toByteArray()
        return ("POST $path HTTP/1.1\r\nHost: $host\r\nContent-Type: multipart/form-data; boundary=$boundary\r\n" +
            "Content-Length: ${body.size}\r\n\r\n").toByteArray() + body
    }
    // stop() closes the port, but the receiver's thread, waiting in accept(), lets go
    // of it only once it wakes, a moment later on a slow emulator. Until then a
    // connection is taken or reset rather than refused. With a connect timeout: a
    // port that no one takes connections on any more fills its queue, and a connect
    // would then wait for minutes.
    private fun assertClosed(url: String) = eventually("$url must be closed", 2_000) {
        val page = URI(url)
        try { Socket().use { it.connect(java.net.InetSocketAddress(page.host, page.port), 500) }; false }
        catch (_: ConnectException) { true }
        catch (_: java.io.IOException) { false }
    }

    // A TOML with a server of its own and no name, as a phone pastes one: it is named for where it came from.
    private fun tomlFor(server: String) = source.replace("vpn.invalid", server)
    private fun sendText(url: String, text: String): String = http(url) { h, path -> formPost(h, path, text) }
    // The page that says a text was sent, which is the form again for the next.
    private fun assertSent(answer: String) {
        assertTrue(answer, answer.startsWith("HTTP/1.1 200 ") &&
            answer.contains(LinkReceiver.escape(context.getString(R.string.receive_page_sent))) && answer.contains("<form"))
    }
    // A send to a wrong code, one letter off.
    private fun sendWrongCode(url: String): String = http(url) { h, path -> formPost(h, path.dropLast(1) + (if (path.endsWith("Z")) "Y" else "Z"), link) }
    // The texts the receiver took that the TV has not answered, which the receiver turns sends away by, and those that wait for their turn.
    private fun placesHeld(a: MainActivity): Int = onMain { (field("receivedTexts").get(a) as ReceivedQueue).waiting().get() }
    private fun waitingForTheirTurn(a: MainActivity): Int = onMain { (field("receivedTexts").get(a) as ReceivedQueue).queued() }
    private fun keepScreenOn(a: MainActivity) = onMain { a.window.attributes.flags and WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON != 0 }
    private fun receivedLine(a: MainActivity): String? = onMain { root(a).findViewWithTag<TextView?>("receive:count")?.text?.toString() }
    private fun openReceiveCard(a: MainActivity): LinkReceiver {
        onMain { method("receiveFromPhone").invoke(a) }
        instrumentation.waitForIdleSync()
        val opened = receiver(a)
        assertNotNull("The emulator must have a private-network IPv4 address on Wi-Fi or Ethernet", opened)
        return opened!!
    }
    // The question about the text from this server is up, whatever number of validations came before it.
    private fun askedAbout(a: MainActivity, server: String, timeoutMs: Long = 30_000) =
        eventually("The confirmation must be about $server", timeoutMs) { confirmMessage(a)?.contains(server) == true }
    // Cancels the question that is up, and waits for it to go or for the next one to take its place.
    private fun cancelQuestion(a: MainActivity, about: String) {
        click(a, "confirm:cancel")
        eventually("The question about $about must go") { confirmMessage(a)?.contains(about) != true }
    }

    // The add menu under the + has the desktop's rows, in its order and its words, with Receive from phone first on a TV
    // and only there; each row runs its action.
    @Test fun receiveFromPhoneIsOfferedFirstOnATvAndNowhereElse() {
        val a = open()
        render(a, 1)
        click(a, "configs:add")
        instrumentation.waitForIdleSync()
        val rows = onMain {
            descendants(root(a).findViewWithTag<View>("overlay:addmenu")).filter { (it.tag as? String)?.startsWith("add:") == true }
                .map { it.tag as String to descendants(it).filterIsInstance<TextView>().first().text.toString() }.toList()
        }
        val others = listOf("add:paste" to context.getString(R.string.text_paste_from_clipboard),
            "add:file" to context.getString(R.string.add_from_file), "add:create" to context.getString(R.string.add_create_new))
        if (!isTv) {
            assertEquals(others, rows)
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
            eventually("Back closes the menu") { overlayTag(a, "addmenu") == null }
            // Create new… opens the editor, as the desktop's does.
            click(a, "configs:add")
            click(a, "add:create")
            eventually("Create new… opens the editor") { overlayTag(a, "editor") != null }
            assertTrue(shows(a, context.getString(R.string.text_new_configuration)))
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
            return
        }
        assertEquals(listOf("add:receive" to context.getString(R.string.text_receive_from_phone)) + others, rows)
        // Each item still runs its own action, now that the TV's comes first.
        click(a, "add:receive")
        eventually("Receive from phone must open the receiver") { receiver(a) != null }
        assertNotNull(overlayTag(a, "receive"))
        onMain { method("stopReceiver").invoke(a) }
        eventually("and stopping it closes the card") { overlayTag(a, "receive") == null }
    }

    // Both legs: what a phone's browser does, end to end, through the emulator's own network address. Several texts
    // in one session: the port and the card stay open while the TV asks about each, which it does in turn, and nothing is
    // added before it says yes.
    @SdkSuppress(minSdkVersion = 29)
    @Test fun aLinkFromAPhoneIsAddedOnlyOnceTheTvConfirmsIt() {
        val a = open()
        render(a, 1)
        val opened = openReceiveCard(a)
        val url = opened.url()
        val host = URI(url).host
        assertTrue("$url must be on the network a phone is on", Regex("http://(10|172|192)\\.[0-9.]+:[0-9]+/[0-9A-Z]{4}-[0-9A-Z]{4}").matches(url))
        onMain {
            val card = root(a).findViewWithTag<View>("overlay:receive")
            assertTrue("The address is on the card, to type", descendants(card).any { view -> view is TextView && view.text.toString() == url })
            // No button: the arrow closes it. The hint is two short sentences, centred as the address is, with no warning in it, and the
            // card holds a line for the count from the start, so that the first text does not make it grow.
            assertEquals("The card has no button", 0, descendants(card).filterIsInstance<Button>().count())
            assertNull("and no Close", card.findViewWithTag<View?>("receive:close"))
            assertFocusLook(card.findViewWithTag<View>("receive:back"), "The back arrow", theme(a))
            val hint = descendants(card).filterIsInstance<TextView>().single { it.text.toString() == context.getString(R.string.text_receive_hint) }
            assertEquals("The hint is centred", Gravity.CENTER_HORIZONTAL, hint.gravity and Gravity.HORIZONTAL_GRAVITY_MASK)
            val addressView = descendants(card).filterIsInstance<TextView>().single { it.text.toString() == url }
            assertEquals("as the address is", Gravity.CENTER_HORIZONTAL, addressView.gravity and Gravity.HORIZONTAL_GRAVITY_MASK)
            assertEquals("The count is centred too", Gravity.CENTER, card.findViewWithTag<TextView>("receive:count").gravity)
            assertTrue("The count's line is there before anything has come", card.findViewWithTag<TextView>("receive:count").height > 0)
            assertTrue("The screen stays on while the card is open", a.window.attributes.flags and WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON != 0)
            // The QR code: square, black on white, a whole number of pixels a module,
            // with the quiet zone and the top left finder pattern where they belong.
            val code = descendants(card).filterIsInstance<ImageView>().single { it.drawable is BitmapDrawable }
            assertEquals("on its own white tile, in either theme", Color.WHITE, ((code.parent as View).background as GradientDrawable).color?.defaultColor)
            val qr = (code.drawable as BitmapDrawable).bitmap
            val modules = QrCode.encodeText(url, QrCode.Ecc.MEDIUM).size + 8
            val scale = qr.width / modules
            assertTrue(scale >= 2)
            assertEquals(modules * scale, qr.width)
            assertEquals(qr.width, qr.height)
            assertEquals(Color.WHITE, qr.getPixel(0, 0))
            assertEquals(Color.BLACK, qr.getPixel(4 * scale, 4 * scale))
        }
        assertEquals("Nothing has come yet, so the card has no count", "", receivedLine(a))
        // Any path shows the page, in the TV's language.
        val page = http(url) { h, _ -> "GET /favicon.ico HTTP/1.1\r\nHost: $h\r\n\r\n" }
        assertTrue(page, page.startsWith("HTTP/1.1 200 ") && page.contains(context.getString(R.string.receive_page_send)))
        // A wrong code is turned away, and the TV goes on waiting.
        val wrong = sendWrongCode(url)
        assertTrue(wrong, wrong.startsWith("HTTP/1.1 404 "))
        assertSame(opened, receiver(a))
        assertSent(http(url) { h, path -> formPost(h, path, link) })
        // And it goes on: the port stays open, and the card with it, and the screen stays on, while the TV asks.
        assertSame(opened, receiver(a))
        assertNotNull("The card stays up under the question", overlayTag(a, "receive"))
        assertTrue(keepScreenOn(a))
        // The card's line is set on the main thread, a moment after the answer was written: the receiver hands the text over once the
        // connection is closed.
        eventually("The card counts the first") { receivedLine(a) == context.getString(R.string.text_receive_received, 1) }
        // The TV shows the server and the sender's address, and asks: the desktop's question, the server on its second line
        // and where it was sent from on its third.
        val from = context.getString(R.string.text_nsent_from_s, host).trim()
        eventually("The confirmation must name the server and \"$from\"", 30_000) {
            confirmMessage(a)?.let { it.contains("vpn.example.com") && it.contains(from) } == true
        }
        val lines = confirmMessage(a)!!.split("\n")
        assertTrue(lines[0], lines[0].startsWith(context.getString(R.string.import_question_link)))
        assertEquals(context.getString(R.string.import_server, "vpn.example.com"), lines[1])
        assertEquals(from, lines[2])
        assertEquals("Nothing is added before the TV confirms", 2, saved().size)
        // A second text comes while the first is asked about: its page says it was sent, the question on the screen is still
        // the first, and still nothing is added.
        assertSent(sendText(url, tomlFor("second.example.com")))
        eventually("The card counts both") { receivedLine(a) == context.getString(R.string.text_receive_received, 2) }
        assertEquals(2, placesHeld(a))
        assertEquals("The second waits for its turn", 1, waitingForTheirTurn(a))
        assertTrue(confirmMessage(a)!!.contains("vpn.example.com") && !confirmMessage(a)!!.contains("second.example.com"))
        assertEquals("Nothing is added before the TV confirms", 2, saved().size)
        answerYes(a)
        eventually("Import must save the first") { saved().size == 3 }
        assertEquals("vpn.example.com", saved().last().getString("name"))
        assertTrue(saved().last().getString("toml").contains("hostname = \"vpn.example.com\""))
        // The second is asked about in turn, with where it was sent from, and cancelled it is not added. It is added under the name of where it
        // came from, as the third is below; as on the desktop the question does not say the name of what is new, which whoever sent it controls.
        val named = context.getString(R.string.text_from_another_device)
        eventually("The second must be asked about, with \"$from\"", 30_000) {
            confirmMessage(a)?.let { it.contains("second.example.com") && it.contains(from) } == true
        }
        assertFalse("The name it would get is not in the question", confirmMessage(a)!!.contains(named))
        assertEquals(3, saved().size)
        cancelQuestion(a, "second.example.com")
        assertEquals("Cancelled, it is not added", 3, saved().size)
        eventually("Both are answered, so no place is held") { placesHeld(a) == 0 }
        assertSame("Cancelling a question does not close the port", opened, receiver(a))
        // The same page takes a third, the form it was sent from, and it is added once confirmed. Its toast is the last one.
        assertSent(sendText(url, tomlFor("third.example.com")))
        askedAbout(a, "third.example.com")
        answerYes(a)
        eventually("Import must save the third") { saved().size == 4 }
        assertEquals(named, saved().last().getString("name"))
        assertTrue(saved().last().getString("toml").contains("hostname = \"third.example.com\""))
        // The toast says what was added; the page stays where it was.
        assertEquals(listOf(context.getString(R.string.config_added, named)), toastTexts(a))
        assertEquals("An import does not change the page", 1, onMain { field("page").getInt(a) })
        assertEquals(0, placesHeld(a))
        // The arrow ends it: the port, the card and the screen's staying on.
        click(a, "receive:back")
        eventually("The arrow must close the card") { overlayTag(a, "receive") == null }
        assertNull(receiver(a))
        assertClosed(url)
        assertFalse("and the screen may go off again", keepScreenOn(a))
    }

    // Both legs: ten waiting is the most. The eleventh send, and any after it, gets the page that says to wait, which is no
    // strike; the texts come to the TV in the order they were sent; and a text answered makes room for the next.
    @SdkSuppress(minSdkVersion = 29)
    @Test fun tenWaitingIsTheMostAndAnAnswerMakesRoomForAnother() {
        val a = open()
        render(a, 1)
        val opened = openReceiveCard(a)
        val url = opened.url()
        for (i in 0 until LinkReceiver.MAX_WAITING) assertSent(sendText(url, tomlFor("host$i.example.com")))
        askedAbout(a, "host0.example.com")
        assertEquals(LinkReceiver.MAX_WAITING, placesHeld(a))
        eventually("The card counts them all") { receivedLine(a) == context.getString(R.string.text_receive_received, LinkReceiver.MAX_WAITING) }
        // More turned away than there are wrong codes to stop it: the TV still listens, and takes nothing.
        repeat(LinkReceiver.MAX_WRONG_CODES + 1) {
            val busy = sendText(url, tomlFor("late.example.com"))
            assertTrue(busy, busy.startsWith("HTTP/1.1 503 ") && busy.contains(LinkReceiver.escape(context.getString(R.string.receive_page_busy))) &&
                busy.contains("<form"))
        }
        assertSame(opened, receiver(a))
        assertEquals(LinkReceiver.MAX_WAITING, placesHeld(a))
        assertEquals("Those turned away are not counted", context.getString(R.string.text_receive_received, LinkReceiver.MAX_WAITING), receivedLine(a))
        assertEquals("Nothing is added", 2, saved().size)
        // One answered, one place: the next text is taken, and the one after is turned away again.
        cancelQuestion(a, "host0.example.com")
        eventually("The next is asked about", 30_000) { confirmMessage(a)?.contains("host1.example.com") == true }
        eventually("A place is free") { placesHeld(a) == LinkReceiver.MAX_WAITING - 1 }
        assertSent(sendText(url, tomlFor("late.example.com")))
        assertTrue(sendText(url, tomlFor("later.example.com")).startsWith("HTTP/1.1 503 "))
        // In turn, in the order they came.
        for (i in 1..3) {
            askedAbout(a, "host$i.example.com")
            cancelQuestion(a, "host$i.example.com")
        }
        assertEquals("Not one was added", 2, saved().size)
    }

    // Both legs: a text that came is asked about whatever else the TV is doing while it is checked. Its check is queued behind another here, as
    // it is on a slow device; meanwhile the list changes (a config is selected and saved) and a newer check begins, each of which makes the
    // checks about the config in use stale. A check of a text to add is not one of those: it was dropped as a refusal, and the text never asked about.
    @SdkSuppress(minSdkVersion = 29)
    @Test fun aTextWaitingForItsCheckIsStillAskedAboutWhenTheListChangesMeanwhile() {
        val a = open()
        render(a, 1)
        val queue = field("receivedTexts").get(a) as ReceivedQueue
        val gate = java.util.concurrent.CountDownLatch(1)
        (field("validationWorker").get(a) as ExecutorService).execute { gate.await() }
        try {
            onMain {
                queue.waiting().addAndGet(1)
                queue.add(tomlFor("late.example.com"), null, "192.0.2.9")
                method("processReceived").invoke(a)
            }
            val before = onMain { field("validationRequest").getLong(a) }
            click(a, "config:1:select")
            assertEquals("Selecting a config saves the list, which makes the checks about the config in use stale", 1, onMain { field("activeIndex").getInt(a) })
            assertTrue(onMain { field("validationRequest").getLong(a) } > before)
            // And a newer check about the config in use, as a tap on the logo makes (which is not made here: it would start the VPN).
            onMain { field("validationRequest").setLong(a, field("validationRequest").getLong(a) + 1) }
            assertEquals("Nothing is asked about yet: the check is still waiting its turn", null, confirmMessage(a))
        } finally {
            gate.countDown()
        }
        askedAbout(a, "late.example.com")
        cancelQuestion(a, "late.example.com")
        eventually("Answered, it holds no place") { placesHeld(a) == 0 }
        assertEquals("and nothing was added", 2, saved().size)
    }

    // Both legs: closing the card closes the port, but what came is still the TV's to answer; leaving the app drops what waits for
    // its turn, and the question that is up stays. A wrong code counts against all the sends of the session.
    @SdkSuppress(minSdkVersion = 29)
    @Test fun closingTheCardKeepsWhatCameAndLeavingTheAppDropsWhatWaits() {
        val a = open()
        render(a, 1)
        val first = openReceiveCard(a).url()
        for (i in 1..3) assertSent(sendText(first, tomlFor("kept$i.example.com")))
        askedAbout(a, "kept1.example.com")
        assertEquals(3, placesHeld(a))
        click(a, "receive:back")
        eventually("The arrow must close the card") { overlayTag(a, "receive") == null }
        assertNull("and the port", receiver(a))
        assertClosed(first)
        assertEquals("What came is still the TV's to answer", 3, placesHeld(a))
        assertTrue(confirmMessage(a)!!.contains("kept1.example.com"))
        cancelQuestion(a, "kept1.example.com")
        askedAbout(a, "kept2.example.com")
        cancelQuestion(a, "kept2.example.com")
        askedAbout(a, "kept3.example.com")
        assertEquals(1, placesHeld(a))
        // A new session, while the last question is up: two more come and wait.
        val second = openReceiveCard(a).url()
        assertSent(sendText(second, tomlFor("left1.example.com")))
        assertSent(sendText(second, tomlFor("left2.example.com")))
        eventually("Two wait for their turn") { waitingForTheirTurn(a) == 2 }
        assertEquals(3, placesHeld(a))
        // Leaving the app, as a screensaver does, closes the port and drops them; the question stays.
        shell("input keyevent KEYCODE_HOME")
        eventually("Leaving the app must stop the receiver") { receiver(a) == null }
        assertClosed(second)
        eventually("What waited is dropped") { waitingForTheirTurn(a) == 0 && placesHeld(a) == 1 }
        assertTrue("The question that was up is still up", confirmMessage(a)!!.contains("kept3.example.com"))
        cancelQuestion(a, "kept3.example.com")
        eventually("and nothing follows it") { placesHeld(a) == 0 }
        SystemClock.sleep(1_000)
        assertNull("The dropped texts are not asked about", confirmMessage(a))
        assertEquals("Not one was added", 2, saved().size)
    }

    // Both legs: three wrong codes stop it, however many right ones came between them, and what came before is still asked about.
    @SdkSuppress(minSdkVersion = 29)
    @Test fun threeWrongCodesStopItAndWhatCameIsStillAskedAbout() {
        val a = open()
        render(a, 1)
        val opened = openReceiveCard(a)
        val url = opened.url()
        assertTrue(sendWrongCode(url).startsWith("HTTP/1.1 404 "))
        assertSent(sendText(url, tomlFor("before.example.com")))
        askedAbout(a, "before.example.com")
        assertTrue(sendWrongCode(url).startsWith("HTTP/1.1 404 "))
        assertSame("Two wrong codes: still open", opened, receiver(a))
        assertTrue(sendWrongCode(url).contains(LinkReceiver.escape(context.getString(R.string.receive_page_locked_out))))
        eventually("Three wrong codes must stop the receiver") { receiver(a) == null }
        assertClosed(url)
        assertNull("and close the card", overlayTag(a, "receive"))
        assertEquals(listOf(context.getString(R.string.text_receive_locked_out)), toastTexts(a))
        assertTrue("What came is still asked about", confirmMessage(a)!!.contains("before.example.com"))
        assertEquals(1, placesHeld(a))
        cancelQuestion(a, "before.example.com")
        assertEquals(2, saved().size)
    }

    // Both legs: a file chosen on the phone instead of pasted. A send with nothing
    // in it or one too large to take is no strike. A TOML file with no name of its
    // own is named after its file, without the escape the browser wrote for the
    // quote, and with the backslash a name on a phone or a Mac may have. It is the
    // TOML that VpnDeviceTest validates natively, with a byte order mark and CRLF
    // line ends.
    @SdkSuppress(minSdkVersion = 29)
    @Test fun aFileFromAPhoneIsNamedAfterItAndAddedOnlyOnceTheTvConfirmsIt() {
        val a = open()
        render(a, 1)
        onMain { method("receiveFromPhone").invoke(a) }
        instrumentation.waitForIdleSync()
        val opened = receiver(a)
        assertNotNull("The emulator must have a private-network IPv4 address on Wi-Fi or Ethernet", opened)
        val url = opened!!.url()
        val page = http(url) { h, _ -> "GET / HTTP/1.1\r\nHost: $h\r\n\r\n" }
        assertTrue(page, page.contains("enctype=\"multipart/form-data\"") &&
            page.contains("<input type=\"file\" id=\"file\" name=\"file\">") &&
            page.contains(LinkReceiver.escape(context.getString(R.string.receive_page_file))))
        // Sent with nothing chosen, the form comes back, and the TV goes on waiting.
        val empty = httpBytes(url) { h, path -> filePost(h, path, "", ByteArray(0)) }
        assertTrue(empty, empty.startsWith("HTTP/1.1 400 ") &&
            empty.contains(LinkReceiver.escape(context.getString(R.string.receive_page_retry))))
        assertSame(opened, receiver(a))
        // A photo chosen by mistake gets the page that says it is too large: the TV
        // reads the rest before it closes, so the answer is not lost to a reset.
        val photo = httpBytes(url) { h, path -> filePost(h, path, "photo.jpg", ByteArray(2 * LinkReceiver.MAX_BODY_BYTES)) }
        assertTrue(photo, photo.startsWith("HTTP/1.1 413 ") &&
            photo.contains(LinkReceiver.escape(context.getString(R.string.receive_page_too_large))))
        assertSame(opened, receiver(a))
        val toml = "\uFEFF" + source.replace("vpn.invalid", "file.example.com").replace("\n", "\r\n") + "\r\n"
        val sent = httpBytes(url) { h, path -> filePost(h, path, "Home\\Office %22TV%22\u202E VPN.toml", toml.toByteArray()) }
        assertTrue(sent, sent.startsWith("HTTP/1.1 200 ") && sent.contains(context.getString(R.string.receive_page_sent)))
        // The receiver goes on after a file, as after any text.
        assertSame(opened, receiver(a))
        // Saved as the file has it, as one picked on the TV is. It is a file, not a link, that is asked about, and as on the desktop the
        // question does not say the name: the server is what it connects to, and the name is the one thing whoever sent it controls.
        val name = "Home\\Office \"TV\" VPN.toml"
        val from = context.getString(R.string.text_nsent_from_s, URI(url).host).trim()
        eventually("The confirmation must name the server and \"$from\"", 30_000) {
            confirmMessage(a)?.let { it.contains("file.example.com") && it.contains(from) } == true
        }
        assertTrue(confirmMessage(a)!!.startsWith(context.getString(R.string.import_question_file) + "\n"))
        assertFalse("The name is not in the question", confirmMessage(a)!!.contains("Office"))
        assertEquals("Nothing is added before the TV confirms", 2, saved().size)
        answerYes(a)
        eventually("Import must save it") { saved().size == 3 }
        assertEquals("Home\\Office \"TV\"\u202E VPN.toml", saved().last().getString("name"))
        assertTrue(saved().last().getString("toml").contains("hostname = \"file.example.com\""))
        click(a, "receive:back")
        eventually("The arrow must stop the receiver") { receiver(a) == null }
        assertClosed(url)
    }

    // Both legs: a TOML file with no name of its own and no file's name to go by,
    // pasted into the page or in a file whose name shows nothing, is named for where
    // it came from. As on the desktop the question does not say the name of what is new (the one thing whoever sent it controls), so
    // the first is asked about by its server and where it was sent from; the second has the first's name, a name already in the list,
    // which is the desktop's collision: that name is said, and Add copy adds it as another.
    @SdkSuppress(minSdkVersion = 29)
    @Test fun aTomlFromAPhoneWithNoFileNameIsNamedForWhereItCameFrom() {
        val a = open()
        render(a, 1)
        val named = context.getString(R.string.text_from_another_device)
        for ((server, fileName, collides) in listOf(Triple("pasted.example.com", null, false), Triple("nameless.example.com", "\u202E\u200F", true))) {
            onMain { method("receiveFromPhone").invoke(a) }
            instrumentation.waitForIdleSync()
            val opened = receiver(a)
            assertNotNull("The emulator must have a private-network IPv4 address on Wi-Fi or Ethernet", opened)
            val url = opened!!.url()
            val toml = source.replace("vpn.invalid", server)
            val sent = httpBytes(url) { h, path ->
                if (fileName == null) filePost(h, path, "", ByteArray(0), text = toml) else filePost(h, path, fileName, toml.toByteArray())
            }
            assertTrue(sent, sent.startsWith("HTTP/1.1 200 ") && sent.contains(context.getString(R.string.receive_page_sent)))
            val from = context.getString(R.string.text_nsent_from_s, URI(url).host).trim()
            eventually("The confirmation must say $server and \"$from\", and ${if (collides) "that $named exists" else "not $named"}", 30_000) {
                confirmMessage(a)?.let { it.contains(server) && it.contains(from) && it.contains(named) == collides } == true
            }
            if (collides) assertEquals(context.getString(R.string.import_exists, named), confirmMessage(a)!!.split("\n")[0])
            val before = saved().size
            SystemClock.sleep(500)
            click(a, if (collides) "confirm:alt" else "confirm:ok")
            eventually("Import must save it") { saved().size == before + 1 }
            assertEquals(named, saved().last().getString("name"))
            assertTrue(saved().last().getString("toml").contains("hostname = \"$server\""))
            click(a, "receive:back")
            eventually("The arrow must stop the receiver") { receiver(a) == null }
        }
    }

    // Back and the arrow close the card and stop the receiver (it has no button of its own); so does leaving the app. A remote starts on
    // the arrow, the one stop the card has, and the screen stays on while the card is open.
    @Test fun backTheArrowOrLeavingTheAppClosesThePort() {
        val a = open()
        // With a remote, the card opens on the arrow.
        leaveTouchMode(a)
        fun keepOn() = onMain { a.window.attributes.flags and WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON != 0 }
        fun openCard(): String {
            onMain { method("receiveFromPhone").invoke(a) }
            instrumentation.waitForIdleSync()
            assertNotNull("The card must be up", overlayTag(a, "receive"))
            assertTrue("No screensaver while the card is up", keepOn())
            return receiver(a)!!.url()
        }
        val first = openCard()
        eventually("A remote must start on the back arrow") { focusedKey(a) == "receive:back" }
        assertEquals("which is the card's only stop", 1, onMain {
            val controls = ArrayList<View>()
            root(a).findViewWithTag<View>("overlay:receive").addFocusables(controls, View.FOCUS_FORWARD)
            controls.size
        })
        ok(a, "receive:back")
        eventually("OK on the arrow must close the card") { overlayTag(a, "receive") == null }
        assertNull("The arrow must stop the receiver", receiver(a))
        assertClosed(first)
        assertFalse("and the screen may go off again", keepOn())
        // Each time is a new code on a new port.
        val second = openCard()
        assertNotEquals(first, second)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        eventually("Back must close the card") { overlayTag(a, "receive") == null }
        assertNull(receiver(a))
        assertClosed(second)
        val third = openCard()
        click(a, "receive:back")
        eventually("The arrow must close the card") { overlayTag(a, "receive") == null }
        assertNull(receiver(a))
        assertClosed(third)
        val fourth = openCard()
        // Home, as a screensaver does, stops the activity.
        shell("input keyevent KEYCODE_HOME")
        eventually("Leaving the app must stop the receiver") { receiver(a) == null }
        assertClosed(fourth)
    }

    // A file sets the server that the confirmation shows and, where its name is one in the list already, that name. Line breaks, characters
    // that reverse or hide text, a long run of text or a tall stack of marks in them must not push what comes after, the sender and that
    // verification is off, out of a message a remote cannot scroll, or paint over it. The confirmation is opened directly, as after the
    // checks: whether the core takes such a server name is the core's to say. As on the desktop, the question does not say the name of
    // what is new: it is the one thing whoever sent it controls.
    @Test fun theConfirmationShowsTheSenderWhateverTheFileSays() {
        val tall = "My \uD83D\uDD12 VPN" + "\u2028\u2029\r\u202E\u2066".repeat(20) + "y".repeat(72)
        val stack = "\u0316\u0317\u0318\u0319".repeat(20).dropLast(1)
        val profiles = org.json.JSONArray()
        for (name in listOf(tall, "a$stack")) profiles.put(org.json.JSONObject().put("name", name).put("toml", source))
        assertTrue(ConfigStore.write(prefs, profiles.toString()))
        val a = open()
        // The confirmation's message for that name and server, then cancelled.
        fun message(name: String, hostname: String): String {
            val toml = source.replace("hostname = \"vpn.invalid\"", "hostname = \"$hostname\"\nskip_verification = true")
            onMain {
                method("showImportConfirmation", String::class.java, Boolean::class.javaPrimitiveType,
                    RoutingConfig.Import::class.java, String::class.java, Boolean::class.javaPrimitiveType)
                    .invoke(a, name, false, RoutingConfig.Import(toml, false), "192.0.2.7", false)
            }
            instrumentation.waitForIdleSync()
            try {
                return confirmMessage(a)!!
            } finally {
                click(a, "confirm:cancel")
                eventually("The confirmation must close") { overlayTag(a, "confirm") == null }
            }
        }
        // Each on one line and cut to 80 characters, so the sender is the third line.
        fun assertShown(message: String, question: String, shownServer: String) {
            val sentFrom = context.getString(R.string.text_nsent_from_s, "192.0.2.7")
            assertTrue(message, message.startsWith(question + "\n" + context.getString(R.string.import_server, shownServer) + sentFrom + "\n" +
                context.getString(R.string.import_skips_file)))
            assertEquals(sentFrom.trim(), message.split("\n")[2])
        }
        // On one line, the name is 80, so it is shown whole, and the server is 81.
        assertShown(message(tall, "my-real-vpn.example${"\\n".repeat(40)}${"x".repeat(62)}"),
            context.getString(R.string.import_exists, "My \uD83D\uDD12 VPN" + "y".repeat(72)), "my-real-vpn.example" + "x".repeat(60) + "\u2026")
        // A letter and 79 marks under it fit in 80 characters, and drawn, a stack that
        // tall would cover the lines below it. Each character is shown with two at most.
        assertShown(message("a$stack", "vpn${stack}.example"), context.getString(R.string.import_exists, "a\u0316\u0317"), "vpn\u0316\u0317.example")
        // A name that is not in the list is not said at all, whatever it holds.
        assertShown(message("Mine \u202E\u2028 reversed", "plain.example"), context.getString(R.string.import_question_file), "plain.example")
    }

    // A configuration named after its file keeps the file's extension, which is not
    // counted as another alphabet: "Дом.toml" is no mix, but a name that mixes them
    // itself, or in its extension, still is. A link's or an export's own name is
    // counted whole, as is the server a link without one is named after: "раураӏ.com"
    // is Cyrillic made to look like "paypal.com".
    @Test fun aFilesExtensionIsNoOtherAlphabet() {
        val a = open()
        val note = context.getString(R.string.import_mixed_name)
        val lookalike = "\u0440\u0430\u0443\u0440\u0430\u04cf.com"
        for ((name, namedAfterFile, mixed) in listOf(Triple("\u0414\u043e\u043c.toml", true, false),
                Triple("\u0414o\u043c.toml", true, true), Triple("\u0414\u043e\u043c.t\u043eml", true, true),
                Triple("home.toml", true, false), Triple(lookalike, false, true), Triple("\u0414\u043e\u043c.toml", false, true),
                Triple("home.toml", false, false))) {
            onMain {
                method("showImportConfirmation", String::class.java, Boolean::class.javaPrimitiveType,
                    RoutingConfig.Import::class.java, String::class.java, Boolean::class.javaPrimitiveType)
                    .invoke(a, name, namedAfterFile, RoutingConfig.Import(source, false), "192.0.2.7", false)
            }
            instrumentation.waitForIdleSync()
            try {
                val message = confirmMessage(a)!!
                assertEquals("$name, named after a file: $namedAfterFile: $message", mixed, message.contains(note))
            } finally {
                click(a, "confirm:cancel")
                eventually("The confirmation must close") { overlayTag(a, "confirm") == null }
            }
        }
    }

    // The in-app overlays (showOverlay): a confirmation, a select popup and a menu, the toast. Each
    // is a view in the window, found by its tag, so none of them is a system dialog.

    private fun overlayTag(a: MainActivity, name: String): View? = onMain { root(a).findViewWithTag<View?>("overlay:$name") }
    // Asks for a confirmation. On the main thread, which is where the activity asks for one.
    private fun askConfirm(a: MainActivity, message: String, onConfirm: () -> Unit = {}, onCancel: () -> Unit = {}) {
        method("showConfirm", CharSequence::class.java, CharSequence::class.java, CharSequence::class.java,
            Runnable::class.java, Runnable::class.java, Runnable::class.java)
            .invoke(a, message, "Delete", null, Runnable { onConfirm() }, null, Runnable { onCancel() })
    }
    private fun showConfirm(a: MainActivity, message: String, onConfirm: () -> Unit = {}, onCancel: () -> Unit = {}) {
        onMain { askConfirm(a, message, onConfirm, onCancel) }
        instrumentation.waitForIdleSync()
    }
    private fun confirmMessage(a: MainActivity): String? =
        onMain { root(a).findViewWithTag<TextView?>("confirm:message")?.text?.toString() }
    private fun click(a: MainActivity, key: String) = onMain { assertTrue("$key must be clickable", root(a).findViewWithTag<View>(key).performClick()) }
    // As a remote does it: the focus on the control, and OK. What an overlay opened this way gives the focus back to, when it closes, is the
    // control: click() leaves the focus where it was, and so on whatever the page had it on.
    private fun ok(a: MainActivity, key: String) {
        focusOn(a, key)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_CENTER)
        instrumentation.waitForIdleSync()
    }
    // Back closes an overlay. With the soft keyboard up, which the app picker puts up on a phone (the CI emulator has no hardware keyboard),
    // the first Back only takes the keyboard away, as for any app.
    private fun closeWithBack(a: MainActivity, tag: String, what: String = tag) {
        repeat(3) {
            if (overlayTag(a, tag) == null) return
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
            val deadline = SystemClock.elapsedRealtime() + 1_500
            while (overlayTag(a, tag) != null && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(50)
        }
        eventually("$what must close with Back") { overlayTag(a, tag) == null }
    }
    // The control with this key, in whichever overlay or page has it.
    private fun byKey(a: MainActivity, key: String): View = onMain {
        root(a).findViewWithTag<View?>(key) ?: throw AssertionError("No control has the key $key")
    }
    // A confirmation's red button, and its alternative, take no click for the first 400 ms.
    private fun answerYes(a: MainActivity) {
        SystemClock.sleep(500)
        click(a, "confirm:ok")
    }
    private fun toastTexts(a: MainActivity): List<String> = onMain {
        val layer = field("toastLayer").get(a) as ViewGroup
        (0 until layer.childCount).map { (layer.getChildAt(it) as TextView).text.toString() }
    }

    // The service tells an app that is on the screen why a connection failed (its notice is silent meanwhile); the app says it in its own
    // toast, as the desktop's window does with an error (Main.qml: toast.show). The toast is the app's own, never the system's.
    @Test fun aFailureReasonSentToTheOpenAppIsSaidInItsOwnToast() {
        val a = open()
        val reason = "Could not start VPN (TestFailure)"
        assertEquals(emptyList<String>(), toastTexts(a))
        context.sendBroadcast(Intent(FreeTunnelVpnService.ACTION_FAILURE).setPackage(context.packageName)
            .putExtra(FreeTunnelVpnService.EXTRA_REASON, reason))
        eventually("The reason is said in the app's toast") { toastTexts(a) == listOf(reason) }
    }

    // render() rebuilds the page and leaves the overlays alone. Back closes the top overlay, and goes
    // no further: not Home on a TV, and not out of the app.
    @Test fun anOverlaySurvivesARebuildAndBackClosesItBeforeAnythingElse() {
        val a = open()
        render(a, 3)
        showConfirm(a, "Delete this?")
        assertNotNull("The confirmation must be in the window", overlayTag(a, "confirm"))
        render(a, 3)
        assertNotNull("An overlay must survive render()", overlayTag(a, "confirm"))
        assertEquals("Delete this?", confirmMessage(a))
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        eventually("Back must close the overlay") { overlayTag(a, "confirm") == null }
        assertFalse("Back must not leave the app while an overlay is up", onMain { a.isFinishing })
        assertEquals("Back must not change the page while an overlay is up", 3, onMain { field("page").getInt(a) })
    }

    // While an overlay is up the page behind it takes no focus and no accessibility, the overlay's
    // default control has the focus and no arrow leaves it, and when it closes the focus is back on
    // what opened it.
    @Test fun anOverlayTakesTheFocusAndGivesItBack() {
        val a = open()
        leaveTouchMode(a)
        render(a, 1)
        focusOn(a, "configs:add")
        showConfirm(a, "Delete this?")
        eventually("Cancel takes the focus first") { focusedKey(a) == "confirm:cancel" }
        onMain {
            assertEquals("The page behind takes no focus", ViewGroup.FOCUS_BLOCK_DESCENDANTS, pageRoot(a).descendantFocusability)
            assertEquals("The page behind is hidden from accessibility", View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS, pageRoot(a).importantForAccessibility)
        }
        for (key in listOf(KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_DOWN,
                KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_TAB)) {
            instrumentation.sendKeyDownUpSync(key)
            instrumentation.waitForIdleSync()
            val focused = focusedKey(a)
            assertTrue("The focus left the overlay for $focused", focused != null && focused.startsWith("confirm:"))
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        eventually("Back gives the focus back to what opened it") { focusedKey(a) == "configs:add" }
        onMain {
            assertEquals(ViewGroup.FOCUS_BEFORE_DESCENDANTS, pageRoot(a).descendantFocusability)
            assertEquals(View.IMPORTANT_FOR_ACCESSIBILITY_AUTO, pageRoot(a).importantForAccessibility)
        }
    }

    // A confirmation asked for while another is up waits its turn and does not replace it. The red
    // button takes no click for its first 400 ms, so that an OK held or repeated on a remote cannot
    // answer what a phone or a web page asked. A tap on the backdrop, like Back, is Cancel.
    @Test fun aConfirmationWaitsForTheOneShownAndIgnoresClicksUntilItIsArmed() {
        val a = open()
        val answers = java.util.concurrent.CopyOnWriteArrayList<String>()
        // Asked and clicked in the same moment, on the main thread: inside the 400 ms, however slow the device is.
        onMain {
            askConfirm(a, "Delete?", onConfirm = { answers.add("yes") }, onCancel = { answers.add("no") })
            assertTrue(root(a).findViewWithTag<View>("confirm:ok").performClick())
        }
        assertEquals("A click in the first 400 ms is ignored", emptyList<String>(), answers.toList())
        assertNotNull(overlayTag(a, "confirm"))
        SystemClock.sleep(500)
        click(a, "confirm:ok")
        assertEquals(listOf("yes"), answers.toList())
        eventually("Delete closes it") { overlayTag(a, "confirm") == null }

        answers.clear()
        showConfirm(a, "First?", onConfirm = { answers.add("first yes") }, onCancel = { answers.add("first no") })
        showConfirm(a, "Second?", onConfirm = { answers.add("second yes") }, onCancel = { answers.add("second no") })
        assertEquals("The second waits: it does not replace the first", "First?", confirmMessage(a))
        onMain { root(a).findViewWithTag<OverlayLayer>("overlay:confirm").backdrop().performClick() }
        eventually("The second shows after the first is answered") { confirmMessage(a) == "Second?" }
        assertEquals("A tap on the backdrop cancels", listOf("first no"), answers.toList())
        click(a, "confirm:cancel")
        eventually("Cancel closes the second") { overlayTag(a, "confirm") == null }
        assertEquals(listOf("first no", "second no"), answers.toList())
    }

    // The card is as wide as the wider of its buttons' row and its question's widest wrapped line, each with 14 at the sides, and no wider than
    // the window less 24 (qml/components/ConfirmDialog.qml); the buttons, which cannot wrap, give up their padding and minimum width, and elide,
    // when their row is wider than the card can be. It once collapsed to a few pixels, the question cut to single characters and the buttons
    // 2 px across: a spacer view between the message and the buttons was 1 wide, and a column with a child that does not fill takes that
    // child's width.
    @Test fun aConfirmationIsAsWideAsWhatItHoldsAndNoWiderThanTheWindowLess24() {
        val a = open()
        val density = onMain { a.resources.displayMetrics.density }
        fun window() = onMain { root(a).width - root(a).paddingLeft - root(a).paddingRight }
        fun panel() = onMain { (root(a).findViewWithTag<View>("overlay:confirm") as OverlayLayer).panel() }
        fun sized(key: String) = onMain { root(a).findViewWithTag<View>(key).let { it.width to it.height } }
        // A short question: the buttons decide, 76 at least each and 8 apart, and the card is that and 28.
        showConfirm(a, "Delete?")
        val (cancel, _) = sized("confirm:cancel")
        val (ok, _) = sized("confirm:ok")
        assertTrue("Cancel is 76 wide at least: $cancel", cancel >= Math.round(76 * density) - 1)
        assertEquals("The card is its buttons and 28", cancel + ok + 8 * density + 28 * density, panel().width.toFloat(), 3f)
        assertTrue("and the question is readable: ${sized("confirm:message")}", sized("confirm:message").let { it.first > 40 * density && it.second > 10 * density })
        click(a, "confirm:cancel")
        eventually("The card closes") { overlayTag(a, "confirm") == null }
        // A long question wraps within the window less 84, and the card is that and 28: well short of the window, and no wider than it less 24.
        showConfirm(a, "Delete this server and everything that was kept for it? ".repeat(8))
        val wide = panel().width
        assertTrue("It is wider than the buttons: $wide", wide > 200 * density)
        assertTrue("and no wider than the window less 24: $wide of ${window()}", wide <= window() - 24 * density + 1)
        assertTrue("with the question as wide as the card less 28: ${sized("confirm:message")}", Math.abs(sized("confirm:message").first - (wide - 28 * density)) <= 2)
        click(a, "confirm:cancel")
        eventually("The card closes again") { overlayTag(a, "confirm") == null }
        // Three buttons, two of them with labels far wider than any window (the first is always Cancel): they give up their padding and elide.
        // The desktop's buttonWidth is the label's width and 16, and no more than a third of the room the row has (the window less 52, and
        // the two gaps), so the long two take a third each and Cancel only its own words; the card is that row and 28, which is short of the
        // window less 24 as long as one of the buttons is short.
        val label = "A label that is much longer than the window is wide ".repeat(3)
        onMain {
            method("showConfirm", CharSequence::class.java, CharSequence::class.java, CharSequence::class.java,
                Runnable::class.java, Runnable::class.java, Runnable::class.java).invoke(a, "Which?", label, label, null, null, null)
        }
        instrumentation.waitForIdleSync()
        val card = panel()
        val share = (window() - 52 * density - 2 * 8 * density) / 3
        val (cancelWidth, _) = sized("confirm:cancel")
        val (altWidth, _) = sized("confirm:alt")
        val (okWidth, _) = sized("confirm:ok")
        assertEquals("A long label takes a third of the room: $altWidth", share, altWidth.toFloat(), 3f)
        assertEquals("both of them: $okWidth", share, okWidth.toFloat(), 3f)
        assertTrue("Cancel is its words and 16, not a third: $cancelWidth of $share", cancelWidth < share)
        assertEquals("The card is its buttons, 8 apart, and 28: ${card.width}", cancelWidth + altWidth + okWidth + 2 * 8 * density + 28 * density,
            card.width.toFloat(), 4f)
        assertTrue("and no wider than the window less 24: ${card.width} of ${window()}", card.width <= window() - 24 * density + 1)
        onMain {
            for (key in listOf("confirm:cancel", "confirm:alt", "confirm:ok")) {
                val button = root(a).findViewWithTag<TextView>(key)
                assertTrue("$key is inside the card: ${button.right} of ${card.width}", button.width > 0 && IntArray(2).also { button.getLocationInWindow(it) }[0] + button.width <=
                    IntArray(2).also { card.getLocationInWindow(it) }[0] + card.width)
                assertEquals("$key has given up its padding", Math.round(4 * density), button.paddingLeft)
            }
            assertEquals(TextUtils.TruncateAt.END, root(a).findViewWithTag<TextView>("confirm:ok").ellipsize)
        }
        click(a, "confirm:cancel")
        eventually("and closes") { overlayTag(a, "confirm") == null }
    }

    // The toast is over the cards (the editor, the picker) and under the popups and the confirmations, as the desktop's is: the desktop's toast
    // is z 1000, its popups 1500 and its window-level confirmation 2500, its cards 0.
    @Test fun theToastIsOverTheCardsAndUnderTheConfirmationsAndPopups() {
        val a = open()
        render(a, 1)
        onMain {
            val host = root(a)
            val order = listOf("overlayLayer", "toastLayer", "popupLayer").map { host.indexOfChild(field(it).get(a) as View) }
            assertEquals("The page, the cards, the toast, then popups and confirmations", order.sorted(), order)
            assertTrue(order.first() > host.indexOfChild(pageRoot(a)))
        }
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 0) }
        instrumentation.waitForIdleSync()
        val editor = overlayTag(a, "editor")!!
        assertSame("A card is in the layer under the toast", field("overlayLayer").get(a), editor.parent)
        showConfirm(a, "Delete?")
        val question = overlayTag(a, "confirm")!!
        assertSame("A confirmation is in the layer over it", field("popupLayer").get(a), question.parent)
        click(a, "confirm:cancel")
        eventually("The question closes") { overlayTag(a, "confirm") == null }
        onMain { method("showSelect", String::class.java, Array<String>::class.java, Int::class.javaPrimitiveType, Class.forName("org.freetunnel.android.MainActivity\$Pick"))
            .invoke(a, "editor:profile", arrayOf("Default"), 0, null) }
        val choices = overlayTag(a, "select")!!
        assertSame("and so is a popup", field("popupLayer").get(a), choices.parent)
    }

    // The status and navigation bars take the backdrop's dim with the page while a card or a confirmation is open, as the desktop's backdrop
    // dims its whole window, and are the page's again after: they were bright bands round a dimmed page. A card and a question over it are
    // two backdrops, and the bars take the dim of both, as the page does. Before Android 15 the bars are the system's, outside the window,
    // and take the dim as a colour. From Android 15 on, with the app targeting it (the phone leg), the window is edge to edge: the bars are
    // drawn over it, their colours cannot be set, and the card's backdrop did not dim what was under them (a geometry check passed that, with
    // the layer running the window's length, while the strips in the pictures stayed the page's colour). So there the test looks at what is
    // drawn: the window as the app draws it, in the middle of the status bar's strip and of the navigation bar's, against the page beside
    // them and against the colour the page has under that many backdrops.
    @Test fun theSystemBarsDimWithACardAndComeBack() {
        val a = open()
        render(a, 1)
        val t = theme(a)
        fun dimmed(backdrops: Int) = (1..backdrops).fold(t.bg) { under, _ -> Theme.over(t.backdrop, under) }
        val outside = android.os.Build.VERSION.SDK_INT < 35
        fun bars() = onMain { a.window.statusBarColor to a.window.navigationBarColor }
        // The window as the activity draws it (in software, as the UI tour's pictures of a FLAG_SECURE window are), which holds the decor's
        // overlay and none of the system's own drawing.
        fun drawn(): Bitmap = onMain {
            val decor = a.window.decorView
            Bitmap.createBitmap(decor.width, decor.height, Bitmap.Config.ARGB_8888).also { decor.draw(Canvas(it)) }
        }
        fun sameShade(x: Int, y: Int, tolerance: Int) = Math.abs(Color.red(x) - Color.red(y)) <= tolerance &&
            Math.abs(Color.green(x) - Color.green(y)) <= tolerance && Math.abs(Color.blue(x) - Color.blue(y)) <= tolerance
        fun dim(backdrops: Int): Boolean {
            if (outside) return bars() == (dimmed(backdrops) to dimmed(backdrops))
            val insets = onMain { a.window.decorView.rootWindowInsets.getInsets(android.view.WindowInsets.Type.systemBars()) }
            val picture = drawn()
            val page = dimmed(backdrops)
            // At the window's left edge, where nothing is drawn over the page's colour.
            if (insets.top > 0) {
                val strip = picture.getPixel(2, insets.top / 2)
                if (!sameShade(strip, picture.getPixel(2, insets.top + 2), 2) || !sameShade(strip, page, 3)) return false
            }
            if (insets.bottom > 0) {
                val strip = picture.getPixel(2, picture.height - 1 - insets.bottom / 2)
                if (!sameShade(strip, picture.getPixel(2, picture.height - insets.bottom - 3), 2) || !sameShade(strip, page, 3)) return false
            }
            return true
        }
        if (outside) assertEquals(t.bg to t.bg, bars())
        else assertTrue("The phone's window has a status bar, and the strips are the page's before anything is open",
            onMain { a.window.decorView.rootWindowInsets.getInsets(android.view.WindowInsets.Type.systemBars()).top } > 0 && dim(0))
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 0) }
        eventually("The bars dim with the card") { dim(1) }
        showConfirm(a, "Delete?")
        eventually("The question is up") { overlayTag(a, "confirm") != null }
        eventually("and the bars take the dim of both, as the page does") { dim(2) }
        click(a, "confirm:cancel")
        eventually("The question closes") { overlayTag(a, "confirm") == null }
        eventually("a card's question closed, the card still dims them once") { dim(1) }
        click(a, "editor:cancel")
        eventually("The card closes and the bars are the page's again") { overlayTag(a, "editor") == null && dim(0) }
        // A popup has no backdrop to dim with.
        render(a, 3)
        click(a, "settings:theme")
        eventually("The popup is up") { overlayTag(a, "select") != null }
        assertTrue("a popup dims nothing", dim(0))
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        eventually("and goes") { overlayTag(a, "select") == null }
    }

    // A select popup sits under the right edge of its control, 4 below it, and after a render it is put
    // back there; with its control gone it closes. The current choice is in the accent with a check mark.
    @Test fun aPopupGoesBackUnderItsControlAfterARebuildAndClosesWithoutIt() {
        val a = open()
        render(a, 3)
        val pickType = Class.forName("org.freetunnel.android.MainActivity\$Pick")
        val picks = java.util.concurrent.CopyOnWriteArrayList<Int>()
        val pick = java.lang.reflect.Proxy.newProxyInstance(pickType.classLoader, arrayOf(pickType)) { _, _, args -> picks.add(args[0] as Int); null }
        onMain {
            method("showSelect", String::class.java, Array<String>::class.java, Int::class.javaPrimitiveType, pickType)
                .invoke(a, "settings:theme", arrayOf("System", "Light", "Dark"), 1, pick)
        }
        instrumentation.waitForIdleSync()
        // Where the popup is against its anchor: how far its right edge is from the anchor's, and its gap below.
        fun placed(): List<Int> = onMain {
            val anchor = root(a).findViewWithTag<View>("settings:theme")
            val layer = root(a).findViewWithTag<ViewGroup>("overlay:select")
            val panel = layer.getChildAt(layer.childCount - 1)
            val at = IntArray(2).also { anchor.getLocationInWindow(it) }
            val inWindow = IntArray(2).also { panel.getLocationInWindow(it) }
            listOf(at[0] + anchor.width - (inWindow[0] + panel.width), inWindow[1] - (at[1] + anchor.height))
        }
        val first = placed()
        val four = onMain { Math.round(4 * a.resources.displayMetrics.density) }
        // Its right edge is where the dropdown's content ends, at the page's margin (the control's own padding goes past it), and it is 4 under
        // the row, which the control is as high as: the desktop's ddVal is the 42 of the row.
        assertEquals("The popup's right edge is the control's content's", onMain { root(a).findViewWithTag<View>("settings:theme").paddingRight }, first[0])
        assertEquals("The popup is 4 under the control", four, first[1])
        assertEquals("and the control is as high as its row", onMain { Math.round(42 * a.resources.displayMetrics.density) },
            onMain { root(a).findViewWithTag<View>("settings:theme").height })
        // The colour at rest, whichever row a remote starts on: a row's focus is its fill, and its words keep their colour.
        val texts = onMain { descendants(root(a).findViewWithTag<View>("overlay:select")).filterIsInstance<TextView>().map { it.text.toString() to it.textColors.defaultColor }.toList() }
        assertEquals(listOf("System", "Light", "Dark"), texts.map { it.first })
        assertEquals("The current choice is in the accent", theme(a).accent, texts.single { it.first == "Light" }.second)
        // Its check mark is drawn (9.6 by 7, as Inter draws it at the desktop's 14 px), one, in the accent.
        val checks = onMain { descendants(root(a).findViewWithTag<View>("overlay:select")).filterIsInstance<ImageView>().toList() }
        assertEquals("and so is its check mark, which only it has", listOf(theme(a).accent), checks.map { it.imageTintList!!.defaultColor })
        assertTrue("and is 9.6 across", Math.abs(Math.round(9.6f * a.resources.displayMetrics.density) - checks.single().drawable.intrinsicWidth) <= 1)
        assertEquals("The others are in the text colour", theme(a).text, texts.single { it.first == "Dark" }.second)
        render(a, 3)
        instrumentation.waitForIdleSync()
        assertEquals("After a render the popup is under its control again", first, placed())
        click(a, "select:2")
        assertEquals("Picking a row runs its choice", listOf(2), picks.toList())
        eventually("and closes the popup") { overlayTag(a, "select") == null }
        // With the control gone, such as after a tab, a popup closes.
        onMain {
            method("showSelect", String::class.java, Array<String>::class.java, Int::class.javaPrimitiveType, pickType)
                .invoke(a, "settings:theme", arrayOf("System", "Light", "Dark"), 0, pick)
        }
        assertNotNull(overlayTag(a, "select"))
        render(a, 0)
        eventually("A popup closes when its control is gone") { overlayTag(a, "select") == null }
    }

    // A menu: rows of 40 with a separator slot of 11, under the control and centred in the window;
    // a row runs its action and closes it. Its first row has the focus.
    @Test fun aMenuHasRowsOf40AndASeparatorAndRunsWhatARowSays() {
        val a = open()
        leaveTouchMode(a)
        render(a, 1)
        val entry = Class.forName("org.freetunnel.android.MainActivity\$MenuEntry")
        val make = entry.getDeclaredConstructor(String::class.java, String::class.java, Runnable::class.java).apply { isAccessible = true }
        val separator = entry.getDeclaredField("SEPARATOR").apply { isAccessible = true }.get(null)
        val ran = java.util.concurrent.CopyOnWriteArrayList<String>()
        val items = java.util.ArrayList<Any?>().apply {
            add(make.newInstance("Paste from clipboard", "add:paste", Runnable { ran.add("paste") }))
            add(make.newInstance("From file…", "add:file", Runnable { ran.add("file") }))
            add(separator)
            add(make.newInstance("Create new…", "add:create", Runnable { ran.add("create") }))
        }
        onMain { method("showMenu", String::class.java, String::class.java, java.util.List::class.java).invoke(a, "addmenu", "configs:add", items) }
        eventually("The first row has the focus") { focusedKey(a) == "add:paste" }
        onMain {
            val dp = a.resources.displayMetrics.density
            for (key in listOf("add:paste", "add:file", "add:create"))
                assertEquals("$key is 40 high", Math.round(40 * dp), root(a).findViewWithTag<View>(key).height)
            val panel = (root(a).findViewWithTag<View>("overlay:addmenu") as OverlayLayer).panel()
            val width = panel.width
            assertTrue("The menu is at least 140 wide", width >= Math.round(140 * dp))
            // As wide as the widest label at 14 and 40 more, as the desktop sizes it (its 5 of margin at each side are in that).
            val paint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG).apply {
                typeface = Fonts.regular(a)
                textSize = 14 * a.resources.displayMetrics.scaledDensity
            }
            val widest = listOf("Paste from clipboard", "From file…", "Create new…").maxOf { Math.ceil(paint.measureText(it).toDouble()).toInt() }
            assertEquals("The menu is its widest label and 40", Math.max(Math.round(140 * dp), widest + Math.round(40 * dp)).toFloat(), width.toFloat(), 3f)
            // A popup casts the desktop's shadow, drawn by its layer; the platform's elevation, at a fifth of the colour it is given, is not used.
            assertNotNull((root(a).findViewWithTag<View>("overlay:addmenu") as OverlayLayer).shadow())
            assertEquals(0f, panel.elevation, 0f)
            assertTrue("The menu is centred in the window", Math.abs((panel.left + panel.right) / 2 - root(a).width / 2) <= 2)
            val file = root(a).findViewWithTag<View>("add:file")
            val create = root(a).findViewWithTag<View>("add:create")
            val gap = create.top - file.bottom
            assertTrue("The separator's slot (11) and the 1 between rows are between them: $gap", gap >= Math.round(11 * dp))
        }
        click(a, "add:file")
        assertEquals(listOf("file"), ran.toList())
        eventually("A row closes the menu") { overlayTag(a, "addmenu") == null }
    }

    // The toast is in the window, one at a time, at the bottom or, while an overlay is open, at the top.
    // A tap dismisses it.
    @Test fun theToastIsInTheWindowOneAtATimeAndATapDismissesIt() {
        val a = open()
        fun toasts(): List<String> = onMain {
            val layer = field("toastLayer").get(a) as ViewGroup
            (0 until layer.childCount).map { (layer.getChildAt(it) as TextView).text.toString() }
        }
        fun gravity(): Int = onMain { ((field("toastLayer").get(a) as ViewGroup).getChildAt(0).layoutParams as FrameLayout.LayoutParams).gravity }
        // Long enough to stay for as long as this takes: 55 ms a character.
        val second = "Second notice " + "x".repeat(150)
        onMain { method("showToast", CharSequence::class.java).invoke(a, "First notice") }
        assertEquals(listOf("First notice"), toasts())
        onMain { method("showToast", CharSequence::class.java).invoke(a, second) }
        assertEquals("A new toast replaces the old", listOf(second), toasts())
        assertEquals(Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL, gravity())
        showConfirm(a, "Delete?")
        assertEquals("Behind an overlay it would not be seen: at the top", Gravity.TOP or Gravity.CENTER_HORIZONTAL, gravity())
        click(a, "confirm:cancel")
        eventually("and at the bottom again once it is closed") { gravity() == (Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL) }
        onMain {
            val toast = (field("toastLayer").get(a) as ViewGroup).getChildAt(0)
            assertFalse("A toast is no control a remote could focus", toast.isFocusable)
            val now = SystemClock.uptimeMillis()
            toast.dispatchTouchEvent(MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN, 5f, 5f, 0))
            toast.dispatchTouchEvent(MotionEvent.obtain(now, now + 10, MotionEvent.ACTION_UP, 5f, 5f, 0))
        }
        eventually("A tap dismisses the toast") { toasts().isEmpty() }
    }

    // A name already in the list, or the same file, is the desktop's name clash: Cancel, Add copy and Replace, in that
    // order, with Cancel first in the focus. The red button and Add copy take no click for 400 ms. Replace puts the config
    // where the one it replaces is; Add copy adds another. The page does not change, and the toast says what was added.
    @Test fun aNameClashOffersAddCopyAndReplaceAndReplaceKeepsThePosition() {
        val a = open()
        leaveTouchMode(a)
        render(a, 3)
        fun importAs(name: String, hostname: String) {
            val toml = source.replace("vpn.invalid", hostname)
            onMain {
                method("showImportConfirmation", String::class.java, Boolean::class.javaPrimitiveType,
                    RoutingConfig.Import::class.java, String::class.java, Boolean::class.javaPrimitiveType)
                    .invoke(a, name, false, RoutingConfig.Import(toml, false), null, true)
            }
            instrumentation.waitForIdleSync()
        }
        importAs("Profile 1", "other.example.com")
        assertEquals(context.getString(R.string.import_exists, "Profile 1") + "\n" + context.getString(R.string.import_server, "other.example.com"),
            confirmMessage(a))
        eventually("Cancel takes the focus first") { focusedKey(a) == "confirm:cancel" }
        onMain {
            assertEquals(context.getString(R.string.import_replace), (root(a).findViewWithTag<View>("confirm:ok") as TextView).text.toString())
            assertEquals(context.getString(R.string.import_add_copy), (root(a).findViewWithTag<View>("confirm:alt") as TextView).text.toString())
            val keys = descendants(root(a).findViewWithTag<View>("overlay:confirm")).mapNotNull { it.tag as? String }.filter { it.startsWith("confirm:") && it != "confirm:message" }.toList()
            assertEquals("Cancel, the alternative and the red button, in that order", listOf("confirm:cancel", "confirm:alt", "confirm:ok"), keys)
            // Asked and clicked in the same moment: inside the 400 ms, however slow the device is.
            assertTrue(root(a).findViewWithTag<View>("confirm:alt").performClick())
            assertTrue(root(a).findViewWithTag<View>("confirm:ok").performClick())
        }
        assertEquals("Neither answer counts in the first 400 ms", 2, saved().size)
        assertNotNull(overlayTag(a, "confirm"))
        answerYes(a)
        eventually("Replace saves it") { saved().first { it.getString("name") == "Profile 1" }.getString("toml").contains("other.example.com") }
        assertEquals("Replace adds nothing, and the position is the same", listOf("Profile 0", "Profile 1"), saved().map { it.getString("name") })
        assertEquals("The page does not change", 3, onMain { field("page").getInt(a) })
        assertEquals(listOf(context.getString(R.string.config_added, "Profile 1")), toastTexts(a))
        eventually("The question closes") { overlayTag(a, "confirm") == null }
        // Add copy.
        importAs("Profile 0", "copy.example.com")
        SystemClock.sleep(500)
        click(a, "confirm:alt")
        eventually("Add copy adds another") { saved().size == 3 }
        assertEquals(listOf("Profile 0", "Profile 1", "Profile 0"), saved().map { it.getString("name") })
        assertTrue(saved().last().getString("toml").contains("copy.example.com"))
        assertEquals("The first one is as it was", "vpn.invalid", org.tomlj.Toml.parse(saved().first().getString("toml")).getString("endpoint.hostname"))
        eventually("The question closes") { overlayTag(a, "confirm") == null }
        // Cancel adds nothing, and a name that is new has two buttons only.
        importAs("Profile 1", "cancelled.example.com")
        click(a, "confirm:cancel")
        eventually("Cancel closes it") { overlayTag(a, "confirm") == null }
        assertEquals(3, saved().size)
        importAs("Brand new", "new.example.com")
        assertEquals("The question of the desktop's, not the name, and the server",
            context.getString(R.string.import_question_link) + "\n" + context.getString(R.string.import_server, "new.example.com"), confirmMessage(a))
        assertNull("A name that is new has no alternative", onMain { root(a).findViewWithTag<View?>("confirm:alt") })
        assertEquals(context.getString(R.string.import_confirm), onMain { (root(a).findViewWithTag<View>("confirm:ok") as TextView).text.toString() })
        answerYes(a)
        eventually("Import adds it") { saved().size == 4 }
        assertEquals("Brand new", saved().last().getString("name"))
    }

    // The desktop's collision is a name already in the list, and only that: the same server under another name is added as a new config,
    // and asked about as one, so that Replace never renames a config to a name the question did not say.
    @Test fun theSameServerUnderAnotherNameIsNoCollision() {
        val a = open()
        onMain {
            method("showImportConfirmation", String::class.java, Boolean::class.javaPrimitiveType,
                RoutingConfig.Import::class.java, String::class.java, Boolean::class.javaPrimitiveType)
                .invoke(a, "Another name", false, RoutingConfig.Import(source, false), null, true)
        }
        instrumentation.waitForIdleSync()
        assertEquals(context.getString(R.string.import_question_link) + "\n" + context.getString(R.string.import_server, "vpn.invalid"), confirmMessage(a))
        assertNull("There is nothing to replace, so no alternative", onMain { root(a).findViewWithTag<View?>("confirm:alt") })
        answerYes(a)
        eventually("It is added under its own name") { saved().map { it.getString("name") } == listOf("Profile 0", "Profile 1", "Another name") }
    }

    // Delete asks with the name cut in the middle to 36, as the desktop's does, in an overlay of its own, and Cancel keeps it.
    @Test fun theDeleteConfirmationNamesTheConfigCutInTheMiddleAndCancelKeepsIt() {
        val long = "A".repeat(30) + "—" + "Z".repeat(30)
        val profiles = org.json.JSONArray()
            .put(org.json.JSONObject().put("name", long).put("toml", source))
            .put(org.json.JSONObject().put("name", "Second").put("toml", source))
        assertTrue(ConfigStore.write(prefs, profiles.toString()))
        val a = open()
        render(a, 1)
        onMain { method("confirmRemove", Int::class.javaPrimitiveType).invoke(a, 0) }
        instrumentation.waitForIdleSync()
        assertEquals(context.getString(R.string.delete_config_question, ShownText.elideMiddle(long, 36)), confirmMessage(a))
        assertEquals(context.getString(R.string.text_delete), onMain { (root(a).findViewWithTag<View>("confirm:ok") as TextView).text.toString() })
        click(a, "confirm:cancel")
        eventually("Cancel closes it") { overlayTag(a, "confirm") == null }
        assertEquals("Cancel keeps it", 2, saved().size)
        onMain { method("confirmRemove", Int::class.javaPrimitiveType).invoke(a, 0) }
        instrumentation.waitForIdleSync()
        answerYes(a)
        eventually("Delete removes it") { saved().size == 1 }
        assertEquals("Second", saved()[0].getString("name"))
    }

    // The editor asks before it drops what was typed, whichever way out is taken, and asks once; Cancel keeps what was typed.
    // The window is secure while it is up: the TOML holds the password.
    @Test fun theEditorAsksBeforeItDropsWhatWasTypedAndKeepsTheWindowSecureWhileItIsOpen() {
        val a = open()
        render(a, 1)
        fun secure() = onMain { a.window.attributes.flags and WindowManager.LayoutParams.FLAG_SECURE != 0 }
        assertFalse(secure())
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, -1) }
        instrumentation.waitForIdleSync()
        assertTrue("The window is secure while the editor is open", secure())
        assertTrue(shows(a, context.getString(R.string.text_new_configuration)))
        // Nothing typed: Back closes it at once.
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        eventually("An unchanged editor closes at once") { overlayTag(a, "editor") == null }
        assertNull("and asks nothing", overlayTag(a, "confirm"))
        assertFalse("The window is not secure once it is closed", secure())
        // Something typed.
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 0) }
        instrumentation.waitForIdleSync()
        assertTrue(shows(a, context.getString(R.string.text_edit_configuration)))
        val name = byKey(a, "editor:name") as EditText
        onMain { name.setText("Typed") }
        for (way in listOf("the Back key", "editor:cancel", "editor:back")) {
            if (way == "the Back key") instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK) else click(a, way)
            eventually("$way must ask first") { confirmMessage(a) == context.getString(R.string.editor_discard_question) }
            assertNotNull("and the editor is still up", overlayTag(a, "editor"))
            assertTrue(secure())
            // Asked again while the question is up or waits, it is not asked twice.
            click(a, "editor:cancel")
            click(a, "confirm:cancel")
            SystemClock.sleep(300)
            assertNull("one question only, the answer to which was no", overlayTag(a, "confirm"))
            assertEquals("Cancel keeps what was typed", "Typed", onMain { name.text.toString() })
        }
        click(a, "editor:cancel")
        eventually("The editor asks") { confirmMessage(a) == context.getString(R.string.editor_discard_question) }
        assertEquals(context.getString(R.string.editor_discard), onMain { (root(a).findViewWithTag<View>("confirm:ok") as TextView).text.toString() })
        answerYes(a)
        eventually("Discard closes the editor") { overlayTag(a, "editor") == null }
        assertFalse(secure())
        assertEquals("Nothing was saved", 2, saved().size)
        assertEquals("Profile 0", saved().first().getString("name"))
    }

    // The app picker is a card: a search box, rows of a name over a package, no checkboxes, no icons. The search box has the focus
    // first, on a phone and on a TV. A row adds that one application and closes the picker. While it looks,
    // it says so; typed into the box, a name or a package narrows the list, and what matches nothing says that.
    @Test fun theAppPickerAddsOneApplicationAndCloses() {
        val a = open()
        if (isTv) leaveTouchMode(a)
        render(a, 2)
        if (isTv) focusOn(a, "apps:choose")
        // Held back, the scan shows what it is doing.
        val gate = java.util.concurrent.CountDownLatch(1)
        (field("scanWorker").get(a) as ExecutorService).execute { gate.await() }
        click(a, "apps:choose")
        instrumentation.waitForIdleSync()
        try {
            assertEquals(context.getString(R.string.picker_scanning), onMain { (root(a).findViewWithTag<View>("picker:status") as TextView).text.toString() })
        } finally {
            gate.countDown()
        }
        eventually("The installed apps must be listed") { onMain { root(a).findViewWithTag<View?>("picker:0") != null } }
        // The search box has the focus first, on a phone and on a TV, as the desktop's has, and no row is highlighted; its placeholder stays
        // while it has the focus, as the desktop's does. Down goes on to the rows.
        eventually("The search box has the focus first") { focusedKey(a) == "picker:search" }
        assertEquals(context.getString(R.string.picker_search), onMain { (root(a).findViewWithTag<View>("picker:search") as EditText).hint.toString() })
        if (isTv) {
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_DOWN)
            eventually("Down goes on to the first row") { focusedKey(a) == "picker:0" }
        }
        // A name over a package, and nothing else.
        val first = onMain {
            val layer = root(a).findViewWithTag<View>("overlay:picker")
            assertTrue("No checkboxes", descendants(layer).none { it is android.widget.CompoundButton })
            // The one picture on the card is the ← before the title (the desktop draws it as a glyph of text); a row has none of an application's.
            assertTrue("No icons", descendants(layer).filterIsInstance<ImageView>().all { it.tag == "picker:back" })
            assertTrue("and none in a row", descendants(layer.findViewWithTag<View>("picker:0")).none { it is ImageView })
            val texts = descendants(layer.findViewWithTag<View>("picker:0")).filterIsInstance<TextView>().toList()
            assertEquals("A name and a package", 2, texts.size)
            assertEquals("The package is cut at its left", TextUtils.TruncateAt.START, texts[1].ellipsize)
            // 1 between the name and the package, as the desktop's Column (spacing: 1) has, and the header's row 2 lower in its 48.
            assertEquals("1 between them", Math.round(a.resources.displayMetrics.density), (texts[1].layoutParams as ViewGroup.MarginLayoutParams).topMargin)
            val header = layer.findViewWithTag<View>("picker:back").parent as View
            assertEquals("The header is 48 high", dp(a, 48f), header.height.toFloat(), dp(a, 1f))
            assertEquals("with 4 above the row: its middle is 26 down", Math.round(4 * a.resources.displayMetrics.density), header.paddingTop)
            assertEquals("and the card has no Done and no file to choose", 0, descendants(layer).filterIsInstance<Button>().count())
            texts[1].text.toString()
        }
        val search = byKey(a, "picker:search") as EditText
        onMain { search.setText("zzzzzz-no-such-app") }
        instrumentation.waitForIdleSync()
        assertEquals(context.getString(R.string.picker_nothing), onMain { (root(a).findViewWithTag<View>("picker:status") as TextView).text.toString() })
        // A package narrows it as a name does.
        onMain { search.setText(first) }
        instrumentation.waitForIdleSync()
        val row = onMain {
            descendants(root(a).findViewWithTag<View>("overlay:picker")).first { v ->
                (v.tag as? String)?.matches(Regex("picker:\\d+")) == true && descendants(v).filterIsInstance<TextView>().any { it.text.toString() == first }
            }.tag as String
        }
        click(a, row)
        eventually("One application adds itself and closes the picker") { overlayTag(a, "picker") == null }
        if (isTv) eventually("The successful save returns focus to Choose") { focusedKey(a) == "apps:choose" }
        assertTrue("It is in the apps the VPN carries or leaves", profiles().apps(profiles().edited()).contains(first))
        assertTrue("and in the old list 1.0.0 reads, which copies the profile the selected config uses", prefs.getStringSet("apps", emptySet())!!.contains(first))
        assertEquals("The page does not change", 2, onMain { field("page").getInt(a) })
    }

    // The picker is a card 460 high at most, and not the length of the window (it is shorter where a keyboard takes room), and says so
    // while it is still looking.
    @Test fun thePickerIsNeverTallerThan460() {
        val a = open()
        render(a, 3)
        fun height(tag: String) = onMain { (root(a).findViewWithTag<View>("overlay:$tag") as OverlayLayer).panel().height }
        onMain { method("appPicker").invoke(a) }
        instrumentation.waitForIdleSync()
        assertTrue("The picker is 460 at most: ${height("picker")}", height("picker") <= dp(a, 460f) + dp(a, 1.5f))
        closeWithBack(a, "picker")
    }

    // A theme and a mode are chosen in select popups, under their control: the current one first, and the focus back on the
    // control after.
    @Test fun theThemeAndTheModeAreChosenInSelectPopups() {
        val a = open()
        leaveTouchMode(a)
        render(a, 3)
        ok(a, "settings:theme")
        fun rows() = onMain {
            descendants(root(a).findViewWithTag<View>("overlay:select")).filterIsInstance<TextView>().map { it.text.toString() }.toList()
        }
        val names = listOf(R.string.text_theme_system, R.string.text_light_78, R.string.text_dark_79).map { context.getString(it) }
        assertEquals(listOf(names[0], names[1], names[2]), rows())
        eventually("The current choice has the focus") { focusedKey(a) == "select:0" }
        click(a, "select:2")
        eventually("Dark is the theme") { prefs.getString("theme_mode", null) == "dark" }
        eventually("The popup closes") { overlayTag(a, "select") == null }
        eventually("The focus is back on the Theme row") { focusedKey(a) == "settings:theme" }
        render(a, 2)
        ok(a, "split:mode")
        assertEquals(listOf(context.getString(R.string.text_bypass_vpn_61), context.getString(R.string.text_through_vpn_62)), rows())
        click(a, "select:1")
        eventually("Through VPN is the mode") { prefs.getBoolean("through", false) }
        eventually("The popup closes") { overlayTag(a, "select") == null }
    }

    // A storage that fails is the desktop's amber banner at the top of Configs and in Settings' Security, and a toast: not a dialog.
    @Test fun aStorageFailureIsTheDesktopsAmberBannerAndAToast() {
        val a = open()
        onMain { field("storageFailed").setBoolean(a, true); method("rootStorageWarning").invoke(a) }
        val problem = context.getString(R.string.text_configurations_are_unavailable) + ". " +
            context.getString(R.string.text_android_could_not_read_storage_saved_data_is_unchang)
        assertEquals(listOf(problem), toastTexts(a))
        for (page in listOf(1, 3)) {
            render(a, page)
            val banner = onMain { root(a).findViewWithTag<TextView?>("storage:banner") }
            assertNotNull("The banner is on page $page", banner)
            assertEquals(problem, onMain { banner!!.text.toString() })
        }
        // Adding is refused, and opens nothing.
        render(a, 1)
        onMain { method("addMenu").invoke(a) }
        assertNull(overlayTag(a, "addmenu"))
        onMain { field("storageFailed").setBoolean(a, false); field("storageProblem").set(a, null) }
        render(a, 1)
        assertNull("The banner goes with the failure", onMain { root(a).findViewWithTag<View?>("storage:banner") })
    }

    // What used to be a one-button dialog is a toast: a refusal, whatever its length, and nothing is asked.
    @Test fun aRefusalIsAToastNotADialog() {
        val a = open()
        render(a, 1)
        onMain { method("refuseImport", Int::class.javaPrimitiveType).invoke(a, R.string.text_subscription_links_are_not_supported_yet) }
        assertEquals(listOf(context.getString(R.string.text_subscription_links_are_not_supported_yet)), toastTexts(a))
        assertNull("and no overlay is up", overlayTag(a, "confirm"))
        onMain { method("refuseImport", Int::class.javaPrimitiveType).invoke(a, R.string.text_this_server_needs_a_newer_trusttunnel_core) }
        assertEquals("a new one replaces the old", listOf(context.getString(R.string.text_this_server_needs_a_newer_trusttunnel_core)), toastTexts(a))
    }

    // ---- Split: profiles, each config's profile, Recommended for Russia (qml/pages/SplitPage.qml) ----

    private fun splitStore() = SplitPrefs(prefs)
    private fun profiles(): SplitProfiles = SplitStore.load(splitStore(), null)
    private fun editProfiles(change: (SplitProfiles) -> Unit) { SplitStore.edit(splitStore(), null) { p -> change(p) } }
    // The label of a chip, by the key of the chip itself (a profile's) or of the X in it (a rule's, an application's, a route's).
    private fun chipLabel(a: MainActivity, key: String): String = onMain {
        val view = root(a).findViewWithTag<View>(key)
        ((if (view is ImageView) view.parent else view) as ViewGroup).getChildAt(0).let { (it as TextView).text.toString() }
    }
    private fun pageKeys(a: MainActivity): Set<String> = onMain {
        descendants(pageRoot(a)).filter { it.isShown }.mapNotNull { it.tag as? String }.toSet()
    }
    // The keys of every control the arrows reach from a tab, one press at a time.
    private fun reachable(a: MainActivity, fromKey: String): Set<String> = onMain {
        val content = root(a)
        val seen = HashSet<View>()
        val queue = ArrayDeque<View>().apply { add(content.findViewWithTag<View>(fromKey)) }
        while (queue.isNotEmpty()) {
            val view = queue.removeFirst()
            if (!seen.add(view)) continue
            for (direction in listOf(View.FOCUS_UP, View.FOCUS_DOWN, View.FOCUS_LEFT, View.FOCUS_RIGHT))
                FocusFinder.getInstance().findNextFocus(content, view, direction)?.let { queue.add(it) }
        }
        seen.mapNotNull { it.tag as? String }.toSet()
    }
    // Types into a page field and presses its Enter, as a keyboard's Done does.
    private fun enter(a: MainActivity, key: String, text: String) = onMain {
        val field = root(a).findViewWithTag<EditText>(key)
        field.setText(text)
        field.onEditorAction(EditorInfo.IME_ACTION_DONE)
    }
    private fun fieldText(a: MainActivity, key: String): String = onMain { root(a).findViewWithTag<EditText>(key).text.toString() }
    // The settings the Split page is about, as the tests need them: on, which way, and the notice's rules.
    private fun splitOn(through: Boolean) { prefs.edit().putBoolean("split_enabled", true).putBoolean("through", through).commit() }
    private fun refuseSplitSaves(a: MainActivity) {
        field("splitPrefs").set(a, SplitPrefs(preferencesThatFailCommits(1)))
    }
    private fun preferencesThatFailCommits(count: Int): SharedPreferences {
        val remainingFailures = java.util.concurrent.atomic.AtomicInteger(count)
        return java.lang.reflect.Proxy.newProxyInstance(
            TvDeviceTest::class.java.classLoader, arrayOf(SharedPreferences::class.java)
        ) { _, method, args ->
            val result = method.invoke(prefs, *(args ?: emptyArray()))
            if (method.name != "edit") result
            else java.lang.reflect.Proxy.newProxyInstance(
                TvDeviceTest::class.java.classLoader, arrayOf(SharedPreferences.Editor::class.java)
            ) { _, editorMethod, editorArgs ->
                val editor = result as SharedPreferences.Editor
                if (editorMethod.name == "commit" && remainingFailures.get() > 0) {
                    remainingFailures.decrementAndGet()
                    editorMethod.invoke(editor, *(editorArgs ?: emptyArray()))
                    false
                } else editorMethod.invoke(editor, *(editorArgs ?: emptyArray()))
            }
        } as SharedPreferences
    }
    private fun seedConfigs(vararg uses: String?) {
        val list = org.json.JSONArray()
        uses.forEachIndexed { i, profile ->
            val config = org.json.JSONObject().put("name", "Profile $i").put("toml", source)
            if (profile != null) config.put("profile", profile)
            list.put(config)
        }
        assertTrue(ConfigStore.write(prefs, list.toString()))
    }

    // An update from 1.0.0: its one list of domains and one of applications become Default's, sorted, and are what the page shows.
    @Test fun theOldListsOfTheFirstVersionBecomeDefaultAndAreWhatThePageShows() {
        val a = open()
        assertEquals(listOf("Default"), profiles().names())
        assertEquals(listOf("example.com", "example.org"), profiles().domains("Default"))
        render(a, 2)
        assertEquals("example.com", chipLabel(a, "domain:0"))
        assertEquals("example.org", chipLabel(a, "domain:1"))
        assertEquals("Profile, Default, edited", context.getString(R.string.split_profile_default), chipLabel(a, "profile:0"))
        assertFalse("Default has no X", pageKeys(a).contains("profile:0:remove"))
        assertFalse("Only its one profile", pageKeys(a).contains("profile:1"))
        // 1.0.0's lists are still there, a copy of the profile the selected config uses.
        assertEquals(setOf("example.com", "example.org"), prefs.getStringSet("domains", emptySet()))
    }

    // A new installation, with nothing saved: split tunnelling on, and Default holds the recommended list, as the desktop's does.
    @Test fun aNewInstallationHasSplitOnAndDefaultHoldsTheRecommendedList() {
        prefs.edit().clear().commit()
        val a = open()
        val rules = RussiaList.read(context)
        assertTrue(rules.size > 50)
        assertTrue("Split tunnelling is on", prefs.getBoolean("split_enabled", false))
        assertFalse("and it is Bypass VPN", prefs.getBoolean("through", false))
        assertEquals(rules, profiles().domains("Default"))
        render(a, 2)
        assertEquals(rules[0], chipLabel(a, "domain:0"))
        assertEquals(rules.last(), chipLabel(a, "domain:${rules.size - 1}"))
        assertFalse(pageKeys(a).contains("domain:${rules.size}"))
        assertTrue(onMain { (root(a).findViewWithTag<ToggleView>("split:enabled")).isChecked })
        // Opened again, it is not made again.
        onMain { a.finish() }
        editProfiles { it.clearDomains("Default") }
        val again = open()
        assertTrue("Cleared, it stays cleared", profiles().domains("Default").isEmpty())
        render(again, 2)
        assertFalse(pageKeys(again).contains("domain:0"))
    }

    @Test fun thePageIsTheDesktopsInItsOrderAndTheEditedProfileIsTheOneInAccent() {
        editProfiles { p -> p.add("Work"); p.add("Home"); p.addDomains("Home", "home.example") }
        val a = open()
        leaveTouchMode(a)
        render(a, 2)
        // Top to bottom: the switch, the mode, Profile with its chips, the rules and the applications.
        // The page is a column, and its rows and flows are in the order they read: the views are in the order they are shown.
        val order = onMain { descendants(pageRoot(a)).filter { it.isShown && it.tag is String }.map { it.tag as String }.toList() }
        val expected = listOf("split:enabled", "split:mode", "profile:0", "profile:1", "profile:2", "profile:add", "domains:russia", "domains:clear")
        val positions = expected.map { order.indexOf(it) }
        assertTrue("$expected must come in this order, but are at $positions in $order", positions.none { it < 0 } && positions == positions.sorted())
        assertEquals(listOf(context.getString(R.string.split_profile_default), "Work", "Home"),
            listOf(chipLabel(a, "profile:0"), chipLabel(a, "profile:1"), chipLabel(a, "profile:2")))
        // The edited one is the accent with accentText whatever drives the screen, and has no mark after its name; a chip is surface and
        // goes to surfaceHover with the focus, and the edited one, which the desktop leaves as it is, a step darker.
        onMain {
            val theme = theme(a)
            val edited = focusLook(root(a).findViewWithTag<View>("profile:2"))
            assertEquals("The edited chip is the accent", theme.accent, edited.restFill)
            assertEquals("and a step darker with the focus", theme.primaryHover, edited.focusedFill)
            assertEquals(theme.accentText, (root(a).findViewWithTag<ViewGroup>("profile:2").getChildAt(0) as TextView).textColors.defaultColor)
            val other = focusLook(root(a).findViewWithTag<View>("profile:1"))
            assertEquals(theme.surface, other.restFill)
            assertEquals("and a chip focuses as it is under the pointer", theme.surfaceHover, other.focusedFill)
            val x = root(a).findViewWithTag<ImageView>("profile:2:remove")
            assertEquals("The X of the edited chip is accentText", theme.accentText, x.imageTintList!!.getColorForState(intArrayOf(), 0))
            assertEquals("and danger with the focus", theme.danger, x.imageTintList!!.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
        }
        assertTrue("Default has no X, the others have one each", pageKeys(a).containsAll(listOf("profile:1:remove", "profile:2:remove")) && !pageKeys(a).contains("profile:0:remove"))
        assertTrue(shows(a, context.getString(R.string.text_rules_bypass_vpn)))
        assertTrue(shows(a, context.getString(R.string.text_applications_bypass_vpn)))
        assertEquals("Home's rules, not Default's, are on the page", "home.example", chipLabel(a, "domain:0"))
    }

    @Test fun theArrowsReachEveryChipAndItsXAndTheRuleControls() {
        editProfiles { p ->
            p.add("Work"); p.addDomains("Work", "work.example")
            p.add("Home"); p.addDomains("Home", "a.example b.example c.example"); p.addApp("Home", "not.installed.package")
        }
        val a = open()
        leaveTouchMode(a)
        render(a, 2)
        val expected = setOf("profile:0", "profile:1", "profile:1:remove", "profile:2", "profile:2:remove", "profile:add",
            "domains:russia", "domains:clear", "domain:0", "domain:1", "domain:2", "domains:input", "apps:choose", "apps:clear", "app:0", "apps:input")
        assertTrue("These must be on the page: ${expected - pageKeys(a)}", pageKeys(a).containsAll(expected))
        val reached = reachable(a, "tab:2")
        assertEquals("Controls the arrows never reach", emptySet<String>(), expected - reached)
        // Right from a chip is its X, Left from the X is the chip; Right from the X is the next chip.
        focusOn(a, "profile:1")
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_RIGHT)
        eventually("Right from a chip reaches its X") { focusedKey(a) == "profile:1:remove" }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_LEFT)
        eventually("Left comes back to the chip") { focusedKey(a) == "profile:1" }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_RIGHT)
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_RIGHT)
        eventually("and the X's Right is the next chip") { focusedKey(a) == "profile:2" }
    }

    // OK on a chip chooses it to edit, and only that: nothing is built again, and the rules on the page are its.
    @Test fun choosingAProfileChangesOnlyWhichOneIsEditedAndRestartsNothing() {
        editProfiles { p -> p.add("Work"); p.addDomains("Work", "work.example"); p.select("Default") }
        splitOn(false)
        val a = open()
        leaveTouchMode(a)
        render(a, 2)
        val gate = java.util.concurrent.CountDownLatch(1)
        (field("validationWorker").get(a) as ExecutorService).execute { gate.await() }
        onMain { field("vpnState").setInt(a, 2) }
        serviceState(2)
        try {
            val before = onMain { field("validationRequest").getLong(a) }
            focusOn(a, "profile:1")
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_CENTER)
            eventually("OK edits Work") { profiles().edited() == "Work" }
            eventually("and the page shows Work's rules") { onMain { root(a).findViewWithTag<View?>("domain:0") } != null && chipLabel(a, "domain:0") == "work.example" }
            eventually("with the focus kept on the chip") { focusedKey(a) == "profile:1" }
            assertEquals("The selected config uses Default: a VPN that is up is not built again", before, onMain { field("validationRequest").getLong(a) })
            assertEquals("It uses Default still", "Default", onMain { method("activeProfileName").invoke(a) as String })
        } finally {
            serviceState(0)
            onMain { field("vpnState").setInt(a, 0) }
            gate.countDown()
        }
    }

    // A rule of the profile the selected config uses is built into a session that is up; a rule of another profile is not.
    @Test fun aRuleOfTheProfileInUseRebuildsTheVpnAndOneOfAnotherProfileDoesNot() {
        editProfiles { p -> p.add("Work") }
        splitOn(false)
        val a = open()
        render(a, 2)
        val gate = java.util.concurrent.CountDownLatch(1)
        (field("validationWorker").get(a) as ExecutorService).execute { gate.await() }
        onMain { field("vpnState").setInt(a, 2) }
        serviceState(2)
        try {
            fun requests() = onMain { field("validationRequest").getLong(a) }
            val before = requests()
            enter(a, "domains:input", "work.example")
            eventually("The rule is added to Work") { profiles().domains("Work") == listOf("work.example") }
            assertEquals("Work is used by no config: nothing is rebuilt", before, requests())
            click(a, "profile:0")
            eventually("Default is edited") { profiles().edited() == "Default" }
            assertEquals("choosing a profile rebuilds nothing", before, requests())
            enter(a, "domains:input", "home.example")
            eventually("The rule is added to Default") { profiles().domains("Default").contains("home.example") }
            assertEquals("Default is the selected config's profile: the VPN is built again", before + 1, requests())
        } finally {
            serviceState(0)
            onMain { field("vpnState").setInt(a, 0) }
            gate.countDown()
        }
    }

    // Split tunnelling off changes nothing in what is built, so a rule edit rebuilds nothing then either.
    @Test fun withSplitTunnellingOffARuleRebuildsNothing() {
        prefs.edit().putBoolean("split_enabled", false).commit()
        val a = open()
        render(a, 2)
        val gate = java.util.concurrent.CountDownLatch(1)
        (field("validationWorker").get(a) as ExecutorService).execute { gate.await() }
        onMain { field("vpnState").setInt(a, 2) }
        serviceState(2)
        try {
            val before = onMain { field("validationRequest").getLong(a) }
            enter(a, "domains:input", "home.example")
            eventually("The rule is added") { profiles().domains("Default").contains("home.example") }
            assertEquals(before, onMain { field("validationRequest").getLong(a) })
        } finally {
            serviceState(0)
            onMain { field("vpnState").setInt(a, 0) }
            gate.countDown()
        }
    }

    // + opens the field for a name, OK on it and Enter add the profile, and the focus goes back to +.
    @Test fun thePlusChipOpensTheNameFieldAndEnterAddsAProfile() {
        val a = open()
        leaveTouchMode(a)
        render(a, 2)
        onMain {
            val field = root(a).findViewWithTag<EditText>("profile:name")
            assertEquals("The field is there, and hidden", View.GONE, field.visibility)
            assertEquals(context.getString(R.string.split_profile_name_hint), field.hint.toString())
            assertEquals("At most 16 characters", SplitProfiles.MAX_NAME, field.filters.filterIsInstance<InputFilter.LengthFilter>().single().max)
        }
        focusOn(a, "profile:add")
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_CENTER)
        eventually("OK on + shows the field, with the focus") { focusedKey(a) == "profile:name" && onMain { root(a).findViewWithTag<View>("profile:name").visibility } == View.VISIBLE }
        enter(a, "profile:name", "Work")
        eventually("Enter adds the profile") { profiles().names() == listOf("Default", "Work") }
        assertEquals("and it is the one being edited", "Work", profiles().edited())
        eventually("The field is gone, and the focus is back on +") { focusedKey(a) == "profile:add" }
        assertEquals(View.GONE, onMain { root(a).findViewWithTag<View>("profile:name").visibility })
        assertEquals("Work", chipLabel(a, "profile:1"))
        assertEquals("and the rules on the page are Work's: none", emptyList<String>(), pageKeys(a).filter { it.startsWith("domain:") })
        assertEquals("and the configs use what they used", "Default", onMain { method("activeProfileName").invoke(a) as String })
    }

    @Test fun escapeAndGoingToAnotherControlDropTheNameFieldAndBackIsTheFirstToClose() {
        val a = open()
        leaveTouchMode(a)
        render(a, 2)
        fun openField() {
            focusOn(a, "profile:add")
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_CENTER)
            eventually("The field opens") { focusedKey(a) == "profile:name" }
            // An arrow looks for the next control from where the field is, so from where it has been laid out.
            eventually("The field is laid out") { onMain { root(a).findViewWithTag<View>("profile:name").let { it.isLaidOut && it.height > 0 } } }
            instrumentation.waitForIdleSync()
        }
        fun visibility() = onMain { root(a).findViewWithTag<View>("profile:name").visibility }
        openField()
        onMain { root(a).findViewWithTag<EditText>("profile:name").setText("Half") }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_ESCAPE)
        eventually("Escape drops it") { visibility() == View.GONE }
        eventually("with the focus back on +") { focusedKey(a) == "profile:add" }
        assertEquals("and adds nothing", listOf("Default"), profiles().names())
        openField()
        assertEquals("Opened again it is empty", "", fieldText(a, "profile:name"))
        // Up leaves a single line of text for the control above it. A page that scrolls takes the first press to scroll to that control when
        // it is out of reach of one arrow, and then the next goes to it: so up to three presses are made, and the field is gone after the first
        // that moves the focus.
        repeat(3) {
            if (visibility() != View.GONE) {
                instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_UP)
                instrumentation.waitForIdleSync()
                SystemClock.sleep(150)
            }
        }
        try {
            eventually("Going to another control drops it") { visibility() == View.GONE }
        } catch (e: AssertionError) {
            // What the arrow had to go on.
            val where = onMain {
                val field = root(a).findViewWithTag<View>("profile:name")
                "the field is at ${field.left},${field.top},${field.right},${field.bottom}, laid out ${field.isLaidOut}, focused ${field.isFocused}; " +
                    "touch mode ${a.window.decorView.isInTouchMode}; the focus is on ${a.currentFocus?.tag}"
            }
            throw AssertionError("${e.message} ($where)", e)
        }
        assertNotEquals("and the focus is where the arrow took it", "profile:name", focusedKey(a))
        assertEquals(listOf("Default"), profiles().names())
        // Back on a phone with its keyboard up only puts the keyboard away; a TV has none to put away.
        if (isTv) {
            openField()
            instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
            eventually("Back closes it first") { visibility() == View.GONE }
            assertEquals("and does not leave the page", 2, onMain { field("page").getInt(a) })
            assertFalse(onMain { a.isFinishing })
        }
    }

    @Test fun aNameThatDiffersOnlyInCaseIsRefusedWithTheDesktopsWords() {
        editProfiles { it.add("Work") }
        val a = open()
        leaveTouchMode(a)
        render(a, 2)
        focusOn(a, "profile:add")
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_CENTER)
        eventually("The field opens") { focusedKey(a) == "profile:name" }
        enter(a, "profile:name", "work")
        eventually("It says why") { context.getString(R.string.split_profile_exists, "Work") in toastTexts(a) }
        assertEquals("and adds nothing", listOf("Default", "Work"), profiles().names())
        assertEquals("The field is closed", View.GONE, onMain { root(a).findViewWithTag<View>("profile:name").visibility })
    }

    // A deletion's last chip, a Clear all and a profile's X take away the control that has a remote's focus. The focus goes to what is beside
    // where it was, and not to the page's tab at the top while the page stays scrolled to where the control was.
    @Test fun theFocusStaysBesideAControlThatGoes() {
        prefs.edit().putString(ExcludedRoutes.KEY, ExcludedRoutes.toJson(listOf("1.2.3.0/24", "10.0.0.0/8"))).commit()
        editProfiles { p -> p.add("Work"); p.addDomains("Work", "one.example"); p.addApp("Work", "not.installed.package") }
        val a = open()
        leaveTouchMode(a)
        render(a, 2)
        // The only rule, taken out with OK on its X, and the only application.
        ok(a, "domain:0")
        eventually("The rule is gone") { profiles().domains("Work").isEmpty() }
        eventually("and the focus is on the link above the chips, not on the tab") { focusedKey(a) == "domains:russia" }
        ok(a, "app:0")
        eventually("The application is gone") { profiles().apps("Work").isEmpty() }
        eventually("and the focus is on Choose…") { focusedKey(a) == "apps:choose" }
        // Clear all takes itself away with the rules.
        ok(a, "domains:russia")
        eventually("The recommended list is added") { profiles().domains("Work").size > 50 }
        ok(a, "domains:clear")
        eventually("Clear all asks") { confirmMessage(a) == context.getString(R.string.split_clear_domains_question) }
        answerYes(a)
        eventually("The rules are cleared") { profiles().domains("Work").isEmpty() }
        eventually("and the focus is on the link beside it") { focusedKey(a) == "domains:russia" }
        // The only custom profile: the chip before it.
        ok(a, "profile:1:remove")
        eventually("Delete asks") { confirmMessage(a) == context.getString(R.string.split_profile_delete_question, "Work") }
        answerYes(a)
        eventually("The profile is gone") { profiles().names() == listOf("Default") }
        eventually("and the focus is on the chip before it") { focusedKey(a) == "profile:0" }
        // Settings: the routes.
        render(a, 3)
        ok(a, "route:0")
        eventually("A route is gone") { savedRoutes() == listOf("10.0.0.0/8") }
        ok(a, "route:0")
        eventually("and the other") { savedRoutes().isEmpty() }
        eventually("and the focus is on Restore defaults") { focusedKey(a) == "routes:restore" }
        ok(a, "routes:restore")
        eventually("Restore asks") { confirmMessage(a) != null }
        answerYes(a)
        eventually("The defaults are back") { savedRoutes() == defaultRoutes() }
        ok(a, "routes:clear")
        eventually("Clear all asks") { confirmMessage(a) == context.getString(R.string.settings_routes_clear_question) }
        answerYes(a)
        eventually("The routes are cleared") { savedRoutes().isEmpty() }
        eventually("and the focus is on Restore defaults") { focusedKey(a) == "routes:restore" }
    }

    // The last config goes: the empty page has its words, "Add a config", in the place of the list, and the focus goes there.
    @Test fun theFocusGoesToTheWordsOfAnEmptyConfigsPageWhenTheLastConfigGoes() {
        seed(1)
        val a = open()
        leaveTouchMode(a)
        render(a, 1)
        ok(a, "config:0:remove")
        eventually("Delete asks") { confirmMessage(a) != null }
        answerYes(a)
        eventually("The config is gone") { saved().isEmpty() }
        eventually("and the focus is on the words that stand for the list") { focusedKey(a) == "configs:hint" }
    }

    // The empty page's words are hidden while their own menu is open, and take the focus back when it closes, however it closes: a view that
    // is not shown takes none, and the focus left in the layer that is fading would go to the Home tab when it is taken away.
    @Test fun theWordsOfAnEmptyPageGetTheFocusBackFromTheirMenu() {
        seed(0)
        val a = open()
        leaveTouchMode(a)
        render(a, 1)
        ok(a, "configs:hint")
        eventually("The menu opens") { overlayTag(a, "addmenu") != null }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        eventually("Back closes it") { overlayTag(a, "addmenu") == null }
        eventually("and the words have the focus again") { focusedKey(a) == "configs:hint" }
        // A row that opens nothing: the clipboard is empty.
        onMain { (a.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager).clearPrimaryClip() }
        ok(a, "configs:hint")
        eventually("The menu opens again") { overlayTag(a, "addmenu") != null }
        click(a, "add:paste")
        eventually("The row closes it") { overlayTag(a, "addmenu") == null }
        eventually("and the focus is on the words, after the notice too") { focusedKey(a) == "configs:hint" }
        // Once the layer is gone, the focus is where it was: not on the tab at the top.
        SystemClock.sleep(600)
        assertEquals("configs:hint", focusedKey(a))
    }

    // A profile is deleted after a question; the configs that used it use Default from then on, which is also what the VPN is built with.
    @Test fun deletingAProfileAsksFirstAndSendsItsConfigsBackToDefault() {
        editProfiles { p -> p.add("Work"); p.addDomains("Work", "work.example") }
        seedConfigs(null, "Work")
        val a = open()
        render(a, 2)
        click(a, "profile:1:remove")
        eventually("It asks, naming the profile") { confirmMessage(a) == context.getString(R.string.split_profile_delete_question, "Work") }
        assertEquals(context.getString(R.string.text_delete), onMain { (root(a).findViewWithTag<View>("confirm:ok") as TextView).text.toString() })
        click(a, "confirm:cancel")
        eventually("No keeps it") { overlayTag(a, "confirm") == null }
        assertEquals(listOf("Default", "Work"), profiles().names())
        assertEquals("Work", saved()[1].optString("profile"))
        click(a, "profile:1:remove")
        eventually("It asks again") { confirmMessage(a) != null }
        answerYes(a)
        eventually("Yes deletes it") { profiles().names() == listOf("Default") }
        eventually("and the page shows Default") { pageKeys(a).none { it.startsWith("profile:1") } }
        assertFalse("The config that used it uses Default: none is written", saved()[1].has("profile"))
        assertEquals("Default", profiles().edited())
    }

    @Test fun theOldListsFollowTheProfileOfTheSelectedConfig() {
        editProfiles { p ->
            p.clearDomains("Default"); p.clearApps("Default")
            p.addDomains("Default", "home.example"); p.addApp("Default", "org.home")
            p.add("Work"); p.addDomains("Work", "work.example"); p.addApp("Work", "org.work")
        }
        seedConfigs(null, "Work")
        val a = open()
        assertEquals("Config 0 uses Default", setOf("home.example"), prefs.getStringSet("domains", emptySet()))
        onMain { method("selectConfig", Int::class.javaPrimitiveType).invoke(a, 1) }
        assertEquals("Selecting the config that uses Work moves them", setOf("work.example"), prefs.getStringSet("domains", emptySet()))
        assertEquals(setOf("org.work"), prefs.getStringSet("apps", emptySet()))
        render(a, 2)
        enter(a, "domains:input", "more.example")
        eventually("A rule of the profile edited and in use is in them") { prefs.getStringSet("domains", emptySet()) == setOf("work.example", "more.example") }
    }

    // Recommended for Russia adds the list to the profile being edited, those it does not have, at the end, and a second time nothing.
    @Test fun recommendedForRussiaAddsTheSharedListOnceAndClearAllAsksFirst() {
        editProfiles { p -> p.add("Work"); p.addDomains("Work", "mine.example *.yandex.ru") }
        val a = open()
        leaveTouchMode(a)
        render(a, 2)
        val rules = RussiaList.read(context)
        click(a, "domains:russia")
        val expected = listOf("mine.example", "*.yandex.ru") + rules.filter { it != "*.yandex.ru" }
        eventually("It is added, without what the profile has") { profiles().domains("Work") == expected }
        assertEquals("Default is untouched", listOf("example.com", "example.org"), profiles().domains("Default"))
        assertEquals(expected.last(), chipLabel(a, "domain:${expected.size - 1}"))
        click(a, "domains:russia")
        SystemClock.sleep(300)
        assertEquals("A second time there is nothing new", expected, profiles().domains("Work"))
        // Clear all asks.
        click(a, "domains:clear")
        eventually("It asks") { confirmMessage(a) == context.getString(R.string.split_clear_domains_question) }
        assertEquals(context.getString(R.string.text_clear), onMain { (root(a).findViewWithTag<View>("confirm:ok") as TextView).text.toString() })
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        eventually("Back says no") { overlayTag(a, "confirm") == null }
        assertEquals(expected, profiles().domains("Work"))
        click(a, "domains:clear")
        eventually("It asks again") { confirmMessage(a) != null }
        answerYes(a)
        eventually("Clear empties the list") { profiles().domains("Work").isEmpty() }
        eventually("and the link goes with it") { !pageKeys(a).contains("domains:clear") }
    }

    // One chip's X takes one rule at once, with nothing asked.
    @Test fun aRuleChipsXRemovesItAtOnce() {
        val a = open()
        render(a, 2)
        click(a, "domain:0")
        eventually("The rule is gone") { profiles().domains("Default") == listOf("example.org") }
        assertNull("and nothing was asked", overlayTag(a, "confirm"))
        eventually("and the page shows one chip") { pageKeys(a).contains("domain:0") && !pageKeys(a).contains("domain:1") }
        assertEquals("example.org", chipLabel(a, "domain:0"))
    }

    // Words are added if they are rules, and the ones that are not are named, in the desktop's words with its reasons.
    @Test fun domainsAreAddedAndTheWordsThatAreNoRuleAreNamed() {
        val a = open()
        render(a, 2)
        enter(a, "domains:input", "Good.Example, intranet; other.example")
        eventually("The words that are rules are added") { profiles().domains("Default").containsAll(listOf("good.example", "other.example")) }
        assertEquals("lowercased, after what was there", listOf("example.com", "example.org", "good.example", "other.example"), profiles().domains("Default"))
        assertTrue("and the other is named", context.getString(R.string.split_refused, "intranet") in toastTexts(a))
        assertEquals("The field is empty again", "", fieldText(a, "domains:input"))
        // Nothing that is a rule: the toast, and the text stays to be put right.
        enter(a, "domains:input", "0.0.0.0/0")
        eventually("A subnet of every address says why") { context.getString(R.string.split_refused_every, "0.0.0.0/0") in toastTexts(a) }
        assertEquals("0.0.0.0/0", fieldText(a, "domains:input"))
        enter(a, "domains:input", "*.1.2.3.4")
        eventually("A wildcard on an address says why") { context.getString(R.string.split_refused_wildcard, "*.1.2.3.4") in toastTexts(a) }
        assertEquals("and nothing was added", 4, profiles().domains("Default").size)
        enter(a, "domains:input", "good.example")
        SystemClock.sleep(300)
        assertEquals("A rule that is there is not added again, and is no refusal", 4, profiles().domains("Default").size)
    }

    @Test fun aDomainSaveThatStorageRefusesKeepsTheDraftAndDoesNotChangeTheProfile() {
        val a = open()
        render(a, 2)
        val before = profiles().toJson()
        refuseSplitSaves(a)

        enter(a, "domains:input", "not-saved.example")

        assertEquals("The failed save leaves the text to retry", "not-saved.example", fieldText(a, "domains:input"))
        assertEquals("No rule is shown as saved", before, profiles().toJson())
        assertEquals("The legacy mirror is rolled back too", setOf("example.com", "example.org"), prefs.getStringSet("domains", emptySet()))
        assertFalse("Unrelated preferences are not changed", prefs.getBoolean("logging", true))
        assertTrue("The user is told storage refused the change",
            context.getString(R.string.text_storage_is_unavailable_data_has_not_been_changed) in toastTexts(a))
    }

    @Test fun splitPrefsRollsBackAbsentKeysEvenWhenTheRollbackCommitAlsoFails() {
        val absent = "split_test_previously_absent"
        assertFalse(prefs.contains(absent))
        val backing = SplitPrefs(preferencesThatFailCommits(2))

        assertFalse(backing.commit(mapOf(absent to "temporary", "logging" to true)))

        assertFalse("The missing key stays missing in memory", prefs.contains(absent))
        assertFalse("An existing unrelated value is restored", prefs.getBoolean("logging", true))
    }

    @Test fun anApplicationIsListedByItsNameAndOneThatIsGoneByItsPackageAndEitherCanBeRemoved() {
        val installed = context.packageManager.getInstalledApplications(0).map { it.packageName }.first { it != context.packageName }
        editProfiles { p -> p.clearApps("Default"); p.addApp("Default", installed); p.addApp("Default", "not.installed.package") }
        val a = open()
        render(a, 2)
        val pm = context.packageManager
        assertEquals(pm.getApplicationInfo(installed, 0).loadLabel(pm).toString(), chipLabel(a, "app:0"))
        assertEquals("One that is gone shows its package, so that it can be removed", "not.installed.package", chipLabel(a, "app:1"))
        assertNull("The picker is the way in", onMain { root(a).findViewWithTag<View?>("apps:add") })
        click(a, "app:1")
        eventually("Its X removes it") { profiles().apps("Default") == listOf(installed) }
        click(a, "apps:clear")
        eventually("Clear all asks") { confirmMessage(a) == context.getString(R.string.split_clear_apps_question) }
        answerYes(a)
        eventually("and clears") { profiles().apps("Default").isEmpty() }
        eventually("with its link") { !pageKeys(a).contains("apps:clear") }
    }

    // The notice, only where it applies, worked out by the code that builds the VPN: nothing to go through, or apps and domains together.
    @Test fun theNoticeIsThereOnlyWhereThroughVpnWouldRouteNothingOrCannotRouteBoth() {
        editProfiles { p -> p.clearDomains("Default"); p.clearApps("Default") }
        val nothing = context.getString(R.string.split_notice_nothing, "Profile 0", context.getString(R.string.split_profile_default))
        val both = context.getString(R.string.split_notice_both, "Profile 0", context.getString(R.string.split_profile_default))
        val installed = context.packageManager.getInstalledApplications(0).map { it.packageName }.first { it != context.packageName }
        val a = open()
        // Bypass VPN: no notice, whatever the lists hold.
        splitOn(false)
        render(a, 2)
        assertFalse(shows(a, nothing)); assertFalse(shows(a, both))
        // Through VPN with nothing listed: the desktop's, naming the config and its profile.
        splitOn(true)
        render(a, 2)
        assertTrue("Through VPN with no rules says so", shows(a, nothing))
        assertFalse(shows(a, both))
        // Rules that can be used: it goes.
        editProfiles { it.addDomains("Default", "home.example") }
        render(a, 2)
        assertFalse(shows(a, nothing)); assertFalse(shows(a, both))
        // Applications and domains: Android's own.
        editProfiles { it.addApp("Default", installed) }
        render(a, 2)
        assertTrue("Both kinds say that Android cannot add them", shows(a, both))
        assertFalse(shows(a, nothing))
        // Applications that are not installed are no rules: the desktop's again.
        editProfiles { p -> p.clearApps("Default"); p.clearDomains("Default"); p.addApp("Default", "not.installed.package") }
        render(a, 2)
        assertTrue(shows(a, nothing))
        // Split tunnelling off: none.
        prefs.edit().putBoolean("split_enabled", false).commit()
        render(a, 2)
        assertFalse(shows(a, nothing))
    }

    @Test fun theNoticeIsAboutTheProfileTheSelectedConfigUsesNotTheOneOnThePage() {
        editProfiles { p -> p.clearDomains("Default"); p.clearApps("Default"); p.add("Work"); p.addDomains("Work", "work.example"); p.select("Default") }
        seedConfigs("Work", null)
        splitOn(true)
        val a = open()
        render(a, 2)
        val nothingForConfigZero = context.getString(R.string.split_notice_nothing, "Profile 0", "Work")
        assertFalse("Config 0 uses Work, which has a rule that can be used", shows(a, nothingForConfigZero))
        onMain { method("selectConfig", Int::class.javaPrimitiveType).invoke(a, 1) }
        render(a, 2)
        assertTrue("Config 1 uses Default, which has none, whichever profile is on the page",
            shows(a, context.getString(R.string.split_notice_nothing, "Profile 1", context.getString(R.string.split_profile_default))))
    }

    @Test fun withNoConfigsTheNoticeIsTheDesktopsAndNamesNone() {
        seed(0)
        editProfiles { p -> p.clearDomains("Default"); p.clearApps("Default") }
        splitOn(true)
        val a = open()
        render(a, 2)
        assertTrue(shows(a, context.getString(R.string.split_notice_nothing_no_configs)))
        editProfiles { it.addDomains("Default", "home.example") }
        render(a, 2)
        assertFalse(shows(a, context.getString(R.string.split_notice_nothing_no_configs)))
    }

    // What the selected config brings with it and what was dropped become notices, the imported rules joined by ", ".
    @Test fun importedRulesAndDroppedRulesAreNotices() {
        val list = org.json.JSONArray().put(org.json.JSONObject().put("name", "Profile 0")
            .put("toml", source.replace("exclusions = []", "exclusions = [\"a.example\", \"b.example\"]")))
        assertTrue(ConfigStore.write(prefs, list.toString()))
        val a = open()
        render(a, 2)
        assertTrue(shows(a, context.getString(R.string.imported_routing_rules, "a.example, b.example")))
        assertFalse("No notice for rules that were not dropped", shows(a, context.getString(R.string.legacy_rules_removed)))
        prefs.edit().putBoolean("rules_migrated", true).commit()
        render(a, 2)
        assertTrue(shows(a, context.getString(R.string.legacy_rules_removed)))
        onMain {
            val notices = descendants(pageRoot(a)).filterIsInstance<TextView>().filter { it.text.toString() == context.getString(R.string.legacy_rules_removed) }
            assertEquals(theme(a).warn, notices.single().currentTextColor)
            assertEquals("on infoBg, as every notice is", theme(a).infoBg, (notices.single().background as GradientDrawable).color!!.defaultColor)
        }
    }

    // A config's profile is chosen in the editor, in a select box of its own, and changing only that is a change.
    @Test fun theEditorChoosesTheSplitProfileOfAConfigAndChangingOnlyThatIsAChange() {
        editProfiles { it.add("Work") }
        val a = open()
        render(a, 1)
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 1) }
        instrumentation.waitForIdleSync()
        fun shown() = onMain { (root(a).findViewWithTag<ViewGroup>("editor:profile").getChildAt(0) as TextView).text.toString() }
        assertTrue(shows(a, context.getString(R.string.editor_split_profile)))
        assertEquals("A config that names no profile uses Default", context.getString(R.string.split_profile_default), shown())
        click(a, "editor:profile")
        eventually("The choices open under the box") { overlayTag(a, "select") != null }
        val rows = onMain { descendants(root(a).findViewWithTag<View>("overlay:select")).filterIsInstance<TextView>().map { it.text.toString() }.toList() }
        assertEquals(listOf(context.getString(R.string.split_profile_default), "Work"), rows)
        assertTrue("The profile in use has the check mark", onMain { descendants(root(a).findViewWithTag<View>("select:0")).any { it is ImageView } })
        click(a, "select:1")
        eventually("Work is chosen") { shown() == "Work" }
        eventually("and the choices close") { overlayTag(a, "select") == null }
        click(a, "editor:cancel")
        eventually("Only the profile changed, and Cancel asks") { confirmMessage(a) == context.getString(R.string.editor_discard_question) }
        click(a, "confirm:cancel")
        eventually("No keeps the editor") { overlayTag(a, "confirm") == null && overlayTag(a, "editor") != null }
        assertEquals("Work", shown())
        click(a, "editor:save")
        (field("validationWorker").get(a) as ExecutorService).submit {}.get(30, TimeUnit.SECONDS)
        instrumentation.waitForIdleSync()
        eventually("Save closes the editor") { overlayTag(a, "editor") == null }
        assertEquals("Work", saved()[1].optString("profile"))
        assertFalse("The other config names none", saved()[0].has("profile"))
        // The same editor, set back to Default: Default is no field.
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 1) }
        instrumentation.waitForIdleSync()
        assertEquals("Work", shown())
        click(a, "editor:profile")
        eventually("The choices open") { overlayTag(a, "select") != null }
        click(a, "select:0")
        eventually("Default is chosen") { shown() == context.getString(R.string.split_profile_default) }
        click(a, "editor:save")
        (field("validationWorker").get(a) as ExecutorService).submit {}.get(30, TimeUnit.SECONDS)
        instrumentation.waitForIdleSync()
        eventually("Save closes it") { overlayTag(a, "editor") == null }
        assertFalse("Default is not written", saved()[1].has("profile"))
    }

    // A config whose profile is no longer one is shown with Default in the editor, and saved with it.
    @Test fun anEditorShowsDefaultForAProfileThatIsGone() {
        seedConfigs(null, "Deleted")
        val a = open()
        render(a, 1)
        onMain { method("editConfig", Int::class.javaPrimitiveType).invoke(a, 1) }
        instrumentation.waitForIdleSync()
        assertEquals(context.getString(R.string.split_profile_default),
            onMain { (root(a).findViewWithTag<ViewGroup>("editor:profile").getChildAt(0) as TextView).text.toString() })
        click(a, "editor:cancel")
        eventually("and nothing was changed, so Cancel closes it at once") { overlayTag(a, "editor") == null }
    }

    // ---- Settings (qml/pages/SettingsPage.qml) ----

    private fun defaultRoutes(): List<String> = ExcludedRoutes.defaults(context)
    private fun savedRoutes(): List<String> = ExcludedRoutes.load(prefs.getString(ExcludedRoutes.KEY, null), defaultRoutes())
    private fun routeLabels(a: MainActivity): List<String> {
        val labels = ArrayList<String>()
        while (onMain { root(a).findViewWithTag<View?>("route:${labels.size}") } != null) labels.add(chipLabel(a, "route:${labels.size}"))
        return labels
    }
    // The field under the applications (qml/pages/SplitPage.qml, apSuggest): what is typed lists the installed applications that match, above it,
    // the name over its package, as wide as the field, and Enter or a tap adds the highlighted one. The list takes neither the focus nor the
    // keyboard from the field; Up and Down move the highlight while it is up; Back puts it away before it does anything else.
    @Test fun theApplicationFieldListsWhatMatchesAboveItAndAddsTheHighlightedOne() {
        val a = open()
        leaveTouchMode(a)
        render(a, 2)
        val entries = onMain { method("installedApps").invoke(a) as List<*> }
        assumeTrue("This device shows no application to match", entries.size >= 2)
        val entryClass = Class.forName("org.freetunnel.android.MainActivity\$AppEntry")
        val pkgField = entryClass.getDeclaredField("pkg").apply { isAccessible = true }
        val inProfile = profiles().apps(profiles().edited())
        val target = entries.map { pkgField.get(it) as String }.first { it !in inProfile }
        fun appField() = root(a).findViewWithTag<EditText>("apps:input")
        assertEquals("The field has the desktop's place and Android's words", context.getString(R.string.split_apps_hint), onMain { appField().hint.toString() })
        focusOn(a, "apps:input")
        onMain { appField().setText(".") }
        eventually("Every package has a dot: the suggestions open") { overlayTag(a, "appsuggest") != null && onMain { root(a).findViewWithTag<View?>("suggestion:1") != null } }
        val theme = theme(a)
        onMain {
            val layer = root(a).findViewWithTag<OverlayLayer>("overlay:appsuggest")
            val panel = layer.panel()
            val input = appField()
            val at = IntArray(2).also { input.getLocationInWindow(it) }
            val panelAt = IntArray(2).also { panel.getLocationInWindow(it) }
            assertTrue("Above the field, 4 clear of it: ${panelAt[1] + panel.height} against ${at[1]}", panelAt[1] + panel.height <= at[1])
            assertEquals("as wide as the field", input.width.toFloat(), panel.width.toFloat(), 2f)
            assertNull("It casts no shadow: the desktop's list has none", layer.shadow())
            assertEquals("The field keeps the focus", input, a.currentFocus)
            // Only the highlight moves, as on the desktop: a clickable row would be a focus stop with no key and no look.
            for (row in descendants(panel).filter { (it.tag as? String)?.startsWith("suggestion:") == true })
                assertFalse("${row.tag} must not be a focus stop", row.isFocusable)
            assertTrue("A name over a package", descendants(root(a).findViewWithTag<View>("suggestion:0")).filterIsInstance<TextView>().toList().size == 2)
            assertEquals("The first is highlighted: on surface", theme.surface, ((root(a).findViewWithTag<View>("suggestion:0")).background as GradientDrawable).color!!.defaultColor)
            assertEquals("and the next is not: on the card's colour", theme.bg, ((root(a).findViewWithTag<View>("suggestion:1")).background as GradientDrawable).color!!.defaultColor)
        }
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_DOWN)
        eventually("Down moves the highlight, and does not leave the field") {
            onMain { ((root(a).findViewWithTag<View>("suggestion:1")).background as GradientDrawable).color!!.defaultColor } == theme.surface
        }
        assertEquals("the field has the focus still", "apps:input", focusedKey(a))
        // Back puts the list away first, and leaves what was typed.
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        eventually("Back puts the suggestions away") { overlayTag(a, "appsuggest") == null }
        assertEquals("on the same page", 2, onMain { field("page").getInt(a) })
        assertEquals("and keeps what was typed", ".", fieldText(a, "apps:input"))
        // A package typed whole: Enter adds the first of what matches.
        onMain { appField().setText(target) }
        eventually("The suggestions are back") { overlayTag(a, "appsuggest") != null && onMain { root(a).findViewWithTag<View?>("suggestion:0") != null } }
        val first = onMain { descendants(root(a).findViewWithTag<View>("suggestion:0")).filterIsInstance<TextView>().map { it.text.toString() }.toList()[1] }
        onMain { appField().onEditorAction(EditorInfo.IME_ACTION_DONE) }
        eventually("Enter adds the highlighted application") { profiles().apps(profiles().edited()).contains(first) }
        eventually("The list is put away, the field is empty and has the cursor for the next") {
            overlayTag(a, "appsuggest") == null && fieldText(a, "apps:input") == "" && focusedKey(a) == "apps:input"
        }
        // Words that match nothing say so on Enter, and add nothing.
        val count = profiles().apps(profiles().edited()).size
        onMain { appField().setText("zzzzzz-no-such-app"); appField().onEditorAction(EditorInfo.IME_ACTION_DONE) }
        eventually("A word that names nothing is said so") { context.getString(R.string.picker_nothing) in toastTexts(a) }
        assertEquals(count, profiles().apps(profiles().edited()).size)
    }

    private fun textOf(a: MainActivity, key: String): String = onMain { root(a).findViewWithTag<TextView>(key).text.toString() }
    private fun visible(a: MainActivity, key: String): Boolean = onMain { root(a).findViewWithTag<View?>(key)?.visibility == View.VISIBLE }
    // The rows of the select popup that is open, as the texts it shows.
    private fun selectRows(a: MainActivity): List<String> = onMain {
        descendants(root(a).findViewWithTag<View>("overlay:select")).filterIsInstance<TextView>().map { it.text.toString() }.toList()
    }

    // The page as the desktop has it, top to bottom, with its margins (18 on the sides, 6 above, 14 below), its section labels as
    // they are typed (no capitals, no bold), and a line between the rows of General and nowhere else.
    @Test fun theSettingsPageIsTheDesktopsInItsOrderWithItsMargins() {
        officialBuild()
        val a = open()
        render(a, 3)
        val keys = listOf("settings:language", "settings:theme", "switch:auto_connect", "switch:killswitch", "routes:restore", "route:0",
            "routes:input", "switch:logging", "settings:update", "settings:site", "settings:core")
        onMain {
            val tops = keys.map { topOf(root(a).findViewWithTag<View>(it)) }
            // The footer's two links share a line, so the order is non-decreasing, and the rest are strictly one under the other.
            assertTrue("top to bottom: $tops", tops.zipWithNext().all { (above, below) -> above <= below })
            assertEquals("the app and the core are one line", topOf(root(a).findViewWithTag<View>("settings:site")), topOf(root(a).findViewWithTag<View>("settings:core")))
            val others = keys.filter { it != "settings:core" }.map { topOf(root(a).findViewWithTag<View>(it)) }
            assertEquals("every other control is under the one before it: $others", others.sorted().distinct(), others)
            val labelIds = listOf(R.string.text_general, R.string.text_security, R.string.settings_excluded_routes, R.string.settings_logging,
                R.string.settings_maintenance)
            val labels = labelIds.map { id ->
                descendants(pageRoot(a)).filterIsInstance<TextView>().first { it.text.toString() == context.getString(id) }
            }
            assertEquals("the section labels are in order", labels.map { topOf(it) }.sorted(), labels.map { topOf(it) })
            for (label in labels) {
                assertFalse("${label.text}: never bold", label.typeface?.isBold == true)
                assertEquals("${label.text}: 12 sp", 12f, label.textSize / a.resources.displayMetrics.scaledDensity, 0.5f)
            }
            val body = (field("pageScroll").get(a) as ScrollView).getChildAt(0)
            assertEquals(Math.round(dp(a, 18f)), body.paddingLeft)
            assertEquals(Math.round(dp(a, 18f)), body.paddingRight)
            assertEquals("6 above", Math.round(dp(a, 6f)), body.paddingTop)
            assertEquals("14 below", Math.round(dp(a, 14f)), body.paddingBottom)
            // Lines: one between Language and Theme and one between Theme and Connect on startup, and none anywhere else.
            val inner = body.width - body.paddingLeft - body.paddingRight
            val lines = descendants(body).filter { it !is ViewGroup && it.isShown && it.width >= inner - 2 && it.height in 1..Math.round(dp(a, 2f)) }.toList()
            assertEquals("two lines in General and no other", 2, lines.size)
            val language = topOf(root(a).findViewWithTag<View>("settings:language"))
            val connect = topOf(root(a).findViewWithTag<View>("switch:auto_connect"))
            assertTrue("both are between the rows of General", lines.all { topOf(it) in language..connect })
            // The desktop's own texts: no About section.
            assertFalse(descendants(body).filterIsInstance<TextView>().any { it.text.toString() == "About" })
            // The desktop's switches that this has, and no switch for checking updates on launch, which the app does by itself.
            assertEquals(listOf("switch:auto_connect", "switch:killswitch", "switch:logging"),
                descendants(body).mapNotNull { it.tag as? String }.filter { it.startsWith("switch:") }.distinct().sorted().toList())
        }
        assertTrue("Kill switch has its subtitle", shows(a, context.getString(R.string.settings_kill_switch_hint)))
        // What only the desktop has is not here.
        for (text in listOf("Launch at system startup", "Hotkeys", "Verbose logs", "Let the VPN config open ports")) assertFalse("$text is the desktop's alone", shows(a, text))
    }

    // The toggles' tracks, the dropdowns' values and the links at the end of a row end where the page's margin does, as the desktop's do. A
    // toggle's row fills with the focus 10 beyond the margin on the left and 6 on the right, where its content is padded by the same,
    // and the body does not clip it there.
    @Test fun whatEndsARowEndsAtThePageMargin() {
        val a = open()
        render(a, 3)
        onMain {
            val body = (field("pageScroll").get(a) as ScrollView).getChildAt(0) as ViewGroup
            assertFalse("so that what fills beyond the margin is drawn", body.clipToPadding)
            fun right(view: View) = IntArray(2).also { view.getLocationInWindow(it) }[0] + view.width
            fun left(view: View) = IntArray(2).also { view.getLocationInWindow(it) }[0]
            val edge = right(body) - body.paddingRight
            val toggle = root(a).findViewWithTag<ToggleView>("switch:killswitch")
            assertEquals("a toggle's track", edge.toFloat(), right(toggle).toFloat(), 2f)
            val row = toggle.parent as ViewGroup
            assertEquals("its row reaches 6 beyond the margin", (edge + Math.round(dp(a, 6f))).toFloat(), right(row).toFloat(), 2f)
            assertEquals("and 10 beyond it on the left", (left(body) + body.paddingLeft - Math.round(dp(a, 10f))).toFloat(), left(row).toFloat(), 2f)
            assertEquals("a dropdown's ▾", edge.toFloat(), (right(root(a).findViewWithTag<View>("settings:theme")) - Math.round(dp(a, 6f))).toFloat(), 2f)
            val restore = root(a).findViewWithTag<TextView>("routes:restore")
            val clear = root(a).findViewWithTag<TextView>("routes:clear")
            assertEquals("the last link of a header", edge.toFloat(), (right(clear) - clear.paddingRight).toFloat(), 2f)
            assertTrue("and the one before it ends before that", right(restore) < right(clear))
            // The label of a toggle's row is where the others' labels are.
            val label = descendants(row).filterIsInstance<TextView>().first()
            val language = descendants(root(a).findViewWithTag<View>("settings:language").parent as ViewGroup).filterIsInstance<TextView>().first()
            assertEquals("a toggle's label starts at the margin, like Language's", left(language).toFloat(), left(label).toFloat(), 2f)
        }
    }

    // Language is English or Русский, each in itself whatever the app speaks, and the one the app speaks is marked.
    @Test fun theLanguageRowOffersEnglishAndRussianInThemselvesAndMarksTheOneInUse() {
        val a = open()
        leaveTouchMode(a)
        render(a, 3)
        assertTrue("The arrows reach both dropdowns", reachable(a, "tab:3").containsAll(listOf("settings:language", "settings:theme")))
        val speaking = onMain { AppLocale.current(a) }
        val shown = if (speaking == "ru") "Русский" else "English"
        assertEquals("The row shows the language in use", shown, onMain { ((root(a).findViewWithTag<ViewGroup>("settings:language")).getChildAt(0) as TextView).text.toString() })
        ok(a, "settings:language")
        eventually("The choices open") { overlayTag(a, "select") != null }
        assertEquals(listOf("English", "Русский"), selectRows(a))
        assertTrue("and the one in use has the check mark", onMain {
            descendants(root(a).findViewWithTag<View>("select:${if (speaking == "ru") 1 else 0}")).any { it is ImageView }
        })
        eventually("The one in use has the focus") { focusedKey(a) == "select:${if (speaking == "ru") 1 else 0}" }
        // The one in use is no change: nothing is saved, and nothing is recreated.
        click(a, "select:${if (speaking == "ru") 1 else 0}")
        eventually("The popup closes") { overlayTag(a, "select") == null }
        assertFalse("nothing is saved", prefs.contains("language"))
        assertFalse(onMain { a.isDestroyed || a.isFinishing })
        eventually("The focus is back on the row") { focusedKey(a) == "settings:language" }
    }

    // Choosing the other language recreates the activity in it, back on Settings; and where the app has no language of its own to
    // speak in (before Android 13) it is the saved one that every context wraps. The language is put back, which Android keeps for the app.
    @Test fun choosingRussianSwitchesTheWholeAppAndSettingsComesBack() {
        val a = open()
        render(a, 3)
        var second: MainActivity? = null
        // The lifecycle monitor is asked on the main thread, and nowhere else.
        fun resumed(): MainActivity? = onMain {
            androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry.getInstance()
                .getActivitiesInStage(androidx.test.runner.lifecycle.Stage.RESUMED).firstOrNull { it is MainActivity && it !== a } as MainActivity?
        }
        try {
            onMain { AppLocale.choose(a, prefs, "ru") }
            eventually("The activity is made again", 20_000) { if (second == null) second = resumed(); second != null }
            val b = second!!
            activity = b
            instrumentation.waitForIdleSync()
            assertEquals("ru", onMain { AppLocale.current(b) })
            assertEquals("and it is on Settings still", 3, onMain { field("page").getInt(b) })
            assertTrue("in Russian", shows(b, "Язык"))
            assertTrue(shows(b, "Основное"))
            assertEquals("Русский", onMain { ((root(b).findViewWithTag<ViewGroup>("settings:language")).getChildAt(0) as TextView).text.toString() })
        } finally {
            // Back to the default language, where Android keeps the choice for the app, and then to a clean activity.
            prefs.edit().remove("language").commit()
            if (android.os.Build.VERSION.SDK_INT >= 33)
                context.getSystemService(android.app.LocaleManager::class.java).setApplicationLocales(android.os.LocaleList.getEmptyLocaleList())
            val live = second ?: a
            instrumentation.runOnMainSync { if (!live.isDestroyed) live.recreate() }
            fun resumedOnes(): List<MainActivity> = onMain {
                androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry.getInstance()
                    .getActivitiesInStage(androidx.test.runner.lifecycle.Stage.RESUMED).filterIsInstance<MainActivity>()
            }
            eventually("English again", 20_000) { resumedOnes().any { AppLocale.current(it) == "en" } }
            activity = resumedOnes().firstOrNull() ?: activity
        }
    }

    // Without a saved list the routes are the shared defaults; the first change saves the whole list.
    @Test fun theExcludedRoutesAreTheDefaultsUntilOneIsChangedAndThenTheListIsSaved() {
        val a = open()
        render(a, 3)
        assertFalse(prefs.contains(ExcludedRoutes.KEY))
        assertEquals(defaultRoutes(), routeLabels(a))
        assertEquals(7, defaultRoutes().size)
        click(a, "route:0")
        eventually("The first route is gone, at once and with nothing asked") { savedRoutes() == defaultRoutes().drop(1) }
        assertNull(overlayTag(a, "confirm"))
        assertEquals(defaultRoutes().drop(1), routeLabels(a))
        assertEquals("saved as the JSON list", defaultRoutes().drop(1), ExcludedRoutes.load(prefs.getString(ExcludedRoutes.KEY, null), emptyList()))
    }

    // Several at once, separated by spaces, commas or semicolons; the ones that are no subnet are named, and a /0 says why.
    @Test fun routesAreAddedAndTheWordsThatAreNoSubnetAreNamed() {
        val a = open()
        render(a, 3)
        enter(a, "routes:input", "10.1.0.0/16, 8.8.8.8;fd00::/8 junk 0.0.0.0/0")
        eventually("The subnets are added after those there were") { savedRoutes() == defaultRoutes() + listOf("10.1.0.0/16", "8.8.8.8/32", "fd00::/8") }
        assertTrue("the words that are none are named, with the reason a /0 has",
            context.getString(R.string.settings_routes_refused_every, "junk, 0.0.0.0/0") in toastTexts(a))
        assertEquals("The field is empty again", "", fieldText(a, "routes:input"))
        assertEquals("a bare address is a /32", "8.8.8.8/32", routeLabels(a)[defaultRoutes().size + 1])
        // A typo alone is named without the reason, and nothing changes.
        enter(a, "routes:input", "10.0.0/8")
        eventually("A typo is named") { context.getString(R.string.settings_routes_refused, "10.0.0/8") in toastTexts(a) }
        assertEquals("and the text stays to be put right", "10.0.0/8", fieldText(a, "routes:input"))
        // A /0 alone, which is no change.
        enter(a, "routes:input", "0.0.0.0/0")
        eventually("A /0 says why") { context.getString(R.string.settings_routes_refused_every, "0.0.0.0/0") in toastTexts(a) }
        assertEquals("0.0.0.0/0", fieldText(a, "routes:input"))
        assertEquals(defaultRoutes() + listOf("10.1.0.0/16", "8.8.8.8/32", "fd00::/8"), savedRoutes())
        // What is there already is neither added nor refused: no new toast.
        val before = toastTexts(a)
        enter(a, "routes:input", "8.8.8.8")
        SystemClock.sleep(300)
        assertEquals(defaultRoutes().size + 3, savedRoutes().size)
        assertTrue("a route that is there is no refusal", toastTexts(a).all { it in before })
    }

    // Restore defaults and Clear all each ask first, with the desktop's questions and buttons.
    @Test fun restoreDefaultsAndClearAllAskFirst() {
        prefs.edit().putString(ExcludedRoutes.KEY, ExcludedRoutes.toJson(listOf("1.2.3.0/24", "10.0.0.0/8"))).commit()
        val a = open()
        leaveTouchMode(a)
        render(a, 3)
        click(a, "routes:restore")
        eventually("Restore asks") { confirmMessage(a) == context.getString(R.string.settings_routes_restore_question) }
        assertEquals(context.getString(R.string.import_replace), textOf(a, "confirm:ok"))
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        eventually("Back says no") { overlayTag(a, "confirm") == null }
        assertEquals(listOf("1.2.3.0/24", "10.0.0.0/8"), savedRoutes())
        click(a, "routes:restore")
        eventually("It asks again") { confirmMessage(a) != null }
        answerYes(a)
        eventually("The defaults replace the list") { savedRoutes() == defaultRoutes() }
        // Clear all.
        click(a, "routes:clear")
        eventually("Clear all asks") { confirmMessage(a) == context.getString(R.string.settings_routes_clear_question) }
        assertEquals(context.getString(R.string.text_clear), textOf(a, "confirm:ok"))
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
        eventually("Back says no") { overlayTag(a, "confirm") == null }
        assertEquals(defaultRoutes(), savedRoutes())
        click(a, "routes:clear")
        eventually("It asks again") { confirmMessage(a) != null }
        answerYes(a)
        eventually("The list is saved empty") { prefs.getString(ExcludedRoutes.KEY, null) == "[]" }
        assertEquals("and stays empty: cleared is not absent", emptyList<String>(), savedRoutes())
        eventually("The link goes with the routes") { !pageKeys(a).contains("routes:clear") && !pageKeys(a).contains("route:0") }
    }

    // A change builds a VPN that is up again, with the routes in its plan; one that is down is built from them when it starts.
    @Test fun aRouteChangeBuildsARunningVpnAgainAndANoOpDoesNot() {
        val a = open()
        render(a, 3)
        val gate = java.util.concurrent.CountDownLatch(1)
        (field("validationWorker").get(a) as ExecutorService).execute { gate.await() }
        onMain { field("vpnState").setInt(a, 2) }
        serviceState(2)
        try {
            fun requests() = onMain { field("validationRequest").getLong(a) }
            val before = requests()
            enter(a, "routes:input", "10.9.0.0/16")
            eventually("The route is added") { savedRoutes().contains("10.9.0.0/16") }
            assertEquals("a VPN that is up is built again", before + 1, requests())
            enter(a, "routes:input", "10.9.0.0/16")
            SystemClock.sleep(300)
            assertEquals("a route that is there changes nothing", before + 1, requests())
            click(a, "route:0")
            eventually("A route is removed") { !savedRoutes().contains(defaultRoutes()[0]) }
            assertEquals("and that builds it again", before + 2, requests())
        } finally {
            serviceState(0)
            onMain { field("vpnState").setInt(a, 0) }
            gate.countDown()
        }
    }

    // The plan the service builds from the saved routes: they are the config's own, then these.
    @Test fun theSavedRoutesAreInThePlanAndSoAreTheDefaultsWhereNoneAreSaved() {
        fun excluded(plan: RoutingConfig.Plan) =
            org.tomlj.Toml.parse(plan.toml).getTable("listener.tun")!!.getArray("excluded_routes")!!.toList().map { it.toString() }
        assertEquals("the defaults where nothing is saved", defaultRoutes(), excluded(RoutingConfig.prepare(context, source, "Default")))
        prefs.edit().putString(ExcludedRoutes.KEY, ExcludedRoutes.toJson(listOf("172.20.0.0/14", "0.0.0.0/0"))).commit()
        assertEquals("only what is saved, and never a /0", listOf("172.20.0.0/14"), excluded(RoutingConfig.prepare(context, source, "Default")))
        prefs.edit().putString(ExcludedRoutes.KEY, "[]").commit()
        assertEquals("none, once cleared", emptyList<String>(), excluded(RoutingConfig.prepare(context, source, "Default")))
    }

    // Maintenance: the one row, and for each state what it says, which picture is beside it and what a click does.
    private fun release() = UpdatePolicy.Release("99.0.0", "https://example.invalid/update.apk",
        "https://example.invalid/SHA256SUMS-android.txt", "https://example.invalid/SHA256SUMS-android.txt.sig")
    private fun updateShows(a: MainActivity, state: AndroidUpdater.State, committed: Boolean = false) {
        updater("committed", committed)
        updater("state", state)
        onMain { method("refreshUpdateControls").invoke(a) }
        instrumentation.waitForIdleSync()
    }
    // Plain, so that it can be used in an onMain block: onMain does not nest.
    private fun arrowOf(a: MainActivity): UpdateArrowView = root(a).findViewWithTag("update:arrow")

    @Test fun theMaintenanceRowSaysAndShowsWhatTheDesktopsDoesForEachState() {
        officialBuild()
        updater("release", release())
        val a = open()
        render(a, 3)
        // Idle, and up to date: "Check for updates", and the version at the right, with a tick once it is known to be current.
        updateShows(a, AndroidUpdater.State.IDLE)
        assertEquals(context.getString(R.string.update_check), textOf(a, "settings:update"))
        assertTrue(onMain { root(a).findViewWithTag<View>("settings:update").isClickable })
        assertTrue(visible(a, "update:version"))
        assertEquals(BuildConfig.VERSION_NAME, textOf(a, "update:version"))
        for (key in listOf("update:tick", "update:arrow", "update:dots")) assertFalse("$key is not shown", onMain { root(a).findViewWithTag<View>(key).isShown })
        updateShows(a, AndroidUpdater.State.CURRENT)
        assertEquals("the same words", context.getString(R.string.update_check), textOf(a, "settings:update"))
        assertTrue(visible(a, "update:tick"))
        // ✓ is drawn, in success, as Inter draws it at the desktop's 14 px (9.6 by 7).
        onMain {
            val tick = root(a).findViewWithTag<ImageView>("update:tick")
            assertEquals(theme(a).success, tick.imageTintList!!.defaultColor)
            assertTrue("the tick is 9.6 across", Math.abs(Math.round(9.6f * a.resources.displayMetrics.density) - tick.drawable.intrinsicWidth) <= 1)
        }
        assertTrue(visible(a, "update:version"))
        // Checking: dots, and nothing to click.
        updateShows(a, AndroidUpdater.State.CHECKING)
        assertEquals(context.getString(R.string.update_checking), textOf(a, "settings:update"))
        assertFalse(onMain { root(a).findViewWithTag<View>("settings:update").isClickable })
        assertTrue(onMain { root(a).findViewWithTag<View>("update:dots").isShown })
        assertFalse(onMain { root(a).findViewWithTag<View>("update:arrow").isShown })
        assertFalse(visible(a, "update:version"))
        assertFalse(visible(a, "update:tick"))
        // An offer: Version X is available, with an arrow down.
        updateShows(a, AndroidUpdater.State.AVAILABLE)
        assertEquals(context.getString(R.string.update_available, "99.0.0"), textOf(a, "settings:update"))
        assertTrue(onMain { root(a).findViewWithTag<View>("settings:update").isClickable })
        assertTrue(onMain { arrowOf(a).isShown })
        assertEquals("a straight arrow down", 0f, onMain { arrowOf(a).bend() }, 0f)
        assertFalse(onMain { arrowOf(a).spinning() })
        assertFalse(onMain { root(a).findViewWithTag<View>("update:dots").isShown })
        // A download: the arrow bent round and turning, and a click on the line does nothing: it cannot be stopped, as the desktop's cannot.
        updater("progress", 42)
        updateShows(a, AndroidUpdater.State.DOWNLOADING)
        assertEquals(context.getString(R.string.update_downloading, 42), textOf(a, "settings:update"))
        assertEquals(1f, onMain { arrowOf(a).bend() }, 0f)
        eventually("it turns") { onMain { arrowOf(a).spinning() && arrowOf(a).spinAngle() > 0f } }
        assertFalse("the line is not clickable while one comes down", onMain { root(a).findViewWithTag<View>("settings:update").isClickable })
        onMain { root(a).findViewWithTag<View>("settings:update").performClick() }
        instrumentation.waitForIdleSync()
        assertEquals("and a click on it changes nothing", AndroidUpdater.State.DOWNLOADING, AndroidUpdater.state)
        assertEquals(context.getString(R.string.update_downloading, 42), textOf(a, "settings:update"))
        // Downloaded: Android's own words, no picture, a click installs.
        updateShows(a, AndroidUpdater.State.READY)
        assertEquals(context.getString(R.string.update_ready, "99.0.0"), textOf(a, "settings:update"))
        assertFalse(onMain { arrowOf(a).isShown })
        assertTrue(onMain { root(a).findViewWithTag<View>("settings:update").isClickable })
        eventually("the arrow stops turning") { onMain { !arrowOf(a).spinning() } }
        eventually("and has come round to where it started") { onMain { arrowOf(a).spinAngle() == 0f } }
        // Installing: nothing to click, unless Android has the session and its window was lost.
        updateShows(a, AndroidUpdater.State.INSTALLING, committed = false)
        assertEquals("and says nothing about starting it again while it cannot", context.getString(R.string.update_installing), textOf(a, "settings:update"))
        assertFalse(onMain { root(a).findViewWithTag<View>("settings:update").isClickable })
        updateShows(a, AndroidUpdater.State.INSTALLING, committed = true)
        assertTrue(onMain { root(a).findViewWithTag<View>("settings:update").isClickable })
        assertEquals("It offers to start again only when it can", context.getString(R.string.update_installing_again), textOf(a, "settings:update"))
        // A failed check is worded as the desktop words it; a failed download or install says why as it is. Both offer to try again.
        updater("failure", UpdatePolicy.Failure.NETWORK)
        updater("failedCheck", true)
        updateShows(a, AndroidUpdater.State.ERROR)
        // Inside "Update check failed: …" the reason continues the sentence, so it begins in lower case.
        assertEquals(context.getString(R.string.update_check_failed, context.getString(R.string.update_error_network_inline)), textOf(a, "settings:update"))
        assertTrue("the reason inside the sentence continues it", context.getString(R.string.update_error_network_inline).first().isLowerCase())
        assertTrue(onMain { arrowOf(a).isShown })
        assertEquals("a retry is the arrow round", 1f, onMain { arrowOf(a).bend() }, 0f)
        assertFalse("and does not turn", onMain { arrowOf(a).spinning() })
        assertTrue(onMain { root(a).findViewWithTag<View>("settings:update").isClickable })
        updater("failure", UpdatePolicy.Failure.VERIFICATION)
        updater("failedCheck", false)
        updateShows(a, AndroidUpdater.State.ERROR)
        assertEquals(context.getString(R.string.update_error_verification), textOf(a, "settings:update"))
        // Said alone, a reason is a sentence: it begins with a capital.
        assertTrue("a failed download's line begins with a capital: ${textOf(a, "settings:update")}", textOf(a, "settings:update").first().isUpperCase())
        updater("failure", UpdatePolicy.Failure.NETWORK)
        updateShows(a, AndroidUpdater.State.ERROR)
        assertEquals(context.getString(R.string.update_error_network), textOf(a, "settings:update"))
        assertTrue("and so does a lost connection's: ${textOf(a, "settings:update")}", textOf(a, "settings:update").first().isUpperCase())
    }

    // The arrow (or the dots) is a target for the pointer only: the remote's stop is the words. A clickable view is a focus stop from API 26
    // unless it says it is not, and a stop with no key and no look is an invisible focus, which Down from the toggle above would land on.
    @Test fun theUpdateArrowIsNoFocusStopWhateverItShows() {
        officialBuild()
        updater("release", release())
        val a = open()
        leaveTouchMode(a)
        render(a, 3)
        val slot = onMain { field("updateSlot").get(a) as View }
        for ((state, failedCheck) in listOf(AndroidUpdater.State.AVAILABLE to false, AndroidUpdater.State.CHECKING to false,
                AndroidUpdater.State.DOWNLOADING to false, AndroidUpdater.State.ERROR to true)) {
            updater("failure", if (state == AndroidUpdater.State.ERROR) UpdatePolicy.Failure.NETWORK else null)
            updater("failedCheck", failedCheck)
            updateShows(a, state)
            onMain {
                assertTrue("$state: the arrow or the dots are shown", slot.isShown)
                assertTrue("$state: and a click on it still does what the words do", slot.isClickable)
                assertFalse("$state: but it is no focus stop", slot.isFocusable)
                for (view in descendants(slot)) assertFalse("$state: nor is ${view.javaClass.simpleName} in it", view.isFocusable)
                for (view in descendants(root(a)).filter { it.isShown && it.isClickable && it.isFocusable })
                    assertTrue("$state: ${view.javaClass.simpleName} is a stop with no key", view.tag is String)
            }
        }
    }

    // The desktop bends the arrow of an offer, and leans a retry arrow, only while the pointer is over the arrow (updIconMa): over the words it
    // colours and underlines them, and the arrow stays as it is. So a remote's focus on the words leaves the arrow alone.
    @Test fun aFocusOnTheUpdateWordsLeavesTheArrowAloneAndThePointerOnTheArrowBendsIt() {
        officialBuild()
        updater("release", release())
        val a = open()
        leaveTouchMode(a)
        render(a, 3)
        updateShows(a, AndroidUpdater.State.AVAILABLE)
        assertEquals(0f, onMain { arrowOf(a).bend() }, 0f)
        focusOn(a, "settings:update")
        SystemClock.sleep(500)
        assertEquals("A focus on the words does not bend the arrow", 0f, onMain { arrowOf(a).bend() }, 0f)
        onMain {
            val words = root(a).findViewWithTag<TextView>("settings:update")
            assertEquals("The words take their hover look: underlined", android.graphics.Paint.UNDERLINE_TEXT_FLAG, words.paintFlags and android.graphics.Paint.UNDERLINE_TEXT_FLAG)
            assertEquals("and the colour the link has there", theme(a).accent, words.currentTextColor)
        }
        fun pointer(on: Boolean) = onMain { method("setUpdateHot", Boolean::class.javaPrimitiveType).invoke(a, on) }
        pointer(true)
        eventually("The pointer on the arrow bends it into ↻") { onMain { arrowOf(a).bend() } == 1f }
        pointer(false)
        eventually("and it straightens when the pointer goes") { onMain { arrowOf(a).bend() } == 0f }
        updater("failure", UpdatePolicy.Failure.NETWORK)
        updateShows(a, AndroidUpdater.State.ERROR)
        assertEquals(0f, onMain { arrowOf(a).turn() }, 0f)
        focusOn(a, "settings:update")
        SystemClock.sleep(500)
        assertEquals("A focus on the words does not lean a retry arrow either", 0f, onMain { arrowOf(a).turn() }, 0f)
        pointer(true)
        eventually("a retry leans 45 degrees under the pointer") { onMain { arrowOf(a).turn() } == 45f }
        pointer(false)
        eventually("and comes upright") { onMain { arrowOf(a).turn() } == 0f }
    }

    // The footer is centred, its two names are links to their pages, and where no browser exists there is a toast, not a crash.
    @Test fun theFooterNamesTheAppAndTheCoreAndOpensTheirPages() {
        val a = open()
        render(a, 3)
        assertEquals("FreeTunnel ${BuildConfig.VERSION_NAME}", textOf(a, "settings:site"))
        assertEquals(context.getString(R.string.settings_core) + BuildConfig.CORE_VERSION, textOf(a, "settings:core"))
        assertTrue("the core's version is the AAR's", BuildConfig.CORE_VERSION.matches(Regex("\\d+(\\.\\d+)*")))
        onMain {
            val site = root(a).findViewWithTag<View>("settings:site")
            val core = root(a).findViewWithTag<View>("settings:core")
            val page = pageRoot(a)
            fun left(view: View) = IntArray(2).also { view.getLocationInWindow(it) }[0]
            val middle = (left(site) + left(core) + core.width) / 2f
            // Together they are in the middle of the page.
            assertEquals("centred", left(page) + page.width / 2f, middle, page.width * 0.03f)
        }
        fun viewer(host: String, path: String) = instrumentation.addMonitor(
            IntentFilter(Intent.ACTION_VIEW).apply { addDataScheme("https"); addDataAuthority(host, null)
                addDataPath(path, android.os.PatternMatcher.PATTERN_LITERAL) },
            Instrumentation.ActivityResult(Activity.RESULT_CANCELED, null), true)
        val site = viewer("github.com", "/${UpdatePolicy.REPOSITORY}")
        val core = viewer("github.com", "/TrustTunnel/TrustTunnelClient")
        try {
            click(a, "settings:site")
            assertEquals("FreeTunnel opens its own page", 1, site.hits)
            assertEquals(0, core.hits)
            click(a, "settings:core")
            assertEquals("the core opens the TrustTunnel client's", 1, core.hits)
            assertEquals(1, site.hits)
        } finally {
            instrumentation.removeMonitor(site)
            instrumentation.removeMonitor(core)
        }
        // Nothing handles this: a TV with no browser. The toast says so.
        onMain { method("openLink", String::class.java).invoke(a, "freetunnel-test-nothing-opens-this://x") }
        assertTrue(context.getString(R.string.text_no_browser) in toastTexts(a))
        assertFalse(onMain { a.isFinishing || a.isDestroyed })
    }

    // ---- Logs (qml/pages/LogsPage.qml) ----

    private val logFile get() = java.io.File(context.filesDir, LogFile.NAME)
    private fun writeLog(vararg lines: String) { logFile.delete(); lines.forEach { LogFile.append(logFile, it) } }
    private fun shownLogOf(a: MainActivity): String = textOf(a, "log:text")
    private fun renderGeneration(a: MainActivity): Int = onMain { field("renderGeneration").getInt(a) }
    // Plain, so that it can be used in an onMain block: onMain does not nest.
    private fun logBox(a: MainActivity): ScrollView = root(a).findViewWithTag("log:box")
    private fun scrollEnd(a: MainActivity): Int = onMain {
        val box = logBox(a)
        Math.max(0, box.getChildAt(0).height - (box.height - box.paddingTop - box.paddingBottom))
    }
    private val entryLine = LogLines.entry(1_793_104_496_000L, java.time.ZoneId.of("UTC"), LogLines.INFO, "Tunnel up")

    // New lines are shown as the desktop shows them ("HH:mm:ss LEVEL message"), old ones as they are.
    @Test fun theLogIsShownAsTheDesktopShowsItAndAnOldLineAsItWas() {
        prefs.edit().putBoolean("logging", true).commit()
        writeLog("1790000000000 an old line", entryLine, LogLines.entry(1_793_104_500_000L, java.time.ZoneId.of("UTC"), LogLines.WARN, "Kill switch\non"))
        val a = open()
        render(a, 4)
        assertEquals("1790000000000 an old line\n12:34:56 INFO Tunnel up\n12:35:00 WARN Kill switch on", shownLogOf(a))
        val text = onMain { root(a).findViewWithTag<TextView>("log:text") }
        onMain {
            assertEquals("Monospace", android.graphics.Typeface.MONOSPACE, text.typeface)
            assertEquals("11 sp", 11f, text.textSize / a.resources.displayMetrics.scaledDensity, 0.5f)
            assertEquals("in the text colour", theme(a).text, text.currentTextColor)
        }
        assertTrue("There is no placeholder over a log", onMain {
            descendants(root(a)).filterIsInstance<TextView>().none { it.text.toString() == context.getString(R.string.logs_placeholder) && it.isShown }
        })
    }

    // Clear and Copy are as wide as their words, with room between them; the box is the surface, radius 8, and fills the page.
    @Test fun clearAndCopyAreLinksAsWideAsTheirWordsAndTheBoxFillsThePage() {
        prefs.edit().putBoolean("logging", true).commit()
        writeLog(entryLine)
        val a = open()
        leaveTouchMode(a)
        render(a, 4)
        onMain {
            val clear = root(a).findViewWithTag<TextView>("log:clear")
            val copy = root(a).findViewWithTag<TextView>("log:copy")
            assertEquals(context.getString(R.string.text_clear), clear.text.toString())
            assertEquals(context.getString(R.string.text_copy), copy.text.toString())
            for (link in listOf(clear, copy)) {
                val words = Math.ceil(link.paint.measureText(link.text.toString()).toDouble()).toInt()
                assertTrue("${link.text} hugs its word: ${link.width}", link.width <= words + link.paddingLeft + link.paddingRight + 1)
            }
            assertTrue("Clear is at the left", clear.left < copy.left)
            assertTrue("and Copy at the right, with room between", copy.left > clear.right + 20)
            assertTrue(Math.abs(topOf(clear) - topOf(copy)) <= 1)
            val box = logBox(a)
            val page = pageRoot(a)
            assertTrue("the box is wide: ${box.width} of ${page.width}", box.width > page.width * 0.85f)
            assertTrue("and tall: from ${topOf(box)}, ${box.height} of ${page.height}", box.height > page.height * 0.5f)
            assertEquals("12 inside it", Math.round(dp(a, 12f)), box.paddingLeft)
            // The foot's Auto-scroll fills a little beyond the margin when it has the focus, which a line of the page cuts off unless the page
            // says it need not; and the line is as wide as its words, not the page.
            val body = box.parent.parent as ViewGroup
            assertFalse("so the page does not clip its lines", body.clipChildren)
            val auto = root(a).findViewWithTag<View>("logs:autoscroll")
            val group = auto.parent as ViewGroup
            assertTrue("and the row round the toggle is no wider than its label and the toggle: ${group.width}", group.width < page.width / 2)
            assertEquals("It ends 6 beyond the margin", (body.width - body.paddingRight + Math.round(dp(a, 6f))).toFloat(),
                (group.right + (group.parent as View).left).toFloat(), 2f)
            // The page does not scroll: the box does.
            assertNull("no scroll view round the page", field("pageScroll").get(a))
        }
        val look = onMain { focusLook(logBox(a)) }
        assertEquals("the box rests on the surface", theme(a).surface, look.restFill)
    }

    // Logging on and nothing to show: the placeholder in the middle; with logging off, the notice and its link instead of it.
    @Test fun anEmptyLogHasItsPlaceholderAndLoggingOffHasItsNoticeAndLink() {
        prefs.edit().putBoolean("logging", true).commit()
        logFile.delete()
        val a = open()
        leaveTouchMode(a)
        render(a, 4)
        assertEquals("", shownLogOf(a))
        val placeholder = onMain { descendants(root(a)).filterIsInstance<TextView>().first { it.text.toString() == context.getString(R.string.logs_placeholder) } }
        onMain {
            assertTrue(placeholder.isShown)
            assertEquals("13 sp", 13f, placeholder.textSize / a.resources.displayMetrics.scaledDensity, 0.5f)
            assertEquals(theme(a).textFaint, placeholder.currentTextColor)
            val box = logBox(a)
            fun middle(view: View) = IntArray(2).also { view.getLocationInWindow(it) }.let { it[0] + view.width / 2f to it[1] + view.height / 2f }
            assertEquals("in the middle of the box", middle(box).first, middle(placeholder).first, 3f)
            assertEquals(middle(box).second, middle(placeholder).second, 3f)
        }
        assertFalse("with logging on there is no notice", shows(a, context.getString(R.string.logs_off)))
        assertNull(onMain { root(a).findViewWithTag<View?>("logs:enable") })
        prefs.edit().putBoolean("logging", false).commit()
        render(a, 4)
        assertTrue(shows(a, context.getString(R.string.logs_off)))
        onMain {
            val notice = descendants(root(a)).filterIsInstance<TextView>().first { it.text.toString() == context.getString(R.string.logs_off) }
            assertEquals("in warn", theme(a).warn, notice.currentTextColor)
            assertFalse("and no placeholder while it is off", descendants(root(a)).filterIsInstance<TextView>()
                .any { it.text.toString() == context.getString(R.string.logs_placeholder) && it.isShown })
        }
        assertEquals(context.getString(R.string.logs_turn_on), textOf(a, "logs:enable"))
        click(a, "logs:enable")
        eventually("Turn on turns logging on") { prefs.getBoolean("logging", false) }
        eventually("and the notice goes") { !shows(a, context.getString(R.string.logs_off)) }
    }

    // Clear empties the log at once, and the text alone changes; Copy puts what is shown on the clipboard and says so.
    @Test fun clearEmptiesTheBoxAtOnceAndCopyCopiesWhatIsShown() {
        prefs.edit().putBoolean("logging", true).commit()
        writeLog(entryLine, LogLines.entry(1_793_104_500_000L, java.time.ZoneId.of("UTC"), LogLines.ERROR, "It failed"))
        val a = open()
        render(a, 4)
        val clipboard = context.getSystemService(android.content.ClipboardManager::class.java)
        click(a, "log:copy")
        eventually("Copy says so") { context.getString(R.string.text_log_copied) in toastTexts(a) }
        // Android answers an app that has not the input focus with nothing (open() waits for it), so it is asked until it answers.
        var copied: String? = null
        eventually("The clipboard holds what was copied") {
            copied = onMain { clipboard.primaryClip?.getItemAt(0)?.coerceToText(context)?.toString() }
            copied != null
        }
        assertEquals("what is shown, line by line", "12:34:56 INFO Tunnel up\n12:35:00 ERROR It failed", copied)
        val generation = renderGeneration(a)
        click(a, "log:clear")
        eventually("The file is gone") { !logFile.exists() }
        assertEquals("and the box is empty at once", "", shownLogOf(a))
        assertEquals("by changing the text alone, never by building the page again", generation, renderGeneration(a))
        assertTrue("with its placeholder", onMain { descendants(root(a)).filterIsInstance<TextView>().first { it.text.toString() == context.getString(R.string.logs_placeholder) }.isShown })
        // An empty log is copied as it is (the desktop's Copy puts logText() on the clipboard whatever it holds) and "Log copied" is said
        // again: what was copied before is replaced by nothing, not kept.
        click(a, "log:copy")
        eventually("An empty log is copied, and the clipboard holds nothing of the old one") {
            onMain { clipboard.primaryClip?.getItemAt(0)?.coerceToText(context)?.toString() } == ""
        }
        eventually("and it says so, as for any other") { context.getString(R.string.text_log_copied) in toastTexts(a) }
    }

    // The box follows the file while the page is shown, by changing its text alone, and stops when the page is left.
    @Test fun theBoxFollowsTheFileWithoutBuildingThePageAgain() {
        prefs.edit().putBoolean("logging", true).commit()
        writeLog(entryLine)
        val a = open()
        render(a, 4)
        val generation = renderGeneration(a)
        assertEquals("12:34:56 INFO Tunnel up", shownLogOf(a))
        LogFile.append(logFile, LogLines.entry(1_793_104_503_000L, java.time.ZoneId.of("UTC"), LogLines.INFO, "A line that came while the page was up"))
        // 1_793_104_503_000 is seven seconds after the first line's 12:34:56.
        eventually("A new line shows within a few seconds", 6_000) { shownLogOf(a).endsWith("12:35:03 INFO A line that came while the page was up") }
        assertEquals("12:34:56 INFO Tunnel up\n12:35:03 INFO A line that came while the page was up", shownLogOf(a))
        assertEquals("the page was not built again", generation, renderGeneration(a))
        // Left, it stops looking: nothing keeps ticking against views that are gone.
        render(a, 3)
        assertNull(onMain { root(a).findViewWithTag<View?>("log:text") })
        LogFile.append(logFile, LogLines.entry(1_793_104_504_000L, java.time.ZoneId.of("UTC"), LogLines.INFO, "while on Settings"))
        SystemClock.sleep(1_500)
        render(a, 4)
        assertTrue("and the page shows it when it comes back", shownLogOf(a).endsWith("while on Settings"))
    }

    // Auto-scroll keeps the end in view; off, the box stays where it is as lines arrive; on again, it goes to the end.
    @Test fun autoScrollKeepsTheEndInViewAndStaysOffWhenTurnedOff() {
        prefs.edit().putBoolean("logging", true).commit()
        writeLog(*Array(400) { LogLines.entry(1_793_104_496_000L + it * 1000L, java.time.ZoneId.of("UTC"), LogLines.INFO, "line number $it of the log") })
        val a = open()
        render(a, 4)
        assertTrue("on by default", onMain { root(a).findViewWithTag<ToggleView>("logs:autoscroll").isChecked })
        assertEquals(context.getString(R.string.logs_auto_scroll), onMain { root(a).findViewWithTag<ToggleView>("logs:autoscroll").contentDescription.toString() })
        eventually("The end is in view") { scrollEnd(a) > 0 && onMain { logBox(a).scrollY } == scrollEnd(a) }
        LogFile.append(logFile, LogLines.entry(1_793_105_000_000L, java.time.ZoneId.of("UTC"), LogLines.INFO, "the newest line"))
        eventually("A new line keeps the end in view", 6_000) { shownLogOf(a).endsWith("the newest line") && onMain { logBox(a).scrollY } == scrollEnd(a) }
        click(a, "logs:autoscroll")
        eventually("off") { !onMain { root(a).findViewWithTag<ToggleView>("logs:autoscroll").isChecked } }
        onMain { logBox(a).scrollTo(0, 0) }
        LogFile.append(logFile, LogLines.entry(1_793_105_001_000L, java.time.ZoneId.of("UTC"), LogLines.INFO, "one more"))
        eventually("The new line is in the text", 6_000) { shownLogOf(a).endsWith("one more") }
        instrumentation.waitForIdleSync()
        assertEquals("but the box has not moved", 0, onMain { logBox(a).scrollY })
        click(a, "logs:autoscroll")
        eventually("On again, it goes to the end") { onMain { logBox(a).scrollY } == scrollEnd(a) && scrollEnd(a) > 0 }
    }

    // On a TV the text is not selectable, so Down from Copy reaches the box, whose focus is surfaceHover (the desktop's log box draws no border),
    // and Down scrolls it. Up at the top and Down at the end leave it, for the control above and the one below.
    @Test fun aRemoteReachesTheLogBoxAndScrollsIt() {
        assumeTrue("A remote drives a TV", isTv)
        prefs.edit().putBoolean("logging", true).commit()
        writeLog(*Array(300) { LogLines.entry(1_793_104_496_000L + it * 1000L, java.time.ZoneId.of("UTC"), LogLines.INFO, "line number $it of the log") })
        val a = open()
        leaveTouchMode(a)
        render(a, 4)
        assertFalse(onMain { root(a).findViewWithTag<TextView>("log:text").isTextSelectable })
        // An arrow scrolls the box by an animation of a quarter of a second, which goes on after the key: what a test does to the box meanwhile, or
        // the next key, finds it half way. Waited out before each step.
        fun still() {
            var last = -1
            var steady = 0
            eventually("The box stops scrolling") {
                val now = onMain { logBox(a).scrollY }
                if (now == last) steady++ else { steady = 0; last = now }
                steady >= 6
            }
        }
        focusOn(a, "log:copy")
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_DOWN)
        eventually("Down from Copy lands on the box") { focusedKey(a) == "log:box" }
        onMain { logBox(a).scrollTo(0, 0) }
        still()
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_DOWN)
        eventually("and the next Down scrolls it") { onMain { logBox(a).scrollY } > 0 }
        still()
        val look = onMain { focusLook(logBox(a)) }
        assertEquals("the focus is surfaceHover", theme(a).surfaceHover, look.focusedFill)
        assertEquals("and no border", theme(a).surfaceHover, look.focusedEdge)
        assertEquals(theme(a).surface, look.restEdge)
        // Up from the top goes back to the links, and Down at its end to the toggle.
        onMain { logBox(a).scrollTo(0, 0) }
        still()
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_UP)
        eventually("Up from the top leaves the box") { focusedKey(a) != "log:box" }
        assertTrue("for a link above it: ${focusedKey(a)}", focusedKey(a) in listOf("log:clear", "log:copy"))
        focusOn(a, "log:box")
        val end = scrollEnd(a)
        onMain { logBox(a).scrollTo(0, end) }
        still()
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_DOWN)
        eventually("Down at the end reaches Auto-scroll") { focusedKey(a) == "logs:autoscroll" }
        // Part of the way, the arrows scroll it and do not leave.
        focusOn(a, "log:box")
        onMain { logBox(a).scrollTo(0, end / 2) }
        still()
        instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_DPAD_UP)
        SystemClock.sleep(400)
        assertEquals("In the middle an arrow scrolls the box", "log:box", focusedKey(a))
        assertTrue(onMain { logBox(a).scrollY } < end / 2)
    }

    // The service writes the lines the page shows: date, time, level and message with tabs between, the level one of three.
    @Test fun aLineTheServiceWritesIsTheDesktopsFormat() {
        val line = LogLines.entry(System.currentTimeMillis(), java.time.ZoneId.systemDefault(), LogLines.WARN, "VPN settings rejected")
        assertTrue(line, Regex("\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\t(INFO|WARN|ERROR)\\t.+").matches(line))
        assertEquals("shown without the date", line.substring(11, 19) + " WARN VPN settings rejected", LogLines.shown(line))
    }
}
