package org.freetunnel.android

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
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
        field("release", null); field("cancelled", false)
        vpnState(0)
        prefs.edit().remove(FreeTunnelVpnService.RESUME_AFTER_UPDATE).commit()
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
    private fun cancelAndroidsDialog() {
        eventually { androidsDialogShown() }
        val root = instrumentation.uiAutomation.rootInActiveWindow!!
        val cancel = (root.findAccessibilityNodeInfosByText("Cancel") + root.findAccessibilityNodeInfosByText("CANCEL") +
                root.findAccessibilityNodeInfosByText("Отмена") + root.findAccessibilityNodeInfosByText("ОТМЕНА")).firstOrNull()
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
        // A launch check left on from an earlier version must not reach the network.
        prefs.edit().putBoolean("check_updates_on_launch", true).commit()
        try {
            field("officialBuild", null)
            assertFalse("A debug build is not signed with the release key", AndroidUpdater.official(context))
            openApp()
            AndroidUpdater.check(context)
            assertEquals(AndroidUpdater.State.IDLE, AndroidUpdater.state)
            field("officialBuild", null); field("certificatePin", debugCertificate())
            assertTrue(AndroidUpdater.official(context))
        } finally {
            prefs.edit().remove("check_updates_on_launch").commit()
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
        assertFalse(shows(context.getString(R.string.update_on_launch)))
        field("officialBuild", null); field("certificatePin", debugCertificate())
        val action = settingsUpdateAction()
        assertNotNull("The official build has an update action", action)
        assertEquals(context.getString(R.string.update_check), action!!.text.toString())
        assertTrue(shows(context.getString(R.string.update_on_launch)))
        assertFalse(shows(context.getString(R.string.update_unofficial)))
    }

    @Test fun cancelReturnsToAvailableWhateverEndsTheDownload() {
        // A cancel that lands while a read is blocked ends the download with
        // that read's timeout, not with Cancelled. Here the queued download is
        // cancelled before it runs and then fails on an untrusted address,
        // without any network access.
        openApp()
        field("officialBuild", true)
        field("release", UpdatePolicy.Release("99.0.0", "https://example.invalid/update.apk",
            "https://example.invalid/SHA256SUMS-android.txt", "https://example.invalid/SHA256SUMS-android.txt.sig"))
        val gate = CountDownLatch(1)
        (value("worker") as ExecutorService).execute { gate.await() }
        try {
            instrumentation.runOnMainSync { AndroidUpdater.download(activity!!) }
            assertEquals(AndroidUpdater.State.DOWNLOADING, AndroidUpdater.state)
            AndroidUpdater.cancel()
        } finally {
            gate.countDown()
        }
        drainWorker()
        assertEquals(AndroidUpdater.State.AVAILABLE, AndroidUpdater.state)
        assertNull(AndroidUpdater.failure)
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
