package org.freetunnel.android

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.Typeface
import android.graphics.drawable.Drawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.StateListDrawable
import android.view.ContextThemeWrapper
import android.view.View
import android.view.View.MeasureSpec
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.TextView
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

// The desktop's controls as Ui builds them: the measurements of qml/Toggle.qml, qml/Field.qml,
// qml/components/* and qml/pages/*, in dp, which look each rests in, and that a remote's focus is the look the control has under the
// pointer (the desktop has no ring), never a strong fill the desktop does not have. Built and measured here, off the screen;
// TvDeviceTest checks the real pages.
class UiComponentsDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private val density get() = context.resources.displayMetrics.density
    private fun dp(n: Float) = Math.round(n * density)
    private fun dp(n: Int) = dp(n.toFloat())

    private fun ui(dark: Boolean = true) = Ui(context).apply { update(dark) }
    // The size a view takes in a space this wide and, if it is given, this tall.
    private fun measured(view: View, width: Int = 0, height: Int = 0): View {
        val w = if (width > 0) MeasureSpec.makeMeasureSpec(width, MeasureSpec.AT_MOST) else MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED)
        val h = if (height > 0) MeasureSpec.makeMeasureSpec(height, MeasureSpec.AT_MOST) else MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED)
        view.measure(w, h)
        return view
    }
    private fun assertNear(message: String, expected: Int, actual: Int, delta: Int) =
        assertTrue("$message: expected $expected, was $actual", Math.abs(expected - actual) <= delta)
    private fun onMain(block: () -> Unit) {
        var failure: Throwable? = null
        instrumentation.runOnMainSync { try { block() } catch (t: Throwable) { failure = t } }
        failure?.let { throw it }
    }

    private fun fill(drawable: Drawable) = (drawable as GradientDrawable).color?.defaultColor ?: Color.TRANSPARENT
    // The colour of the middle of the left edge of what a look draws: a border has no getter. What looks() returns is a state of the view's
    // StateListDrawable, which fades its states in and out over 120 ms: the one it has just entered is drawn at an alpha of about 0 for the
    // first frames (the rest state, entered last, was read as transparent: 0, or 3 a moment later). So a copy of it is drawn, at its own alpha.
    private fun edge(drawable: Drawable): Int {
        val copy = (drawable.constantState?.newDrawable()?.mutate() ?: drawable).also { it.alpha = 255 }
        val bitmap = Bitmap.createBitmap(160, 240, Bitmap.Config.ARGB_8888)
        copy.setBounds(0, 0, 160, 240)
        copy.draw(Canvas(bitmap))
        return bitmap.getPixel(0, 120)
    }
    private fun looks(view: View): Map<String, Drawable> {
        val states = view.background as StateListDrawable
        val out = HashMap<String, Drawable>()
        for ((name, state) in listOf("rest" to intArrayOf(), "pressed" to intArrayOf(android.R.attr.state_pressed),
                "focused" to intArrayOf(android.R.attr.state_focused), "activated" to intArrayOf(android.R.attr.state_activated))) {
            states.setState(state)
            out[name] = states.current
        }
        return out
    }

    @Test fun theToggleIs38By22AndFlipsOnATap() {
        onMain {
            val ui = ui()
            val big = measured(ui.toggle(false, false, "t:big")) as ToggleView
            // The track is 38 by 22, and the view is the track and nothing round it: a fill round it would be the ring.
            assertEquals(dp(38), big.measuredWidth)
            assertEquals(dp(22), big.measuredHeight)
            val small = measured(ui.toggle(false, true, "t:small"))
            assertEquals("Logs' toggle is 34 by 20", dp(34), small.measuredWidth)
            assertEquals(dp(20), small.measuredHeight)
            var flips = 0
            var last = false
            big.setListener { _, checked -> flips++; last = checked }
            assertFalse(big.isChecked)
            assertTrue("A click it acts on is a click that was performed", big.performClick())
            assertTrue("A tap flips it", big.isChecked)
            assertEquals(1, flips)
            assertTrue(last)
            big.performClick()
            assertFalse(big.isChecked)
            assertEquals(2, flips)
            big.isEnabled = false
            assertEquals("Disabled it is dimmed", 0.45f, big.alpha, 0.001f)
            assertFalse("and a click on it does nothing", big.performClick())
            assertFalse(big.isChecked)
            assertEquals(2, flips)
            // TalkBack reads it as a switch, with its state. The view is not in a window here, and a view that is not has no class name in
            // its node info (View fills it in only once attached), so the class is asked for itself.
            assertEquals("android.widget.Switch", big.accessibilityClassName.toString())
            val info = big.createAccessibilityNodeInfo()
            assertTrue(info.isCheckable)
            assertEquals("and says which way it is", big.isChecked, info.isChecked)
        }
    }

    // Off, a toggle's track fills inside its 1 px border, as a Qt Rectangle's does, and the border is the translucent black of the desktop's over
    // whatever is behind it: a rim a little darker than the page. Stroked over the fill it was lighter than the page in the dark theme.
    @Test fun anOffToggleHasADarkRimOverThePageAndAFillInsideIt() {
        onMain {
            for (dark in listOf(true, false)) {
                val theme = Theme.of(dark)
                val toggle = measured(ui(dark).toggle(false, false, "t:rim")) as ToggleView
                toggle.layout(0, 0, toggle.measuredWidth, toggle.measuredHeight)
                val bitmap = Bitmap.createBitmap(toggle.width, toggle.height, Bitmap.Config.ARGB_8888)
                toggle.draw(Canvas(bitmap))
                val border = Math.max(1, Math.round(density))
                val middle = toggle.height / 2
                val rim = bitmap.getPixel(border / 2, middle)
                assertNear("The rim is black at the desktop's 0.18, over nothing: the page shows through it", 0x2E, Color.alpha(rim), 3)
                assertEquals("black", 0, Color.red(rim) + Color.green(rim) + Color.blue(rim))
                val track = bitmap.getPixel(border + 3, middle)
                assertEquals("The track is inside it, and opaque", 255, Color.alpha(track))
                assertEquals(theme.toggleOff, track)
            }
        }
    }

    // The desktop gives a toggle no hover, so a remote's focus on it is the nearest thing the desktop has: the row fills with surface, as
    // a config row does while one of its icons has the focus. The toggle itself draws nothing for it, in any state.
    @Test fun aToggleHasNoLookOfItsOwnAndItsRowFillsWithItsFocus() {
        onMain {
            for (dark in listOf(true, false)) {
                val theme = Theme.of(dark)
                val ui = ui(dark)
                val toggle = ui.toggle(true, false, "switch:killswitch")
                val own = looks(toggle)
                for ((state, look) in own) assertEquals("The toggle's own $state is no fill", Color.TRANSPARENT, fill(look))
                val row = ui.toggleRow("Kill switch", "block traffic if the VPN drops", toggle)
                // Its content is 10 and 6 in, and the row goes in its place 10 and 6 beyond the margins, so that nothing moves.
                assertEquals(dp(Ui.FOCUS_ROW_LEFT_DP), row.paddingLeft)
                assertEquals(dp(Ui.FOCUS_ROW_RIGHT_DP), row.paddingRight)
                val place = ui.focusRowLp()
                assertEquals(-dp(10), place.leftMargin)
                assertEquals(-dp(6), place.rightMargin)
                assertFalse("The row is no stop of its own: the toggle is", row.isFocusable)
                assertEquals("switch:killswitch", toggle.tag)
                val rows = looks(row)
                assertEquals("It rests on the page", Color.TRANSPARENT, fill(rows["rest"]!!))
                assertEquals("and fills with surface while the toggle has the focus", theme.surface, fill(rows["activated"]!!))
                assertEquals(dp(8).toFloat(), (rows["activated"] as GradientDrawable).cornerRadius, 0.5f)
                assertFalse(row.isActivated)
                toggle.onFocusChangeListener.onFocusChange(toggle, true)
                assertTrue("The toggle's focus activates its row", row.isActivated)
                toggle.onFocusChangeListener.onFocusChange(toggle, false)
                assertFalse(row.isActivated)
                assertNull("A ring is a foreground", row.foreground)
                // The editor's row and Logs' small one are the same.
                val editor = ui.editorToggleRow("Allow IPv6", ui.toggle(true, false, "editor:ipv6"))
                assertEquals(theme.surface, fill(looks(editor)["activated"]!!))
                val group = ui.toggleGroup("Auto-scroll", 12f, theme.textDim, ui.toggle(true, true, "logs:autoscroll"))
                assertEquals(theme.surface, fill(looks(group)["activated"]!!))
                measured(group)
                assertTrue("It is as wide as its words, never the page: ${group.measuredWidth}", group.measuredWidth < dp(200))
            }
        }
    }

    @Test fun buttonsAre32HighWithTheDesktopsMinimumWidthsAndNoPressedAnimation() {
        onMain {
            val ui = ui()
            val cancel = measured(ui.button("Cancel", Ui.Kind.NEUTRAL, 76, "b:cancel"))
            assertEquals("A dialog's button is 32 high", dp(32), cancel.measuredHeight)
            assertTrue("and at least 76 wide", cancel.measuredWidth >= dp(76))
            val save = measured(ui.button("Save", Ui.Kind.PRIMARY, 88, "b:save"))
            assertTrue("An editor's button is at least 88 wide", save.measuredWidth >= dp(88))
            val long = measured(ui.button("A label that is wider than the minimum", Ui.Kind.NEUTRAL, 76, "b:long")) as TextView
            val words = Math.ceil(long.paint.measureText(long.text.toString()).toDouble()).toInt()
            assertNear("A long label is its width plus 26", words + dp(26), long.measuredWidth, 2)
            assertNull("No pressed animation: Material's lift is off", long.stateListAnimator)
            assertEquals("Normal weight", false, long.typeface.isBold)
        }
    }

    // The desktop's weights are Normal and Medium (500): nothing is bold.
    @Test fun mediumIsNeverBold() {
        onMain {
            val ui = ui()
            val name = ui.medium("DE · Frankfurt", 15f, Color.WHITE)
            assertSame(Fonts.medium(context), name.typeface)
            assertFalse(name.typeface.isBold)
            val mono = ui.mono("log", 11f, Color.WHITE)
            assertFalse(mono.typeface.isBold)
            assertEquals(android.graphics.Typeface.MONOSPACE, mono.typeface)
            assertEquals("A section label is as it is typed", "Rules — bypass VPN", ui.sectionLabel("Rules — bypass VPN").text.toString())
        }
    }

    // One typeface in both apps: Inter, Regular (400) for all text and Medium (500) where the desktop's is Medium, each a file of its own
    // (assets/fonts/inter, which the build copies into res/font), and monospace only where the desktop has it. The theme names Regular, for
    // the text that nothing sets.
    @Test fun textIsSetInInterAndMonospaceOnlyWhereTheDesktopHasIt() {
        onMain {
            val ui = ui()
            val regular = Fonts.regular(context)
            val medium = Fonts.medium(context)
            val sample = "Frankfurt premium \u00b7 \u041f\u0440\u0438\u0432\u0435\u0442, \u043c\u0438\u0440 0123456789"
            fun width(face: Typeface) = Paint().apply { typeface = face; textSize = 100f }.measureText(sample)
            assertNotSame("Medium is a file of its own, not Regular made heavier", regular, medium)
            assertTrue("and Inter Medium sets this text wider than Inter Regular does", width(medium) > width(regular))
            assertTrue("Regular is not the system's face", Math.abs(width(regular) - width(Typeface.DEFAULT)) > 1f)
            assertTrue("nor is Medium the system's", Math.abs(width(medium) - width(Typeface.create("sans-serif-medium", Typeface.NORMAL))) > 1f)
            assertFalse(regular.isBold)
            assertFalse(medium.isBold)

            assertSame("a label", regular, ui.label("Frankfurt", 14f, Color.WHITE).typeface)
            assertSame("a button", regular, ui.button("Cancel", Ui.Kind.NEUTRAL, 76, "b:cancel").typeface)
            assertSame("a link", regular, ui.link("Copy", 13f, Ui.LinkKind.ACCENT, "log:copy").typeface)
            assertSame("a link with no key", regular, ui.link("Copy", 13f, Ui.LinkKind.ACCENT, null).typeface)
            assertSame("a form's field", regular, ui.lineEdit().typeface)
            assertSame("a page's input", regular, ui.pageInput("profile name", "profile:name").typeface)
            assertSame("a title in Medium", medium, ui.medium("Frankfurt", 15f, Color.WHITE).typeface)
            assertSame("Home's selector", medium, (ui.selector("DE", true, 0, "home:config").getChildAt(0) as TextView).typeface)
            assertEquals("the log", Typeface.MONOSPACE, ui.mono("log", 11f, Color.WHITE).typeface)

            // Text that nothing sets takes the theme's face: Inter Regular, as the desktop's application font is.
            val plain = TextView(ContextThemeWrapper(context, R.style.AppTheme))
            assertEquals("the theme's face is Inter Regular", width(regular), width(plain.typeface), 0.01f)
            assertTrue("and not the system's", Math.abs(width(plain.typeface) - width(Typeface.DEFAULT)) > 1f)
        }
    }

    @Test fun aFlowWrapsItsChildrenAndSpacesRowsAsItDoesColumns() {
        onMain {
            val flow = FlowLayout(context, dp(6))
            repeat(5) { flow.addView(View(context), FrameLayout.LayoutParams(dp(100), dp(28))) }
            flow.measure(MeasureSpec.makeMeasureSpec(dp(260), MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED))
            flow.layout(0, 0, flow.measuredWidth, flow.measuredHeight)
            // 100 + 6 + 100 fits in 260; a third would not.
            assertEquals("Three rows of two, two and one", 3 * dp(28) + 2 * dp(6), flow.measuredHeight)
            assertEquals(dp(100) + dp(6), flow.getChildAt(1).left)
            assertEquals("The third child starts the second row", 0, flow.getChildAt(2).left)
            assertEquals(dp(28) + dp(6), flow.getChildAt(2).top)
            assertEquals(2 * (dp(28) + dp(6)), flow.getChildAt(4).top)
        }
    }

    @Test fun aChipIs28HighAndItsXIsTwentyTwoWideThreeFromTheLabel() {
        onMain {
            val ui = ui()
            val plain = measured(ui.chip("Default", false, false, null, false))
            assertEquals("A chip is 28 high", dp(28), plain.measuredHeight)
            val withX = ui.chip("Work", false, false, null, false)
            withX.addView(ui.chipX("domain:0", false), ui.chipXLp())
            measured(withX)
            val label = Math.ceil((withX.getChildAt(0) as TextView).paint.measureText("Work").toDouble()).toInt()
            // 11 in, the label, 3, the cell 22 less 8 beyond the padding, 11: the label and 39.
            assertNear("A chip with its X", label + dp(39), withX.measuredWidth, 3)
            assertEquals(dp(28), withX.measuredHeight)
        }
    }

    // A name longer than the row gives way to the X, as the desktop's label is cut at the chip less 39: the X is still there to be pressed.
    @Test fun aLongChipLabelIsCutAndTheXStaysInTheChip() {
        onMain {
            val ui = ui()
            val chip = ui.chip("a-very-long-name.subdomain.another-subdomain.example.com.example.org.example.net", false, false, null, false)
            val x = ui.chipX("domain:0", false)
            chip.addView(x, ui.chipXLp())
            measured(chip, dp(200))
            assertTrue("The chip is no wider than its room: ${chip.measuredWidth}", chip.measuredWidth <= dp(200))
            assertEquals("and its X is its 22", dp(22), x.measuredWidth)
            chip.layout(0, 0, chip.measuredWidth, chip.measuredHeight)
            val label = chip.getChildAt(0) as TextView
            assertEquals("The label ends 3 before the X", label.right + dp(3), x.left)
            assertTrue("and the X is inside the chip: it ends at ${x.right} of ${chip.width}", x.right <= chip.width)
            assertEquals("The label is cut with an ellipsis", android.text.TextUtils.TruncateAt.END, label.ellipsize)
            // The X sits 8 into the chip's padding, which a row would cut it off at: about a dp of its stroke at rest.
            assertFalse("A chip does not clip its X to its padding", chip.clipToPadding)
            // A short one is as wide as the desktop's: its label and 39.
            val short = ui.chip("vk.com", false, false, null, false)
            short.addView(ui.chipX("domain:1", false), ui.chipXLp())
            measured(short, dp(200))
            val words = Math.ceil((short.getChildAt(0) as TextView).paint.measureText("vk.com").toDouble()).toInt()
            assertNear("A short chip is its label and 39", words + dp(39), short.measuredWidth, 3)
        }
    }

    // A chip's X is its own stop, and a remote reaches it with Right from the chip and the chip with Left. The desktop's X is textDim
    // (accentText on an accent chip) and danger under the pointer: a remote's focus on it is that, and on the chip nothing changes the X.
    @Test fun aChipAndItsXReachEachOtherAndTheXTurnsDangerWithTheFocus() {
        onMain {
            val theme = Theme.DARK
            val ui = ui()
            val chip = ui.chip("Work", false, true, "profile:1", false)
            val x = ui.chipX("profile:1:remove", false)
            chip.addView(x, ui.chipXLp())
            ui.linkChipX(chip, x)
            assertEquals(x.id, chip.nextFocusRightId)
            assertEquals(chip.id, x.nextFocusLeftId)
            assertTrue(chip.id != View.NO_ID && x.id != View.NO_ID && chip.id != x.id)
            val tint = x.imageTintList!!
            assertEquals("Resting on a chip, the cross is textDim", theme.textDim, tint.getColorForState(intArrayOf(), 0))
            assertEquals("danger with the focus", theme.danger, tint.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
            assertEquals("and under the pointer", theme.danger, tint.getColorForState(intArrayOf(android.R.attr.state_pressed), 0))
            assertEquals("A chip's focus does not select the cross: it has no look of its own to sit on", false, x.isSelected)
            assertNull("and the chip has no listener that does", chip.onFocusChangeListener)
            for (state in listOf("rest", "focused", "pressed")) assertEquals("The cross has no fill ($state)", Color.TRANSPARENT, fill(looks(x)[state]!!))
            // On an accent chip it is accentText, and danger under the focus too.
            val over = ui.chipX("profile:2:remove", true).imageTintList!!
            assertEquals(theme.accentText, over.getColorForState(intArrayOf(), 0))
            assertEquals(theme.danger, over.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
        }
    }

    // Split's + chip: 34 by 28, radius 13, on surface with a border, a + of 17 in accent; with the focus it is surfaceHover, as a chip is
    // under the pointer, and keeps its border and its +.
    @Test fun thePlusChipIs34By28WithABorderAndFocusesAsAChipDoes() {
        onMain {
            for (dark in listOf(true, false)) {
                val theme = Theme.of(dark)
                val ui = ui(dark)
                val plus = measured(ui.plusChip("New profile", "profile:add")) as TextView
                assertEquals("+", plus.text.toString())
                assertEquals(dp(34), plus.measuredWidth)
                assertEquals(dp(28), plus.measuredHeight)
                val look = looks(plus)
                assertEquals(theme.surface, fill(look["rest"]!!))
                assertEquals("with a border", theme.border, edge(look["rest"]!!))
                assertEquals(theme.surfaceHover, fill(look["pressed"]!!))
                assertEquals(theme.surfaceHover, fill(look["focused"]!!))
                assertEquals("and the border stays", theme.border, edge(look["focused"]!!))
                assertEquals(theme.accent, plus.textColors.defaultColor)
                assertEquals("The + is accent with the focus too", theme.accent, plus.textColors.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
                assertEquals("New profile", plus.contentDescription)
            }
        }
    }

    // The editor's select box: a label over a box that is the control, 34 high, whose border turns accent under the pointer.
    @Test fun aSelectBoxIsAFieldWhoseBorderTurnsAccentUnderThePointerAndWithTheFocus() {
        onMain {
            for (dark in listOf(true, false)) {
                val theme = Theme.of(dark)
                val ui = ui(dark)
                val shown = arrayOfNulls<TextView>(1)
                var opened = 0
                val field = ui.selectField("Split profile", "Default", "editor:profile", shown) { opened++ }
                field.measure(MeasureSpec.makeMeasureSpec(dp(300), MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED))
                val box = field.findViewWithTag<android.view.ViewGroup>("editor:profile")
                assertEquals("The box is 34 high", dp(34), box.measuredHeight)
                assertEquals("and as wide as the field", dp(300), box.measuredWidth)
                assertEquals("Default", shown[0]!!.text.toString())
                assertEquals(theme.text, shown[0]!!.textColors.defaultColor)
                // ▾, drawn at the size the desktop's system font gives it (8 by 7; Inter has none), in textDim.
                val arrow = box.getChildAt(1) as android.widget.ImageView
                assertEquals(dp(8), arrow.drawable.intrinsicWidth)
                assertEquals(theme.textDim, arrow.imageTintList!!.defaultColor)
                val look = looks(box)
                assertEquals(theme.inputBg, fill(look["rest"]!!))
                assertEquals(theme.inputBorder, edge(look["rest"]!!))
                assertEquals("accent under the pointer", theme.accent, edge(look["pressed"]!!))
                assertEquals("and with the focus", theme.accent, edge(look["focused"]!!))
                assertEquals("on the same fill: the desktop's focus is the border alone", theme.inputBg, fill(look["focused"]!!))
                box.performClick()
                assertEquals(1, opened)
            }
        }
    }

    @Test fun aDropdownHasTheValueAndItsArrowAsItsOnlyControl() {
        onMain {
            val ui = ui()
            val row = ui.dropdown("Mode", "Bypass VPN", "split:mode") {}
            val group = row.findViewWithTag<View>("split:mode")
            assertNotNull(group)
            var focusStops = 0
            fun walk(view: View) {
                if (view.isFocusable) focusStops++
                if (view is android.view.ViewGroup) for (i in 0 until view.childCount) walk(view.getChildAt(i))
            }
            walk(row)
            assertEquals("Only the value and its ▾ are a stop, not the label", 1, focusStops)
            measured(row, dp(360))
            assertTrue("The row is 42 high at least", row.measuredHeight >= dp(42))
            assertTrue("The control hugs its value and its arrow", group.measuredWidth < dp(150))
            // As high as the row, as the desktop's value column is, and what a popup for its choices opens under (MainActivity.anchorRect).
            assertTrue(group is Ui.DropdownValue)
            assertEquals("and as high as the row", row.measuredHeight, group.measuredHeight)
            assertEquals("Its ▾ is drawn, 8 across", dp(8), (group as android.view.ViewGroup).getChildAt(2).let { (it as android.widget.ImageView).drawable.intrinsicWidth })
        }
    }

    // The rest: what a remote's focus shows on a nav tile, a row, a menu row, the header's buttons, Home's + and a dropdown's value is
    // what the pointer shows on them. The current tab, which the desktop leaves as it is under the pointer, keeps its infoBg.
    @Test fun aRemotesFocusOnTheOtherControlsIsTheirHoverLook() {
        onMain {
            for (dark in listOf(true, false)) {
                val theme = Theme.of(dark)
                val ui = ui(dark)
                val tile = looks(View(context).also { ui.focus(it, "tab:1", Ui.Look.TILE) })
                assertEquals(Color.TRANSPARENT, fill(tile["rest"]!!))
                assertEquals("A tile under the pointer is surface", theme.surface, fill(tile["pressed"]!!))
                assertEquals("and with the focus", theme.surface, fill(tile["focused"]!!))
                val current = looks(View(context).also { ui.focus(it, "tab:0", Ui.Look.TILE, Ui.F_CURRENT) })
                assertEquals("The current tab rests on infoBg", theme.infoBg, fill(current["rest"]!!))
                assertEquals("and keeps it under the pointer and with the focus, which only grow it", theme.infoBg, fill(current["pressed"]!!))
                assertEquals(theme.infoBg, fill(current["focused"]!!))
                for ((look, radius) in listOf(Ui.Look.ROW to 8, Ui.Look.MENU_ROW to 6, Ui.Look.PILL to 8, Ui.Look.ICON to 6)) {
                    val seen = looks(View(context).also { ui.focus(it, "t:$look", look) })
                    assertEquals("$look rests on the page", Color.TRANSPARENT, fill(seen["rest"]!!))
                    assertEquals("$look under the pointer", theme.surface, fill(seen["pressed"]!!))
                    assertEquals("and with the focus", theme.surface, fill(seen["focused"]!!))
                    assertEquals("$look's radius", dp(radius).toFloat(), (seen["focused"] as GradientDrawable).cornerRadius, 0.5f)
                }
                // The icons of the header's buttons and of Home's + stay accent while the button is filled.
                for (button in listOf(ui.iconButton(R.drawable.ic_add, 22, "Add", "configs:add"), ui.smallIconButton(R.drawable.ic_add_home, 14, "Add", "home:add"))) {
                    val tint = (button.getChildAt(0) as android.widget.ImageView).imageTintList!!
                    assertEquals(theme.accent, tint.getColorForState(intArrayOf(), 0))
                    assertEquals(theme.accent, tint.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
                }
                // A dropdown's value and its ▾ go from textDim to the text colour, with no fill.
                val row = ui.dropdown("Theme", "System", "settings:theme") {}
                val group = row.findViewWithTag<android.view.ViewGroup>("settings:theme")
                for (state in listOf("rest", "pressed", "focused")) assertEquals("A value has no fill ($state)", Color.TRANSPARENT, fill(looks(group)[state]!!))
                val shown = group.getChildAt(0) as TextView
                assertEquals(theme.textDim, shown.textColors.defaultColor)
                assertEquals(theme.text, shown.textColors.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
                assertEquals(theme.text, shown.textColors.getColorForState(intArrayOf(android.R.attr.state_pressed), 0))
                val arrow = (group.getChildAt(2) as android.widget.ImageView).imageTintList!!
                assertEquals(theme.textDim, arrow.getColorForState(intArrayOf(), 0))
                assertEquals(theme.text, arrow.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
                assertEquals(theme.text, arrow.getColorForState(intArrayOf(android.R.attr.state_pressed), 0))
            }
        }
    }

    // A control rests as the desktop draws it however the screen is driven, and its focus is its hover: a primary button is accent with
    // accentText and goes to primaryHover, a destructive one danger with white and destructiveHover, a dialog's neutral one surface and
    // surfaceHover, the editor's Cancel its page with a border and surface, a chip surface and surfaceHover, and a selected chip, which
    // the desktop leaves as it is, accent with accentText and no mark, and primaryHover with the focus alone.
    @Test fun buttonsAndChipsRestAsTheDesktopDrawsThemAndFocusAsTheyAreUnderThePointer() {
        onMain {
            for (dark in listOf(true, false)) {
                val theme = Theme.of(dark)
                val ui = ui(dark)
                val primary = ui.button("Save", Ui.Kind.PRIMARY, 88, "b:p")
                val primaryLooks = looks(primary)
                assertEquals("A primary button rests on the accent", theme.accent, fill(primaryLooks["rest"]!!))
                assertEquals("it goes darker under the pointer", theme.primaryHover, fill(primaryLooks["pressed"]!!))
                assertEquals("and with the focus", theme.primaryHover, fill(primaryLooks["focused"]!!))
                assertEquals("its words are accentText in every state", theme.accentText, (primary as TextView).textColors.defaultColor)
                assertEquals(theme.accentText, primary.textColors.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
                val destructive = ui.button("Delete", Ui.Kind.DESTRUCTIVE, 76, "b:d")
                val dangerLooks = looks(destructive)
                assertEquals("A destructive button rests on danger", theme.danger, fill(dangerLooks["rest"]!!))
                assertEquals(theme.destructiveHover, fill(dangerLooks["pressed"]!!))
                assertEquals(theme.destructiveHover, fill(dangerLooks["focused"]!!))
                assertEquals("with a white label, always", theme.onDanger, (destructive as TextView).textColors.defaultColor)
                assertEquals(theme.onDanger, destructive.textColors.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
                val neutral = looks(ui.button("Cancel", Ui.Kind.NEUTRAL, 76, "b:n"))
                assertEquals(theme.surface, fill(neutral["rest"]!!))
                assertEquals(theme.surfaceHover, fill(neutral["pressed"]!!))
                assertEquals(theme.surfaceHover, fill(neutral["focused"]!!))
                val secondary = looks(ui.button("Cancel", Ui.Kind.SECONDARY, 88, "b:s"))
                assertEquals("The editor's Cancel rests on the page", theme.bg, fill(secondary["rest"]!!))
                assertEquals(theme.border, edge(secondary["rest"]!!))
                assertEquals(theme.surface, fill(secondary["pressed"]!!))
                assertEquals(theme.surface, fill(secondary["focused"]!!))
                assertEquals("and keeps its border", theme.border, edge(secondary["focused"]!!))
                val selected = looks(ui.chip("Work", true, true, "profile:1", false))
                assertEquals("The selected chip rests on the accent", theme.accent, fill(selected["rest"]!!))
                assertEquals("and the pointer changes nothing", theme.accent, fill(selected["pressed"]!!))
                assertEquals("the focus alone goes darker, as a primary button does", theme.primaryHover, fill(selected["focused"]!!))
                val selectedChip = ui.chip("Work", true, true, "profile:1", false)
                val label = selectedChip.getChildAt(0) as TextView
                assertEquals("accentText on it", theme.accentText, label.textColors.defaultColor)
                assertEquals("with no mark after the name", "Work", label.text.toString())
                val other = looks(ui.chip("Default", false, true, "profile:0", false))
                assertEquals(theme.surface, fill(other["rest"]!!))
                assertEquals(theme.surfaceHover, fill(other["pressed"]!!))
                assertEquals(theme.surfaceHover, fill(other["focused"]!!))
            }
        }
    }

    // A text field keeps the desktop's own focus, an accent border, on the same fill.
    @Test fun aFieldTurnsItsBorderAccentWithTheFocus() {
        onMain {
            for (dark in listOf(true, false)) {
                val theme = Theme.of(dark)
                val field = looks(ui(dark).pageInput("domains, comma-separated, then Enter", "domains:input"))
                assertEquals(theme.inputBg, fill(field["rest"]!!))
                assertEquals(theme.inputBorder, edge(field["rest"]!!))
                assertEquals(theme.accent, edge(field["focused"]!!))
                assertEquals("The fill stays, whatever drives the screen", theme.inputBg, fill(field["focused"]!!))
            }
            val ui = ui()
            val input = EditText(context)
            val box = ui.field("Name", input, "editor:name")
            measured(box, dp(300))
            assertEquals("A field's box is 34 high", dp(34), input.measuredHeight)
        }
    }

    // The desktop hides a field's placeholder while the field has the focus, and shows it again when the focus is gone with nothing
    // typed (Field.qml: visible: text.length === 0 && !activeFocus). The new profile's name and the picker's search keep theirs.
    // And the cursor is the text colour, a dp wide: the system's teal is in no palette of the app.
    @Test fun aPlaceholderGoesWithTheFocusAndTheCursorIsTheTextColour() {
        onMain {
            for (dark in listOf(true, false)) {
                val theme = Theme.of(dark)
                val ui = ui(dark)
                val domains = ui.pageInput("domains, comma-separated, then Enter", "domains:input")
                assertEquals("domains, comma-separated, then Enter", domains.hint.toString())
                domains.onFocusChangeListener.onFocusChange(domains, true)
                assertNull("No placeholder with the focus", domains.hint)
                domains.onFocusChangeListener.onFocusChange(domains, false)
                assertEquals("and it is back without it", "domains, comma-separated, then Enter", domains.hint.toString())
                val name = ui.pageInput("profile name, then Enter", "profile:name", true)
                assertNull("A field that keeps its placeholder has no listener to take it", name.onFocusChangeListener)
                val editor = ui.field("Name", ui.lineEdit().also { it.hint = "Germany · Frankfurt" }, "editor:name").findViewWithTag<EditText>("editor:name")
                editor.onFocusChangeListener.onFocusChange(editor, true)
                assertNull(editor.hint)
                editor.onFocusChangeListener.onFocusChange(editor, false)
                assertEquals("Germany · Frankfurt", editor.hint.toString())
                assertTrue("A field of one line leaves with Up and Down", domains is Ui.LineEdit && editor is Ui.LineEdit)
                if (android.os.Build.VERSION.SDK_INT >= 29) {
                    val caret = domains.textCursorDrawable as GradientDrawable
                    assertEquals("The cursor is the text colour", theme.text, caret.color!!.defaultColor)
                    assertEquals("a dp wide", Math.max(1, dp(1)), caret.intrinsicWidth)
                }
            }
        }
    }

    // A link is its words, wrap_content, and its focus is its hover: the other colour and an underline, with no fill, as the desktop's
    // link is under the pointer. A danger link goes to its lighter.
    @Test fun aLinkHugsItsWordsAndItsFocusIsItsHoverColourUnderlined() {
        onMain {
            val theme = Theme.DARK
            val ui = ui()
            val link = measured(ui.link("Recommended for Russia", 12f, Ui.LinkKind.ACCENT, "domains:russia"), dp(400)) as TextView
            val words = Math.ceil(link.paint.measureText(link.text.toString()).toDouble()).toInt()
            assertNear("A link is its words and a little around them", words + link.paddingLeft + link.paddingRight, link.measuredWidth, 2)
            val focused = intArrayOf(android.R.attr.state_focused)
            assertEquals(theme.accent, link.textColors.defaultColor)
            assertEquals("An accent link goes to the text colour", theme.text, link.textColors.getColorForState(focused, 0))
            for (state in listOf("rest", "pressed", "focused")) assertEquals("A link has no fill ($state)", Color.TRANSPARENT, fill(looks(link)[state]!!))
            val danger = ui.link("Clear all", 12f, Ui.LinkKind.DANGER, "domains:clear")
            assertEquals(theme.danger, danger.textColors.defaultColor)
            assertEquals("A danger link goes to its lighter", theme.dangerLinkHover, danger.textColors.getColorForState(focused, 0))
            assertEquals(theme.dangerLinkHover, danger.textColors.getColorForState(intArrayOf(android.R.attr.state_pressed), 0))
            val text = ui.link("Add a config", 15f, Ui.LinkKind.TEXT, "configs:hint")
            assertEquals(theme.text, text.textColors.defaultColor)
            assertEquals("A text link goes accent under the pointer", theme.accent, text.textColors.getColorForState(intArrayOf(android.R.attr.state_pressed), 0))
            assertEquals("and with the focus", theme.accent, text.textColors.getColorForState(focused, 0))
            val faint = ui.link("FreeTunnel 1.0", 12f, Ui.LinkKind.FAINT, "settings:site")
            assertEquals(theme.textFaint, faint.textColors.defaultColor)
            assertEquals(theme.accent, faint.textColors.getColorForState(focused, 0))
            // Underlined where the desktop's is, under the pointer (TvDeviceTest looks at it with the focus, in a window).
            val hot = ui.link("Copy", 13f, Ui.LinkKind.ACCENT, "log:copy") as Ui.LinkText
            val underline = android.graphics.Paint.UNDERLINE_TEXT_FLAG
            assertEquals(0, hot.paintFlags and underline)
            hot.isPressed = true
            assertTrue("pressed", hot.paintFlags and underline != 0)
            hot.isPressed = false
            assertEquals(0, hot.paintFlags and underline)
            hot.isHovered = true
            assertTrue("hovered", hot.paintFlags and underline != 0)
            // A line that does nothing at the moment (the update row while it checks) has no pointer look: plain in the text colour, not
            // underlined. A remote's focus stays on it, though, and shows: the hover colour, underlined (the third place the focus shows what
            // the pointer does not).
            val dormant = ui.link("Checking…", 14f, Ui.LinkKind.TEXT, "settings:update") as Ui.LinkText
            dormant.setDormant(true)
            assertEquals("A dormant link rests in its colour", theme.text, dormant.textColors.getColorForState(intArrayOf(), 0))
            assertEquals("and stays in it under the pointer", theme.text, dormant.textColors.getColorForState(intArrayOf(android.R.attr.state_pressed), 0))
            assertEquals("and under a hover", theme.text, dormant.textColors.getColorForState(intArrayOf(android.R.attr.state_hovered), 0))
            assertEquals("but not with the focus, which must show", theme.accent, dormant.textColors.getColorForState(focused, 0))
            dormant.isPressed = true
            assertEquals("and is not underlined under the pointer", 0, dormant.paintFlags and underline)
            dormant.isPressed = false
            dormant.isHovered = true
            assertEquals("nor hovered", 0, dormant.paintFlags and underline)
            dormant.isHovered = false
            dormant.setDormant(false)
            assertEquals("Awake it is the link it was", theme.accent, dormant.textColors.getColorForState(intArrayOf(android.R.attr.state_pressed), 0))
            dormant.isPressed = true
            assertTrue(dormant.paintFlags and underline != 0)
            // The link in a row of a page adds nothing to the row's height: its padding hangs outside it.
            val place = ui.linkLp(true)
            assertEquals(-dp(4), place.topMargin)
            assertEquals(-dp(4), place.bottomMargin)
            assertEquals(-dp(6), place.rightMargin)
            assertEquals(0, ui.linkLp(false).rightMargin)
        }
    }

    // The parts of a card (the editor, the app picker, Receive from phone): the header is 48 high with the arrow in a cell
    // of its own and the title in Medium; the editor's text area has a Field's look in monospace 12 and grows from its minimum; its
    // toggle row is 32 high; and a box of text that a remote scrolls rests on surface and goes to surfaceHover, with no border.
    @Test fun aCardsHeaderTextAreaToggleRowAndBoxOfTextAreAsTheDesktopDrawsThem() {
        onMain {
            val theme = Theme.DARK
            val ui = ui()
            val header = measured(ui.cardHeader("New config", "editor:back", "Back") {}, dp(372)) as android.view.ViewGroup
            assertEquals("A card's header is 48 high", dp(48), header.measuredHeight)
            // ←, drawn as Inter draws it at the desktop's 20 px, 14.6 across, in a cell 6 larger all round.
            val arrow = header.findViewWithTag<android.widget.ImageView>("editor:back")
            assertNear("The ← is drawn 14.6 across", dp(14.6f), arrow.drawable.intrinsicWidth, 1)
            assertNear("and 12.7 high", dp(12.7f), arrow.drawable.intrinsicHeight, 1)
            assertEquals(dp(6), arrow.paddingLeft)
            val tint = arrow.imageTintList!!
            assertEquals("The arrow is textDim", theme.textDim, tint.defaultColor)
            assertEquals("and the text colour under the pointer", theme.text, tint.getColorForState(intArrayOf(android.R.attr.state_pressed), 0))
            assertEquals("and with a remote's focus", theme.text, tint.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
            // It is the focus stop itself: its tint follows its own states, never the header's, which is never focused, pressed or hovered.
            assertFalse("The arrow does not take the header's states", arrow.isDuplicateParentStateEnabled)
            assertTrue("and is a stop", arrow.isFocusable)
            for (state in listOf("rest", "pressed", "focused")) assertEquals("The arrow has no fill ($state): it only tints", Color.TRANSPARENT, fill(looks(arrow)[state]!!))
            assertEquals("Back", arrow.contentDescription.toString())
            val title = header.getChildAt(1) as TextView
            assertSame("The title is Medium, never bold", Fonts.medium(context), title.typeface)
            assertEquals("New config", title.text.toString())

            val area = EditText(context)
            // The certificate: a label with "Load from file…" at its right, over a box 70 high whose text scrolls.
            val load = ui.link("Load from file…", 12f, Ui.LinkKind.ACCENT, "editor:certfile")
            val field = ui.fieldArea("Certificate (PEM) · optional", load, area, "editor:cert", 70)
            measured(field, dp(300))
            assertEquals("The text area is 70 high", dp(70), area.measuredHeight)
            assertEquals("and the link's padding adds nothing to its label's line", -dp(4), (load.layoutParams as android.view.ViewGroup.MarginLayoutParams).topMargin)
            assertEquals(android.graphics.Typeface.MONOSPACE, area.typeface)
            val areaLooks = looks(area)
            assertEquals("It is a Field: inputBg with an inputBorder", theme.inputBg, fill(areaLooks["rest"]!!))
            assertEquals(theme.accent, edge(areaLooks["focused"]!!))

            val row = measured(ui.editorToggleRow("Skip certificate check", ui.toggle(false, false, "editor:skip")), dp(300))
            assertTrue("A toggle row of the editor is 32 high at least", row.measuredHeight >= dp(32))
            assertEquals("It fills with surface while the toggle has the focus", theme.surface, fill(looks(row)["activated"]!!))

            val box = android.widget.ScrollView(context)
            ui.focus(box, "log:box", Ui.Look.BOX)
            val boxLooks = looks(box)
            assertEquals("A box of text rests on surface", theme.surface, fill(boxLooks["rest"]!!))
            assertEquals("without a border", theme.surface, edge(boxLooks["rest"]!!))
            // The desktop's log box has no border under the pointer or with the focus: like any control that rests on surface, it goes to surfaceHover.
            assertEquals("and with the focus is surfaceHover", theme.surfaceHover, fill(boxLooks["focused"]!!))
            assertEquals("still with no border", theme.surfaceHover, edge(boxLooks["focused"]!!))
        }
    }

    // Every icon is made from the desktop's SVG path data: each loads, at the size the desktop draws it.
    @Test fun everyVectorDrawableLoads() {
        for ((id, size) in listOf(R.drawable.ic_connection to 24f, R.drawable.ic_network to 24f, R.drawable.ic_settings to 24f,
                R.drawable.ic_log to 24f, R.drawable.ic_ping to 24f, R.drawable.ic_export to 24f, R.drawable.ic_edit to 24f,
                R.drawable.ic_close to 24f, R.drawable.ic_logo to 100f, R.drawable.ic_add to 22f, R.drawable.ic_add_home to 14f,
                // The characters the desktop draws as text (▾ ✓ ↓ ↑ ←): ▾ at the size its system font gives it, the others as Inter
                // draws them at the sizes the desktop's QML sets them in (✓ 14 px, ↓ and ↑ 14, ← 20), each as wide as its ink.
                R.drawable.ic_dropdown to 8f, R.drawable.ic_check to 9.6f, R.drawable.ic_arrow_down to 8.9f, R.drawable.ic_arrow_up to 8.9f,
                R.drawable.ic_back to 14.6f)) {
            val drawable = context.getDrawable(id)
            assertNotNull(drawable)
            // Its own size, in dp: a pixel either way is the rounding.
            assertNear("Drawable $id is $size wide", dp(size), drawable!!.intrinsicWidth, 1)
            // Drawn: something is painted in it, not just parsed.
            val bitmap = Bitmap.createBitmap(dp(size), dp(size), Bitmap.Config.ARGB_8888)
            drawable.setBounds(0, 0, dp(size), dp(size))
            drawable.draw(Canvas(bitmap))
            var painted = 0
            for (x in 0 until bitmap.width) for (y in 0 until bitmap.height) if (Color.alpha(bitmap.getPixel(x, y)) != 0) painted++
            assertTrue("Drawable $id paints nothing", painted > 0)
        }
    }

    @Test fun iconsTakeTheirColourFromAndOnlyFromTheirTint() {
        onMain {
            val ui = ui()
            val x = ui.chipX("domain:0", false)
            assertEquals("A ChipX is textDim at rest", Theme.DARK.textDim, x.imageTintList!!.defaultColor)
            assertEquals("and danger with the focus, as under the pointer: a tint, not a circle", Theme.DARK.danger,
                x.imageTintList!!.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
            assertEquals("Danger under the pointer", Theme.DARK.danger,
                x.imageTintList!!.getColorForState(intArrayOf(android.R.attr.state_pressed), 0))
        }
    }

    // Home's selector is its name and ▾ and no wider, 6 apart, in the text colour and the accent under the pointer, in Medium,
    // never bold; with the focus the same, the name underlined, and no box round them. Without configs it is "Add a config" alone.
    @Test fun homesSelectorIsAsWideAsItsNameAndArrowAndFocusesAsALink() {
        onMain {
            val theme = Theme.DARK
            val ui = ui()
            val selector = measured(ui.selector("DE \u00b7 Frankfurt", true, 0, "home:config"), dp(400)) as android.view.ViewGroup
            val name = selector.getChildAt(0) as TextView
            val arrow = selector.getChildAt(1) as android.widget.ImageView
            assertEquals("The ▾ is drawn, 8 across", dp(8), arrow.drawable.intrinsicWidth)
            assertSame("The name is Medium, never bold", Fonts.medium(context), name.typeface)
            assertEquals(theme.text, name.textColors.defaultColor)
            assertEquals("and the accent under the pointer", theme.accent, name.textColors.getColorForState(intArrayOf(android.R.attr.state_hovered), 0))
            assertEquals("The arrow follows it", theme.accent, arrow.imageTintList!!.getColorForState(intArrayOf(android.R.attr.state_hovered), 0))
            assertEquals("and with the focus", theme.accent, name.textColors.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
            assertEquals(theme.accent, arrow.imageTintList!!.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
            // No padding: the desktop's selector is its name, 6 and the arrow, and 24 high, so that the + is 12 from the arrow (6 and its own 6).
            assertNear("As wide as the name, 6 and the arrow", name.measuredWidth + dp(6) + arrow.measuredWidth, selector.measuredWidth, 2)
            assertEquals("and 24 high", dp(24), selector.measuredHeight)
            assertEquals(0, selector.paddingLeft + selector.paddingTop + selector.paddingRight + selector.paddingBottom)
            assertTrue("Far from the width of the screen", selector.measuredWidth < dp(300))
            val looks = looks(selector)
            assertEquals("It rests on the page", Color.TRANSPARENT, fill(looks["rest"]!!))
            assertEquals("and has no box for its focus: the words are", Color.TRANSPARENT, fill(looks["focused"]!!))
            // A long name is cut at the room it is given, and the arrow stays after what shows.
            val long = measured(ui.selector("A very very very long name of a server that goes on", true, dp(120), "home:config"), dp(400)) as android.view.ViewGroup
            assertTrue("The name takes at most the room", (long.getChildAt(0) as TextView).measuredWidth <= dp(120))
            assertEquals(android.text.TextUtils.TruncateAt.END, (long.getChildAt(0) as TextView).ellipsize)
            // With nothing to choose from it is the words alone.
            val empty = ui.selector("Add a config", false, 0, "home:config")
            assertEquals("No \u25be", 1, empty.childCount)
        }
    }

    // A speed tile: the arrow in its colour and the number in Medium 15; a badge: 11 in Medium on infoBg, 8 in, the same on a row that
    // is filled as on one that is not.
    @Test fun aSpeedTileAndABadgeAreAsTheDesktopDrawsThem() {
        onMain {
            val theme = Theme.DARK
            val ui = ui()
            val value = arrayOfNulls<TextView>(1)
            val tile = ui.speedTile(R.drawable.ic_arrow_down, theme.success, value)
            // ↓ drawn 8.9 across and 10.3 high, as Inter draws it at the desktop's 14 px, in the colour of the speed.
            val arrow = tile.getChildAt(0) as android.widget.ImageView
            assertEquals(theme.success, arrow.imageTintList!!.defaultColor)
            assertNear("The ↓ is 8.9 across", dp(8.9f), arrow.drawable.intrinsicWidth, 1)
            assertNear("and 10.3 high", dp(10.3f), arrow.drawable.intrinsicHeight, 1)
            assertSame(value[0], tile.getChildAt(1))
            assertSame("The number is Medium", Fonts.medium(context), value[0]!!.typeface)
            assertEquals(theme.text, value[0]!!.currentTextColor)
            assertFalse("A tile is no control", tile.isFocusable || tile.isClickable)
            assertEquals("It rests on the tile colour", theme.tile, fill(tile.background))

            val badge = measured(ui.badge("connected", theme.success)) as TextView
            assertSame(Fonts.medium(context), badge.typeface)
            assertEquals(theme.success, badge.textColors.defaultColor)
            assertEquals("It does not change with a row's focus", theme.success, badge.textColors.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
            val words = Math.ceil(badge.paint.measureText("connected").toDouble()).toInt()
            assertNear("8 in from each side", words + dp(16), badge.measuredWidth, 2)
            assertEquals("on infoBg", theme.infoBg, fill(badge.background))
        }
    }

    // A config row: its logo, its name taking what is left, the three icons in cells 26 wide at the right 6 from the edge, each 40 high in the
    // 48 of the row; it rests on the page, on surface under the pointer, with the focus and while one of its icons has the focus; an icon
    // changes its colour alone (the text colour, danger for the close icon), on the row or off it.
    @Test fun aConfigRowIsLaidOutAndLooksAsTheDesktopsDoes() {
        onMain {
            val theme = Theme.DARK
            val ui = ui()
            val row = ConfigRowView(context, ui, 1, "DE \u00b7 Frankfurt", true, "42 ms", "connected", true, "Export config", "Edit config", "Delete")
            row.measure(MeasureSpec.makeMeasureSpec(dp(400), MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(dp(48), MeasureSpec.EXACTLY))
            row.layout(0, 0, dp(400), dp(48))
            assertEquals("config:1:select", row.tag)
            assertEquals(dp(26), row.removeIcon.width)
            assertEquals(dp(40), row.removeIcon.height)
            assertNear("The icons end 6 from the edge", dp(400) - dp(6), row.removeIcon.right, 1)
            assertEquals("side by side", row.removeIcon.left, row.editIcon.right)
            assertEquals(row.editIcon.left, row.exportIcon.right)
            assertTrue("The name has room", (row.getChildAt(1) as TextView).width > dp(100))
            assertNear("The logo is 22", dp(22), row.getChildAt(0).width, 1)
            assertEquals(listOf("config:1:export", "config:1:edit", "config:1:remove"), listOf(row.exportIcon, row.editIcon, row.removeIcon).map { it.tag })
            assertEquals("Edit config", row.editIcon.contentDescription)

            val states = row.background as StateListDrawable
            states.setState(intArrayOf())
            assertEquals("It rests on the page", Color.TRANSPARENT, fill(states.current))
            states.setState(intArrayOf(android.R.attr.state_activated))
            assertEquals("and on surface while an icon of it has the focus", theme.surface, fill(states.current))
            states.setState(intArrayOf(android.R.attr.state_pressed))
            assertEquals("and under the pointer", theme.surface, fill(states.current))
            states.setState(intArrayOf(android.R.attr.state_focused))
            assertEquals("With the focus it is surface too, not a strong fill", theme.surface, fill(states.current))

            for (icon in listOf(row.exportIcon, row.editIcon, row.removeIcon))
                for (state in listOf("rest", "pressed", "focused")) assertEquals("An icon's cell has no fill ($state): the row fills", Color.TRANSPARENT, fill(looks(icon)[state]!!))
            val tint = (row.editIcon.getChildAt(0) as android.widget.ImageView).imageTintList!!
            assertEquals("An icon is textDim", theme.textDim, tint.defaultColor)
            assertEquals("the text colour under the pointer", theme.text, tint.getColorForState(intArrayOf(android.R.attr.state_pressed), 0))
            assertEquals("and with the focus", theme.text, tint.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
            assertEquals("The row's own focus does not change it: the desktop's icons are textDim on a row that is under the pointer",
                theme.textDim, tint.getColorForState(intArrayOf(android.R.attr.state_selected), 0))
            val close = (row.removeIcon.getChildAt(0) as android.widget.ImageView).imageTintList!!
            assertEquals("The close icon turns danger under the pointer", theme.danger, close.getColorForState(intArrayOf(android.R.attr.state_pressed), 0))
            assertEquals("and with the focus", theme.danger, close.getColorForState(intArrayOf(android.R.attr.state_focused), 0))
            // The row fills while one of its icons has the focus, as the desktop's does under the pointer.
            row.exportIcon.onFocusChangeListener.onFocusChange(row.exportIcon, true)
            assertTrue(row.isActivated)
            row.exportIcon.onFocusChangeListener.onFocusChange(row.exportIcon, false)
            assertFalse(row.isActivated)

            // A row without a ping or a badge has neither, and the name takes the room.
            val plain = ConfigRowView(context, ui, 0, "Name", false, null, null, false, "Export config", "Edit config", "Delete")
            assertEquals("logo, name and three icons", 5, plain.childCount)
            assertEquals("The other configs' logo is dim", 0.4f, plain.getChildAt(0).alpha, 0.001f)
        }
    }

    // The desktop's logo has no hover, and a fill behind it washes out its greys (#d0d0d0 .. #454545 on the accent, #4f4f4f in the light
    // theme), so as a control it has no background in any state: its focus is its size (HeroValues), and the nav tile it is in.
    @Test fun theLogoControlHasNoFillForItsFocus() {
        onMain {
            for (dark in listOf(true, false)) {
                val box = FrameLayout(context)
                ui(dark).focus(box, "home:toggle", Ui.Look.HERO)
                val looks = looks(box)
                for (state in listOf("rest", "pressed", "focused")) assertEquals("The logo has no fill ($state)", Color.TRANSPARENT, fill(looks[state]!!))
                assertNull("A remote's focus is not a foreground either", box.foreground)
                assertTrue(box.isFocusable)
            }
        }
    }

    // The Maintenance row's arrow: 20 by 20, a shaft down the middle of its box when it is ↓, and, bent round into ↻, a ring about the
    // pivot of the shape UpdateMotion gives, hollow in the middle; turned by 45 degrees it is drawn elsewhere.
    @Test fun theUpdateArrowDrawsDownTheMiddleAndBendsIntoARing() {
        onMain {
            val size = dp(UpdateArrowView.SIZE_DP)
            assertEquals(20, UpdateArrowView.SIZE_DP)
            val arrow = UpdateArrowView(context, Color.RED)
            arrow.layout(0, 0, size, size)
            fun paint(): Bitmap = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888).also { arrow.draw(Canvas(it)) }
            fun painted(bitmap: Bitmap, xDp: Float, yDp: Float) = Color.alpha(bitmap.getPixel(Math.round(xDp * density), Math.round(yDp * density))) > 100
            val straight = paint()
            val down = UpdateMotion.arrow(0f, 20f, 20f)
            assertTrue("the shaft is down the middle", painted(straight, 10f, down.shaft[2 * 14 + 1]))
            assertFalse("and nothing is at the sides of it", painted(straight, 3f, 10f) || painted(straight, 17f, 10f))
            val pixel = straight.getPixel(Math.round(10f * density), Math.round(down.shaft[2 * 14 + 1] * density))
            assertTrue("in the colour it was given", Color.red(pixel) > 200 && Color.green(pixel) < 60 && Color.blue(pixel) < 60)
            arrow.setBend(1f, false)
            assertEquals(1f, arrow.bend(), 0f)
            val ring = UpdateMotion.arrow(1f, 20f, 20f)
            val bent = paint()
            assertFalse("a ring is hollow in the middle", painted(bent, ring.pivotX, ring.pivotY))
            assertTrue("and painted a radius from the pivot, where the shaft begins", painted(bent, ring.pivotX + UpdateMotion.RADIUS, ring.pivotY))
            assertTrue("and across the circle from it", painted(bent, ring.pivotX - UpdateMotion.RADIUS, ring.pivotY) || painted(bent, ring.pivotX, ring.pivotY - UpdateMotion.RADIUS))
            // Leaned 45 degrees about the pivot, the arrow is not where it was.
            arrow.setTurn(45f, false)
            assertEquals(45f, arrow.turn(), 0f)
            val leaned = paint()
            assertFalse("the tail of the ring has moved", painted(leaned, ring.pivotX + UpdateMotion.RADIUS, ring.pivotY))
            assertEquals("a view that is not on the screen does not spin", false, arrow.spinning())
            arrow.setSpinning(true)
            assertTrue(arrow.spinning())
            assertEquals("and is not turned until it is shown", 0f, arrow.spinAngle(), 0f)
        }
    }

    // The three dots: 2.5 across with 2.5 between, in a row 2 below the middle of the box, resting at the start of a round.
    @Test fun theBusyDotsAreThreeDotsInARowBelowTheMiddle() {
        onMain {
            val size = dp(20)
            val dots = BusyDotsView(context, Color.GREEN)
            dots.layout(0, 0, size, size)
            val bitmap = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888).also { dots.draw(Canvas(it)) }
            fun painted(xDp: Float, yDp: Float) = Color.alpha(bitmap.getPixel(Math.round(xDp * density), Math.round(yDp * density))) > 100
            for (x in listOf(5f, 10f, 15f)) assertTrue("a dot at $x", painted(x, 12f))
            assertFalse("a gap between the first and the second", painted(7.5f, 12f))
            assertFalse("a gap between the second and the third", painted(12.5f, 12f))
            assertFalse("nothing above them while they rest", painted(10f, 6f))
            assertEquals("the round starts at nothing", 0L, dots.millis())
        }
    }

    // A popup turns over above its control where it does not fit under it, before the window's edge and the 8 it keeps clear of it; Home's
    // config dropdown does not (the desktop's cfgPopup is at the selector's bottom plus 6, whatever is left), and shrinks, to scroll, only
    // where the window itself ends.
    @Test fun aNoFlipPopupStaysUnderItsControlWhereTheOthersTurnOver() {
        onMain {
            val width = dp(400)
            val height = dp(300)
            // The panel is 140 by 132; the anchor 24 high at this top; the window keeps 24 clear of its top and 27 of its foot, as a TV does.
            fun placed(placement: Int, anchorTop: Int): Pair<Int, Int> {
                val panel = FrameLayout(context)
                panel.addView(View(context), FrameLayout.LayoutParams(dp(140), dp(132)))
                val layer = OverlayLayer(context, OverlayLayer.Kind.POPUP, View(context), panel)
                layer.setSafe(0, dp(27), 0, dp(27))
                layer.setAnchor(OverlayLayer.Anchor { Rect(dp(130), anchorTop, dp(270), anchorTop + dp(24)) }, placement, dp(6))
                layer.measure(MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY))
                layer.layout(0, 0, width, height)
                return panel.top to panel.height
            }
            val (turnedTop, turnedHeight) = placed(OverlayLayer.UNDER_CENTRE_OF_CONTROL, dp(160))
            assertTrue("An ordinary popup turns over above a control with little room under it: $turnedTop", turnedTop + turnedHeight <= dp(160))
            val (stayTop, stayHeight) = placed(OverlayLayer.UNDER_CENTRE_OF_CONTROL_NO_FLIP, dp(160))
            assertEquals("Home's stays under it, 6 down", dp(160) + dp(24) + dp(6), stayTop)
            assertTrue("and scrolls in what the window has left, no further than its edge: $stayHeight", stayHeight in 1..(height - stayTop))
            val (roomyTop, roomyHeight) = placed(OverlayLayer.UNDER_CENTRE_OF_CONTROL_NO_FLIP, dp(40))
            assertEquals("With room under it, 6 down", dp(40) + dp(24) + dp(6), roomyTop)
            assertEquals("and as tall as it is", dp(132), roomyHeight)
            // Short of the window's edge but past the 8 and the overscan a popup keeps clear of, it still stays under.
            val (closeTop, closeHeight) = placed(OverlayLayer.UNDER_CENTRE_OF_CONTROL_NO_FLIP, dp(130))
            assertEquals(dp(130) + dp(24) + dp(6), closeTop)
            assertEquals("with all of it showing where the window has room: ${closeHeight}", dp(132), closeHeight)
        }
    }

    // The password's echo is the desktop's ●, one for each character, never one shown as the character it stands for, and the field is
    // still a password field to the system.
    @Test fun aPasswordIsEchoedAsTheDesktopsDotsAndNeverShowsACharacter() {
        onMain {
            val mask = Ui.PasswordMask()
            val typed = android.text.SpannableStringBuilder("p4ss")
            val shown = mask.getTransformation(typed, View(context))
            assertEquals("\u25CF\u25CF\u25CF\u25CF", shown.toString())
            assertEquals(4, shown.length)
            assertEquals('\u25CF', shown[3])
            // It follows the text, which is what a field does when a character is added: the one just typed is a dot at once.
            typed.append("!")
            assertEquals("\u25CF\u25CF\u25CF\u25CF\u25CF", shown.toString())
            assertEquals("\u25CF\u25CF", shown.subSequence(1, 3).toString())
            val chars = CharArray(3)
            (shown as android.text.GetChars).getChars(2, 5, chars, 0)
            assertEquals("\u25CF\u25CF\u25CF", String(chars))
            assertTrue("a password transformation to the system, so that nothing in the field is copied", mask is android.text.method.PasswordTransformationMethod)
        }
    }
}
