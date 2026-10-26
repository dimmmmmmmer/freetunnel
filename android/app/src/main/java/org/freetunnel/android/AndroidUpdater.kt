package org.freetunnel.android

import android.annotation.SuppressLint
import android.app.Activity
import android.app.ActivityOptions
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageInstaller
import android.content.pm.PackageManager
import androidx.core.content.edit
import androidx.core.net.toUri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.Settings
import android.widget.Toast
import org.json.JSONArray
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.net.HttpURLConnection
import java.net.SocketTimeoutException
import java.net.URI
import java.security.MessageDigest
import java.util.concurrent.Executors

/** Process-local UI state; all network, hashing and session writes use one worker. */
object AndroidUpdater {
    enum class State { IDLE, CHECKING, CURRENT, AVAILABLE, DOWNLOADING, READY, INSTALLING, ERROR }
    /** Which install session a result delivered to InstallResultActivity belongs to. */
    const val EXTRA_SESSION = "org.freetunnel.android.extra.SESSION"
    // Overall backstops for a transfer that trickles; each read also has its own
    // 30 s timeout. Generous, because a release's limits stay with its users.
    private const val PAGE_DEADLINE = 120_000L
    private const val APK_DEADLINE = 3_600_000L
    @Volatile var state = State.IDLE; private set
    @Volatile var release: UpdatePolicy.Release? = null; private set
    @Volatile var progress = 0; private set
    /** Why the last attempt failed, for the ERROR text. */
    @Volatile var failure: UpdatePolicy.Failure? = null; private set
    /** The session went to Android: INSTALLING then ends with its result, or recoverInstall. */
    @Volatile var committed = false; private set
    private var observer: Runnable? = null
    private val main = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor()
    @Volatile private var verifiedFile: File? = null
    @Volatile private var expectedDigest: ByteArray? = null
    @Volatile private var verifiedVersionCode = 0L
    @Volatile private var sessionId = -1
    @Volatile private var cancelled = false
    @Volatile private var officialBuild: Boolean? = null
    // A var only so that device tests can pin the debug certificate.
    @Volatile private var certificatePin = BuildConfig.RELEASE_CERTIFICATE_SHA256

    private class Cancelled : Exception("Download cancelled")

    fun observe(callback: Runnable?) { observer = callback }
    private fun publish(value: State) { state = value; main.post { observer?.run() } }
    private fun failed(error: Throwable, otherwise: UpdatePolicy.Failure) {
        failure = UpdatePolicy.classify(error, otherwise)
        publish(State.ERROR)
    }
    private fun busy() = state in listOf(State.CHECKING, State.DOWNLOADING, State.INSTALLING)
    private fun clearResume(context: Context) {
        context.getSharedPreferences("freetunnel", Context.MODE_PRIVATE).edit(commit = true) {
            remove(FreeTunnelVpnService.RESUME_AFTER_UPDATE)
        }
    }

    /**
     * Whether this app is signed with the pinned release certificate, checked
     * once per process. Any other build (debug, CI, a fork, the 0.9.0 preview)
     * never checks or downloads: Android would refuse its update anyway. Fails
     * closed without a pin, on a lookup error, or with several signers.
     */
    fun official(context: Context): Boolean = officialBuild ?: runCatching {
        UpdatePolicy.signedBy(signers(packageInfo(context, null)), certificatePin)
    }.getOrDefault(false).also { officialBuild = it }

    /**
     * This app (archive == null) or a downloaded APK, with its signers. Before
     * API 28 only GET_SIGNATURES exists. From API 28 an archive's certificates
     * are believed to be collected only when GET_SIGNATURES is passed as well.
     */
    @SuppressLint("PackageManagerGetSignatures") // signers() accepts exactly one signer
    @Suppress("DEPRECATION")
    internal fun packageInfo(context: Context, archive: File?): PackageInfo {
        val manager = context.packageManager
        if (archive == null) {
            return if (Build.VERSION.SDK_INT >= 28) manager.getPackageInfo(context.packageName, PackageManager.GET_SIGNING_CERTIFICATES)
            else manager.getPackageInfo(context.packageName, PackageManager.GET_SIGNATURES)
        }
        val info = if (Build.VERSION.SDK_INT >= 28) {
            manager.getPackageArchiveInfo(archive.path, PackageManager.GET_SIGNING_CERTIFICATES or PackageManager.GET_SIGNATURES)
        } else {
            manager.getPackageArchiveInfo(archive.path, PackageManager.GET_SIGNATURES)
        }
        return info ?: error("Invalid APK")
    }

    /**
     * Every certificate the APK is signed with. A missing list, or a rotated key
     * on API 28+, gives none, so the pin check refuses it, as the release job does.
     */
    @Suppress("DEPRECATION")
    internal fun signers(info: PackageInfo): List<ByteArray> {
        if (Build.VERSION.SDK_INT >= 28) {
            val signing = info.signingInfo ?: return emptyList()
            if (signing.hasPastSigningCertificates()) return emptyList()
            return signing.apkContentsSigners?.map { it.toByteArray() } ?: emptyList()
        }
        return info.signatures?.map { it.toByteArray() } ?: emptyList()
    }

    @Suppress("DEPRECATION")
    private fun versionCode(info: PackageInfo): Long =
        if (Build.VERSION.SDK_INT >= 28) info.longVersionCode else info.versionCode.toLong()

    fun check(context: Context) {
        // A launch check in READY would delete the verified APK, so it waits.
        if (busy() || state == State.READY || !official(context)) return
        failure = null
        cancelled = false
        publish(State.CHECKING)
        worker.execute {
            runCatching {
                verifiedFile?.delete(); verifiedFile = null; expectedDigest = null
                val candidates = mutableListOf<UpdatePolicy.Candidate>()
                for (page in 1..10) {
                    val bytes = read("https://api.github.com/repos/${UpdatePolicy.REPOSITORY}/releases?per_page=100&page=$page", 2_097_152, false)
                    val array = JSONArray(bytes.toString(Charsets.UTF_8))
                    for (i in 0 until array.length()) {
                        val item = array.getJSONObject(i)
                        val assets = item.getJSONArray("assets")
                        val pairs = (0 until assets.length()).map { assets.getJSONObject(it) }
                            .map { it.getString("name") to it.getString("browser_download_url") }
                        // Duplicate asset names are ambiguous and cannot be selected.
                        if (pairs.map { it.first }.distinct().size != pairs.size) continue
                        candidates.add(UpdatePolicy.Candidate(item.getString("tag_name"), item.getBoolean("draft"),
                            item.getBoolean("prerelease"), pairs.toMap()))
                    }
                    if (array.length() < 100) break
                    check(page < 10) { "Release history is too large" }
                }
                release = UpdatePolicy.select(candidates, BuildConfig.VERSION_NAME)
                publish(if (release == null) State.CURRENT else State.AVAILABLE)
            }.onFailure { release = null; failed(it, UpdatePolicy.Failure.NETWORK) }
        }
    }

    fun download(activity: Activity) {
        val selected = release ?: return
        if (busy() || !official(activity)) return
        val app = activity.applicationContext
        progress = 0
        cancelled = false
        failure = null
        publish(State.DOWNLOADING)
        worker.execute {
            var output: File? = null
            runCatching {
                require(UpdatePolicy.trustedAsset(selected.manifest, selected.tag, "SHA256SUMS-android.txt"))
                require(UpdatePolicy.trustedAsset(selected.signature, selected.tag, "SHA256SUMS-android.txt.sig"))
                require(UpdatePolicy.trustedAsset(selected.apk, selected.tag, selected.filename))
                val manifest = read(selected.manifest, 65_536, true)
                val signature = read(selected.signature, 64, true)
                val expected = UpdatePolicy.verifyManifest(selected, manifest, signature, UpdatePolicy.rawPublicKey(BuildConfig.RELEASE_SIGNING_KEY))
                val directory = File(app.cacheDir, "updates").apply { check(isDirectory || mkdirs()) }
                directory.listFiles()?.forEach { it.delete() }
                output = File.createTempFile("verified-", ".apk", directory)
                val digest = MessageDigest.getInstance("SHA-256")
                val deadline = SystemClock.elapsedRealtime() + APK_DEADLINE
                output!!.outputStream().use { destination ->
                    transfer(selected.apk, 524_288_000, true) { input, size ->
                        copyApk(input, destination, size, digest, deadline)
                    }
                }
                UpdatePolicy.verifyDigest(expected, digest.digest())
                // One parse of the verified bytes gives the package, the version and
                // the signer, which must be the one this official build has.
                val info = packageInfo(app, output!!)
                @Suppress("DEPRECATION")
                val installed = app.packageManager.getPackageInfo(app.packageName, 0)
                require(info.packageName == app.packageName && info.versionName == selected.version)
                val code = versionCode(info)
                require(code > versionCode(installed)) { "APK is not newer than this app" }
                require(UpdatePolicy.signedBy(signers(info), certificatePin)) { "APK is signed with another certificate" }
                verifiedFile = output
                expectedDigest = expected
                verifiedVersionCode = code
                publish(State.READY)
            }.onFailure {
                output?.delete(); verifiedFile = null; expectedDigest = null
                if (it is Cancelled) publish(State.AVAILABLE) else failed(it, UpdatePolicy.Failure.VERIFICATION)
            }
        }
    }

    /** Stops a download at its next chunk; the release stays available. */
    fun cancel() { if (state == State.DOWNLOADING) cancelled = true }

    private fun stopIfOverdue(deadline: Long) {
        if (cancelled) throw Cancelled()
        if (SystemClock.elapsedRealtime() > deadline) throw SocketTimeoutException("Update transfer took too long")
    }

    private fun copyApk(input: InputStream, output: OutputStream, size: Long, digest: MessageDigest, deadline: Long) {
        val bytes = ByteArray(65_536)
        var total = 0L
        var last = -1
        while (true) {
            stopIfOverdue(deadline)
            val count = input.read(bytes)
            if (count < 0) break
            total += count
            require(total <= 524_288_000) { "APK too large" }
            digest.update(bytes, 0, count)
            output.write(bytes, 0, count)
            val next = if (size > 0) ((total * 100) / size).coerceAtMost(99).toInt() else 0
            if (next != last) { progress = next; last = next; main.post { observer?.run() } }
        }
        // A connection that ended early: the network's fault, not the release's.
        if (total <= 0 || (size >= 0 && total != size)) throw IOException("Incomplete APK")
    }

    fun install(activity: Activity) {
        // From INSTALLING only once the session went to Android and its dialog may be lost.
        if (state != State.READY && !(state == State.INSTALLING && committed)) return
        if (!activity.packageManager.canRequestPackageInstalls()) {
            // Some TV and OEM builds have no such screen; the toast says where to look.
            runCatching {
                activity.startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, "package:${activity.packageName}".toUri()))
            }.onFailure { Toast.makeText(activity, R.string.update_allow_installs, Toast.LENGTH_LONG).show() }
            return
        }
        val file = verifiedFile
        val expected = expectedDigest
        val app = activity.applicationContext
        val vpnOn = FreeTunnelVpnService.state(app) in 1..6
        val code = verifiedVersionCode
        sessionId = -1
        committed = false
        if (file == null || expected == null || !file.isFile) {
            // The cache was cleared since the download: fetch it again.
            verifiedFile = null; expectedDigest = null
            clearResume(app)
            publish(State.AVAILABLE)
            return
        }
        publish(State.INSTALLING)
        worker.execute {
            val installer = app.packageManager.packageInstaller
            runCatching {
                // Android stops this process, and the VPN with it, to replace the
                // app. PackageReplacedReceiver reconnects in the new version if the
                // VPN is on now, so the marker must be on disk before the session.
                app.getSharedPreferences("freetunnel", Context.MODE_PRIVATE).edit(commit = true) {
                    if (vpnOn && code > 0) putLong(FreeTunnelVpnService.RESUME_AFTER_UPDATE, code)
                    else remove(FreeTunnelVpnService.RESUME_AFTER_UPDATE)
                }
                // Only the updater creates sessions, so any left over are earlier
                // attempts, for example one whose dialog was lost.
                installer.mySessions.forEach { runCatching { installer.abandonSession(it.sessionId) } }
                val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL).apply {
                    setAppPackageName(app.packageName)
                    setSize(file.length())
                    if (Build.VERSION.SDK_INT >= 31) setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_REQUIRED)
                }
                val id = installer.createSession(params)
                sessionId = id
                installer.openSession(id).use { session ->
                    // Hash the bytes as they go into the session, so that Android gets
                    // exactly what was verified, not what the cache holds by now.
                    val digest = MessageDigest.getInstance("SHA-256")
                    session.openWrite("base.apk", 0, file.length()).use { out ->
                        file.inputStream().use { input ->
                            val bytes = ByteArray(65_536)
                            while (true) {
                                val count = input.read(bytes)
                                if (count < 0) break
                                digest.update(bytes, 0, count)
                                out.write(bytes, 0, count)
                            }
                        }
                        session.fsync(out)
                    }
                    if (!MessageDigest.isEqual(expected, digest.digest())) {
                        file.delete(); verifiedFile = null; expectedDigest = null
                        throw UpdatePolicy.Refused(UpdatePolicy.Failure.VERIFICATION, "APK checksum mismatch")
                    }
                    // API 35 requires a mutable callback. It targets a private explicit
                    // activity; only the system-held PendingIntent can deliver results.
                    val flags = PendingIntent.FLAG_UPDATE_CURRENT or (if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0)
                    val options = if (Build.VERSION.SDK_INT >= 35) ActivityOptions.makeBasic().apply {
                        setPendingIntentCreatorBackgroundActivityStartMode(ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOWED)
                    }.toBundle() else null
                    val callback = Intent(app, InstallResultActivity::class.java).putExtra(EXTRA_SESSION, id)
                    val result = PendingIntent.getActivity(app, id, callback, flags, options)
                    committed = true
                    session.commit(result.intentSender)
                }
            }.onFailure {
                val id = sessionId
                if (id >= 0) runCatching { installer.abandonSession(id) }
                sessionId = -1
                committed = false
                clearResume(app)
                failed(it, UpdatePolicy.Failure.INSTALLER)
            }
        }
    }

    /**
     * Called when the app comes to the front. If Android's result was lost (the
     * system dropped the session, or on API 34+ its dialog was blocked from
     * starting in the background), Install works again. A session that still
     * exists is left alone, since it may be an approved install that is running;
     * Settings then offers Install update, which starts over.
     */
    fun recoverInstall(context: Context) {
        if (state != State.INSTALLING || !committed) return
        if (context.packageManager.packageInstaller.getSessionInfo(sessionId) != null) return
        sessionId = -1
        committed = false
        clearResume(context)
        publish(if (verifiedFile?.isFile == true) State.READY else State.AVAILABLE)
    }

    fun installationFinished(context: Context, session: Int, status: Int) {
        val current = state == State.INSTALLING && session == sessionId
        // A failure ends the reconnect planned for after the update, unless it is
        // the result of a session that install() abandoned for a newer one.
        if (status != PackageInstaller.STATUS_SUCCESS && (current || state != State.INSTALLING)) clearResume(context)
        // A result for another session, possibly from an earlier process, changes
        // nothing else: an IDLE state never turns into a READY with no file.
        if (!current) return
        sessionId = -1
        committed = false
        when (status) {
            PackageInstaller.STATUS_SUCCESS -> { discardDownload(); publish(State.CURRENT) }
            // The user cancelled Android's dialog: the verified APK stays for another try.
            PackageInstaller.STATUS_FAILURE_ABORTED -> publish(State.READY)
            else -> { discardDownload(); failure = UpdatePolicy.Failure.INSTALLER; publish(State.ERROR) }
        }
    }

    /** Android's confirmation could not be shown, so the session can never finish. */
    fun confirmationFailed(context: Context, session: Int) {
        runCatching { context.packageManager.packageInstaller.abandonSession(session) }
        installationFinished(context, session, PackageInstaller.STATUS_FAILURE)
    }

    private fun discardDownload() { verifiedFile?.delete(); verifiedFile = null; expectedDigest = null }

    /** Deletes downloaded APKs, which a self-update would otherwise leave in the cache. */
    fun clearDownloads(context: Context) {
        File(context.cacheDir, "updates").listFiles()?.forEach { it.delete() }
    }

    private fun read(url: String, maximum: Int, asset: Boolean): ByteArray {
        val deadline = SystemClock.elapsedRealtime() + PAGE_DEADLINE
        return transfer(url, maximum.toLong(), asset) { input, _ ->
            val output = java.io.ByteArrayOutputStream()
            val buffer = ByteArray(8192)
            while (true) {
                stopIfOverdue(deadline)
                val count = input.read(buffer)
                if (count < 0) break
                require(output.size() + count <= maximum) { "Response too large" }
                output.write(buffer, 0, count)
            }
            output.toByteArray()
        }
    }

    private fun <T> transfer(url: String, maximum: Long, asset: Boolean, action: (InputStream, Long) -> T): T {
        var current = URI(url)
        repeat(6) { redirect ->
            require(current.scheme == "https" && current.rawUserInfo == null && current.rawFragment == null && current.port in listOf(-1, 443))
            val hosts = if (asset) setOf("github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com") else setOf("api.github.com")
            require(current.host in hosts) { "Untrusted download host" }
            val connection = current.toURL().openConnection() as HttpURLConnection
            connection.instanceFollowRedirects = false
            connection.connectTimeout = 20_000; connection.readTimeout = 30_000
            connection.setRequestProperty("User-Agent", "FreeTunnel-Android/${BuildConfig.VERSION_NAME}")
            if (!asset) connection.setRequestProperty("Accept", "application/vnd.github+json")
            try {
                val status = connection.responseCode
                if (status in listOf(301, 302, 303, 307, 308)) {
                    require(asset && redirect < 5) { "Unexpected redirect" }
                    current = current.resolve(connection.getHeaderField("Location") ?: error("Missing redirect"))
                } else {
                    if (UpdatePolicy.rateLimited(status, connection.getHeaderField("X-RateLimit-Remaining"), connection.getHeaderField("Retry-After"))) {
                        throw UpdatePolicy.Refused(UpdatePolicy.Failure.RATE_LIMITED, "HTTP $status")
                    }
                    if (status != 200) throw IOException("HTTP $status")
                    require(connection.contentLengthLong <= maximum) { "Response too large" }
                    return connection.inputStream.use { action(it, connection.contentLengthLong) }
                }
            } finally { connection.disconnect() }
        }
        error("Too many redirects")
    }
}
