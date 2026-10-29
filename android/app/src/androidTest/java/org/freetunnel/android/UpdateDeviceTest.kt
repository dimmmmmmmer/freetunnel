package org.freetunnel.android

import android.accessibilityservice.AccessibilityService
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.content.res.Configuration
import android.os.SystemClock
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.TextView
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.CountDownLatch
import java.util.concurrent.ExecutorService
import java.util.concurrent.TimeUnit

class UpdateDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private val prefs get() = context.getSharedPreferences("freetunnel", Context.MODE_PRIVATE)
    private val installer get() = context.packageManager.packageInstaller
    private fun field(name: String, value: Any?) {
        AndroidUpdater::class.java.getDeclaredField(name).apply { isAccessible = true }.set(null, value)
    }
    private fun value(name: String): Any? = AndroidUpdater::class.java.getDeclaredField(name).apply { isAccessible = true }.get(null)
    private fun vpnState(state: Int) {
        FreeTunnelVpnService::class.java.getDeclaredField("currentState").apply { isAccessible = true }.setInt(null, state)
    }
    private var copy: File? = null
    private var activity: MainActivity? = null
    private fun eventually(condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + 30_000
        while (SystemClock.elapsedRealtime() < deadline) {
            if (condition()) return
            SystemClock.sleep(100)
        }
        assertTrue("Condition did not become true", condition())
    }
    @After fun cleanup() {
        installer.mySessions.forEach { runCatching { installer.abandonSession(it.sessionId) } }
        copy?.delete()
        field("verifiedFile", null); field("expectedDigest", null); field("state", AndroidUpdater.State.IDLE)
        field("sessionId", -1); field("committed", false); field("failure", null); field("verifiedVersionCode", 0L)
        field("officialBuild", null); field("certificatePin", BuildConfig.RELEASE_CERTIFICATE_SHA256)
        field("release", null); field("failedCheck", false); field("progress", 0)
        field("checking", false); field("silent", false)
        vpnState(0)
        prefs.edit().remove(FreeTunnelVpnService.RESUME_AFTER_UPDATE).remove(AndroidUpdater.LAST_LAUNCH_CHECK).commit()
        activity?.let { instrumentation.runOnMainSync { it.finish() } }
    }

    // A verified update ready to install: a copy of this already-installed test
    // APK. Crypto policy is covered separately with signed, tampered inputs.
    private fun readyToInstall() {
        copy = File.createTempFile("installer-test-", ".apk", context.cacheDir).also {
            File(context.applicationInfo.sourceDir).copyTo(it, overwrite = true)
        }
        val digest = MessageDigest.getInstance("SHA-256")
        copy!!.inputStream().use { input -> val bytes = ByteArray(65_536); while (true) {
            val count = input.read(bytes); if (count < 0) break; digest.update(bytes, 0, count)
        } }
        field("verifiedFile", copy); field("expectedDigest", digest.digest()); field("state", AndroidUpdater.State.READY)
    }
    private fun openApp() {
        activity = instrumentation.startActivitySync(Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
    }
    private fun androidsDialogShown() =
        instrumentation.uiAutomation.rootInActiveWindow?.packageName?.toString()?.contains("packageinstaller") == true
    private val isTv get() =
        (context.resources.configuration.uiMode and Configuration.UI_MODE_TYPE_MASK) == Configuration.UI_MODE_TYPE_TELEVISION
    private fun cancelAndroidsDialog() {
        eventually { androidsDialogShown() }
        val root = instrumentation.uiAutomation.rootInActiveWindow!!
        val cancel = (root.findAccessibilityNodeInfosByText("Cancel") + root.findAccessibilityNodeInfosByText("CANCEL") +
                root.findAccessibilityNodeInfosByText("Отмена") + root.findAccessibilityNodeInfosByText("ОТМЕНА")).firstOrNull()
        // Android TV's installer may word or lay out its dialog differently. Back
        // cancels it just the same: the installer refuses the session as it closes.
        if (cancel == null && isTv) {
            assertTrue(instrumentation.uiAutomation.performGlobalAction(AccessibilityService.GLOBAL_ACTION_BACK))
            return
        }
        assertNotNull("Android must offer cancellation", cancel)
        assertTrue(cancel!!.performAction(AccessibilityNodeInfo.ACTION_CLICK))
    }
    private fun emptySession(): Int = installer.createSession(PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL))
    // Waits until everything already queued on the updater's worker has run.
    private fun drainWorker() { (value("worker") as ExecutorService).submit {}.get(30, TimeUnit.SECONDS) }

    @Test fun installCallbackIsPrivateAndAndroidAsksBeforeReplacingApp() {
        assertFalse(context.packageManager.getActivityInfo(ComponentName(context, InstallResultActivity::class.java), 0).exported)
        assertTrue("Grant REQUEST_INSTALL_PACKAGES appop before device tests", context.packageManager.canRequestPackageInstalls())
        readyToInstall()
        // A real target version and a marker from an earlier attempt: with the VPN
        // off now, Install must remove the marker rather than write its own.
        field("verifiedVersionCode", 4242L)
        prefs.edit().putLong(FreeTunnelVpnService.RESUME_AFTER_UPDATE, 4242L).commit()
        openApp()
        instrumentation.runOnMainSync { AndroidUpdater.install(activity!!) }
        // Checked before Cancel, which removes the marker in any case.
        eventually { androidsDialogShown() }
        assertFalse("The VPN was off, so nothing reconnects", prefs.contains(FreeTunnelVpnService.RESUME_AFTER_UPDATE))
        cancelAndroidsDialog()
        // Cancel keeps the verified APK, so Install works again without a download.
        eventually { AndroidUpdater.state == AndroidUpdater.State.READY }
        assertTrue(copy!!.isFile)
        eventually { installer.mySessions.isEmpty() }
        assertFalse(prefs.contains(FreeTunnelVpnService.RESUME_AFTER_UPDATE))
        @Suppress("DEPRECATION")
        assertEquals(BuildConfig.VERSION_NAME, context.packageManager.getPackageInfo(context.packageName, 0).versionName)
    }

    @Test fun installRefusesBytesThatNoLongerMatch() {
        readyToInstall()
        // What the cache holds no longer hashes to what the download verified.
        field("expectedDigest", ByteArray(32))
        field("verifiedVersionCode", 4242L)
        vpnState(2)
        openApp()
        instrumentation.runOnMainSync { AndroidUpdater.install(activity!!) }
        var dialog = false
        eventually {
            dialog = dialog || androidsDialogShown()
            AndroidUpdater.state == AndroidUpdater.State.ERROR
        }
        assertEquals(UpdatePolicy.Failure.VERIFICATION, AndroidUpdater.failure)
        assertFalse("A copy that fails the check is deleted", copy!!.exists())
        eventually { installer.mySessions.isEmpty() }
        assertFalse("Nothing is installed, so nothing reconnects", prefs.contains(FreeTunnelVpnService.RESUME_AFTER_UPDATE))
        assertFalse("Nothing reaches Android's installer", dialog || androidsDialogShown())
    }

    @Test fun vpnOnAtInstallWritesAndCancelClearsResumeMarker() {
        readyToInstall()
        field("verifiedVersionCode", 4242L)
        vpnState(2)
        openApp()
        instrumentation.runOnMainSync { AndroidUpdater.install(activity!!) }
        eventually { prefs.getLong(FreeTunnelVpnService.RESUME_AFTER_UPDATE, 0L) == 4242L }
        cancelAndroidsDialog()
        eventually { AndroidUpdater.state == AndroidUpdater.State.READY }
        eventually { !prefs.contains(FreeTunnelVpnService.RESUME_AFTER_UPDATE) }
    }

    // apksigner's digest of this APK's signer, computed on the CI host: an
    // independent reading of the format that the release pin, and so every
    // official build's updates, depend on. Missing, it fails rather than skips.
    private fun debugCertificate(): String {
        val digest = InstrumentationRegistry.getArguments().getString("debugCertificate")
        assertTrue("Run with -Pandroid.testInstrumentationRunnerArguments.debugCertificate=<apksigner's SHA-256 digest>, as Android CI does",
            digest != null && Regex("[0-9a-f]{64}").matches(digest))
        return digest!!
    }

    @Test fun signerDigestMatchesApksigner() {
        val expected = debugCertificate()
        assertTrue(UpdatePolicy.signedBy(AndroidUpdater.signers(AndroidUpdater.packageInfo(context, null)), expected))
        copy = File.createTempFile("signer-test-", ".apk", context.cacheDir).also {
            File(context.applicationInfo.sourceDir).copyTo(it, overwrite = true)
        }
        assertTrue(UpdatePolicy.signedBy(AndroidUpdater.signers(AndroidUpdater.packageInfo(context, copy)), expected))
    }

    @Test fun debugBuildIsNotOfficialAndNeverChecks() {
        field("officialBuild", null)
        assertFalse("A debug build is not signed with the release key", AndroidUpdater.official(context))
        openApp()
        AndroidUpdater.check(context)
        assertEquals(AndroidUpdater.State.IDLE, AndroidUpdater.state)
        // Nor does its check on launch: it does not even count one.
        AndroidUpdater.checkOnLaunch(context)
        assertEquals(AndroidUpdater.State.IDLE, AndroidUpdater.state)
        assertEquals(false, value("checking"))
        assertFalse(prefs.contains(AndroidUpdater.LAST_LAUNCH_CHECK))
        field("officialBuild", null); field("certificatePin", debugCertificate())
        assertTrue(AndroidUpdater.official(context))
    }

    // The check on launch is the desktop's: silent (nothing is said while it runs, so the line stays as it was, and a Checking… would
    // be a change the desktop does not make), and held to once in 12 hours, whatever the attempt ends with. The worker is held up,
    // so that no request is made and what the app does on its own is what is looked at.
    @Test fun theCheckOnLaunchIsSilentAndAtMostOnceEvery12Hours() {
        field("officialBuild", null); field("certificatePin", debugCertificate())
        val gate = CountDownLatch(1)
        (value("worker") as ExecutorService).execute { gate.await() }
        try {
            val hour = 3_600_000L
            // Within 12 hours of the last: nothing is started, and the time stays what it was.
            val recent = System.currentTimeMillis() - 11 * hour
            prefs.edit().putLong(AndroidUpdater.LAST_LAUNCH_CHECK, recent).commit()
            AndroidUpdater.checkOnLaunch(context)
            assertEquals(false, value("checking"))
            assertEquals(recent, prefs.getLong(AndroidUpdater.LAST_LAUNCH_CHECK, 0L))
            // Past them: it starts, the time is now, and the line says nothing (IDLE, not CHECKING).
            val before = System.currentTimeMillis()
            prefs.edit().putLong(AndroidUpdater.LAST_LAUNCH_CHECK, before - 13 * hour).commit()
            AndroidUpdater.checkOnLaunch(context)
            assertEquals(true, value("checking"))
            assertEquals(true, value("silent"))
            assertEquals("a silent check shows no Checking…", AndroidUpdater.State.IDLE, AndroidUpdater.state)
            assertTrue(prefs.getLong(AndroidUpdater.LAST_LAUNCH_CHECK, 0L) >= before)
            // One at a time, and the time is not moved by a launch that finds one under way.
            val stamped = prefs.getLong(AndroidUpdater.LAST_LAUNCH_CHECK, 0L)
            prefs.edit().putLong(AndroidUpdater.LAST_LAUNCH_CHECK, 0L).commit()
            AndroidUpdater.checkOnLaunch(context)
            assertEquals(0L, prefs.getLong(AndroidUpdater.LAST_LAUNCH_CHECK, -1L))
            prefs.edit().putLong(AndroidUpdater.LAST_LAUNCH_CHECK, stamped).commit()
            // A click on the line while it runs makes it the one that is said, and it is not started twice.
            AndroidUpdater.check(context)
            assertEquals(AndroidUpdater.State.CHECKING, AndroidUpdater.state)
            assertEquals(false, value("silent"))
        } finally {
            gate.countDown()
        }
        drainWorker()
        assertEquals(false, value("checking"))
        assertNotEquals("the check answered, one way or the other", AndroidUpdater.State.CHECKING, AndroidUpdater.state)
    }

    // A manual check is never held to the 12 hours, and a check on launch waits for what is already known or under way.
    @Test fun aCheckFromTheLineIsNeverThrottledAndALaunchOneLeavesWhatIsKnown() {
        field("officialBuild", null); field("certificatePin", debugCertificate())
        val gate = CountDownLatch(1)
        (value("worker") as ExecutorService).execute { gate.await() }
        try {
            prefs.edit().putLong(AndroidUpdater.LAST_LAUNCH_CHECK, System.currentTimeMillis()).commit()
            AndroidUpdater.check(context)
            assertEquals(AndroidUpdater.State.CHECKING, AndroidUpdater.state)
            assertEquals(false, value("silent"))
        } finally {
            gate.countDown()
        }
        drainWorker()
        // Something found is not looked for again by a launch, nor is a download or a verified update disturbed.
        for (state in listOf(AndroidUpdater.State.AVAILABLE, AndroidUpdater.State.DOWNLOADING, AndroidUpdater.State.READY,
                AndroidUpdater.State.INSTALLING, AndroidUpdater.State.ERROR)) {
            field("state", state)
            prefs.edit().remove(AndroidUpdater.LAST_LAUNCH_CHECK).commit()
            AndroidUpdater.checkOnLaunch(context)
            assertEquals("$state: nothing starts", false, value("checking"))
            assertFalse("$state: and none is counted", prefs.contains(AndroidUpdater.LAST_LAUNCH_CHECK))
            assertEquals(state, AndroidUpdater.state)
        }
    }

    @Test fun launchCheckKeepsAReadyUpdate() {
        // Official, so that only the READY rule can stop the check.
        field("officialBuild", null); field("certificatePin", debugCertificate())
        readyToInstall()
        AndroidUpdater.check(context)
        assertEquals(AndroidUpdater.State.READY, AndroidUpdater.state)
        drainWorker()
        assertTrue("The verified APK is kept for Install", copy!!.isFile)
    }

    // Shows Settings and returns its update action, or null when it has none.
    private fun settingsUpdateAction(): TextView? {
        instrumentation.runOnMainSync {
            MainActivity::class.java.getDeclaredField("page").apply { isAccessible = true }.setInt(activity, 3)
            MainActivity::class.java.getDeclaredMethod("render").apply { isAccessible = true }.invoke(activity)
        }
        return MainActivity::class.java.getDeclaredField("updateAction").apply { isAccessible = true }.get(activity) as TextView?
    }
    private fun shows(text: String): Boolean {
        val found = ArrayList<View>()
        instrumentation.runOnMainSync { activity!!.window.decorView.findViewsWithText(found, text, View.FIND_VIEWS_WITH_TEXT) }
        return found.isNotEmpty()
    }

    @Test fun settingsOffersUpdatesOnlyInTheOfficialBuild() {
        openApp()
        field("officialBuild", null)
        assertNull("A debug build has no update action", settingsUpdateAction())
        assertTrue(shows(context.getString(R.string.update_unofficial)))
        field("officialBuild", null); field("certificatePin", debugCertificate())
        val action = settingsUpdateAction()
        assertNotNull("The official build has an update action", action)
        assertEquals(context.getString(R.string.update_check), action!!.text.toString())
        assertFalse(shows(context.getString(R.string.update_unofficial)))
        // One row under Maintenance, the desktop's section; no section of Updates or hint of Android's own any more.
        assertTrue(shows(context.getString(R.string.settings_maintenance)))
        assertFalse("the update line is one control, over its words", action.width > 0 && action.width >= activity!!.window.decorView.width * 3 / 4)
    }

    // The update line, clicked when the update is downloaded, installs it: with the VPN on after the app's own question (see the next
    // test), and with it off straight to Android's installer, whose window is cancelled here, leaving the update ready.
    @Test fun clickingTheUpdateLineWhenItIsDownloadedGoesToAndroidsInstaller() {
        assertTrue("Grant REQUEST_INSTALL_PACKAGES appop before device tests", context.packageManager.canRequestPackageInstalls())
        field("officialBuild", null); field("certificatePin", debugCertificate())
        readyToInstall()
        field("verifiedVersionCode", 4242L)
        openApp()
        val action = settingsUpdateAction()
        assertNotNull(action)
        instrumentation.runOnMainSync { MainActivity::class.java.getDeclaredMethod("refreshUpdateControls").apply { isAccessible = true }.invoke(activity) }
        // No release is known here, so its version is empty in the line.
        assertEquals("a downloaded update says so", context.getString(R.string.update_ready, ""), action!!.text.toString())
        instrumentation.runOnMainSync { assertTrue("it is clickable", action.isClickable); action.performClick() }
        eventually { androidsDialogShown() }
        cancelAndroidsDialog()
        eventually { AndroidUpdater.state == AndroidUpdater.State.READY }
        assertTrue(copy!!.isFile)
    }

    // The overlay of the app's own window with this tag, or null.
    private fun overlayView(tag: String): View? {
        var found: View? = null
        instrumentation.runOnMainSync { found = activity!!.window.decorView.findViewWithTag(tag) }
        return found
    }
    private fun click(tag: String) {
        instrumentation.runOnMainSync {
            val view = activity!!.window.decorView.findViewWithTag<View?>(tag)
            assertNotNull("No control has the tag $tag", view)
            assertTrue(view!!.performClick())
        }
    }

    // Installing replaces the app, and Android stops the VPN with it, so with the VPN on Install update asks first: in an overlay of
    // the app's own window, not a system dialog, with Cancel first in the focus and the red Install update after 400 ms. Cancel
    // installs nothing and leaves the update ready; Install update goes on to Android's own installer, which is cancelled here.
    @Test fun installUpdateWarnsInAnOverlayWhileTheVpnIsOn() {
        assertTrue("Grant REQUEST_INSTALL_PACKAGES appop before device tests", context.packageManager.canRequestPackageInstalls())
        readyToInstall()
        field("verifiedVersionCode", 4242L)
        vpnState(2)
        openApp()
        fun ask() {
            instrumentation.runOnMainSync {
                MainActivity::class.java.getDeclaredMethod("installUpdate").apply { isAccessible = true }.invoke(activity)
            }
            instrumentation.waitForIdleSync()
        }
        ask()
        assertNotNull("The warning is an overlay of the window", overlayView("overlay:confirm"))
        var message = ""
        var label = ""
        instrumentation.runOnMainSync {
            message = (activity!!.window.decorView.findViewWithTag<View>("confirm:message") as TextView).text.toString()
            label = (activity!!.window.decorView.findViewWithTag<View>("confirm:ok") as TextView).text.toString()
        }
        assertEquals(context.getString(R.string.update_disconnect_warning), message)
        assertEquals(context.getString(R.string.update_install), label)
        assertFalse("Nothing has gone to Android's installer yet", androidsDialogShown())
        // Cancel installs nothing.
        click("confirm:cancel")
        eventually { overlayView("overlay:confirm") == null }
        assertEquals(AndroidUpdater.State.READY, AndroidUpdater.state)
        assertTrue(installer.mySessions.isEmpty())
        assertFalse(prefs.contains(FreeTunnelVpnService.RESUME_AFTER_UPDATE))
        // Install update, once it is armed, goes on, and the VPN is to come back after the update.
        ask()
        SystemClock.sleep(500)
        click("confirm:ok")
        eventually { androidsDialogShown() }
        assertEquals(4242L, prefs.getLong(FreeTunnelVpnService.RESUME_AFTER_UPDATE, 0L))
        cancelAndroidsDialog()
        eventually { AndroidUpdater.state == AndroidUpdater.State.READY }
        eventually { !prefs.contains(FreeTunnelVpnService.RESUME_AFTER_UPDATE) }
    }

    // A download cannot be stopped from the app, as the desktop's cannot: it ends with its result, and a failure is an error with its
    // reason, to try again from. Here the queued download fails on an untrusted address, without any network access.
    @Test fun aFailedDownloadIsAnErrorWithItsReasonNotAReturnToAvailable() {
        openApp()
        field("officialBuild", true)
        field("release", UpdatePolicy.Release("99.0.0", "https://example.invalid/update.apk",
            "https://example.invalid/SHA256SUMS-android.txt", "https://example.invalid/SHA256SUMS-android.txt.sig"))
        val gate = CountDownLatch(1)
        (value("worker") as ExecutorService).execute { gate.await() }
        try {
            instrumentation.runOnMainSync { AndroidUpdater.download(activity!!) }
            assertEquals(AndroidUpdater.State.DOWNLOADING, AndroidUpdater.state)
        } finally {
            gate.countDown()
        }
        drainWorker()
        assertEquals(AndroidUpdater.State.ERROR, AndroidUpdater.state)
        assertEquals(UpdatePolicy.Failure.VERIFICATION, AndroidUpdater.failure)
        assertFalse("a download's failure is not a check's", AndroidUpdater.failedCheck)
    }

    @Test fun lostInstallerResultReturnsToReady() {
        val gone = emptySession()
        installer.abandonSession(gone)
        // The installer forgets an abandoned session on its own thread.
        eventually { installer.getSessionInfo(gone) == null }
        readyToInstall()
        field("state", AndroidUpdater.State.INSTALLING); field("committed", true); field("sessionId", gone)
        prefs.edit().putLong(FreeTunnelVpnService.RESUME_AFTER_UPDATE, 4242L).commit()
        instrumentation.runOnMainSync { AndroidUpdater.recoverInstall(context) }
        assertEquals(AndroidUpdater.State.READY, AndroidUpdater.state)
        assertTrue(copy!!.isFile)
        assertFalse(prefs.contains(FreeTunnelVpnService.RESUME_AFTER_UPDATE))
    }

    @Test fun pendingSessionSurvivesResumeAndInstallStartsOver() {
        val pending = emptySession()
        readyToInstall()
        field("state", AndroidUpdater.State.INSTALLING); field("committed", true); field("sessionId", pending)
        // It may be an approved install that is running, so resuming never abandons it.
        instrumentation.runOnMainSync { AndroidUpdater.recoverInstall(context) }
        assertEquals(AndroidUpdater.State.INSTALLING, AndroidUpdater.state)
        assertNotNull(installer.getSessionInfo(pending))
        openApp()
        instrumentation.runOnMainSync { AndroidUpdater.install(activity!!) }
        eventually { installer.getSessionInfo(pending) == null }
        cancelAndroidsDialog()
        eventually { AndroidUpdater.state == AndroidUpdater.State.READY }
        assertTrue(copy!!.isFile)
    }

    @Test fun resultForAnotherSessionIsIgnored() {
        AndroidUpdater.installationFinished(context, 12345, PackageInstaller.STATUS_FAILURE_ABORTED)
        assertEquals(AndroidUpdater.State.IDLE, AndroidUpdater.state)
        AndroidUpdater.installationFinished(context, 12345, PackageInstaller.STATUS_SUCCESS)
        assertEquals(AndroidUpdater.State.IDLE, AndroidUpdater.state)
        // A failure from an earlier process ends the planned reconnect.
        prefs.edit().putLong(FreeTunnelVpnService.RESUME_AFTER_UPDATE, 4242L).commit()
        AndroidUpdater.installationFinished(context, 12345, PackageInstaller.STATUS_FAILURE)
        assertEquals(AndroidUpdater.State.IDLE, AndroidUpdater.state)
        assertFalse(prefs.contains(FreeTunnelVpnService.RESUME_AFTER_UPDATE))
        // The result of a session install() abandoned for a newer one changes nothing.
        readyToInstall()
        field("state", AndroidUpdater.State.INSTALLING); field("committed", true); field("sessionId", 7)
        prefs.edit().putLong(FreeTunnelVpnService.RESUME_AFTER_UPDATE, 4242L).commit()
        AndroidUpdater.installationFinished(context, 6, PackageInstaller.STATUS_FAILURE_ABORTED)
        assertEquals(AndroidUpdater.State.INSTALLING, AndroidUpdater.state)
        assertEquals(7, value("sessionId"))
        assertEquals(4242L, prefs.getLong(FreeTunnelVpnService.RESUME_AFTER_UPDATE, 0L))
        assertTrue(copy!!.isFile)
    }
}
