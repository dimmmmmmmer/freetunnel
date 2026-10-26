package org.freetunnel.android

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.net.VpnService
import androidx.core.content.edit

/**
 * Runs in the new version once the app has replaced itself. It deletes the
 * downloaded APK and, if the VPN was on when the user tapped Install, starts it
 * again: Android stopped it to replace the app. Android allows a foreground
 * service to start from this broadcast. Every later version must keep this
 * receiver, or updating from that version would leave the VPN off.
 */
class PackageReplacedReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_MY_PACKAGE_REPLACED) return
        AndroidUpdater.clearDownloads(context)
        val prefs = context.getSharedPreferences("freetunnel", Context.MODE_PRIVATE)
        val target = prefs.getLong(FreeTunnelVpnService.RESUME_AFTER_UPDATE, 0L)
        prefs.edit(commit = true) { remove(FreeTunnelVpnService.RESUME_AFTER_UPDATE) }
        // The marker names the version being installed, so neither a failed
        // update nor any other version acts on it. Without VPN consent the
        // service could not start anyway.
        if (target == BuildConfig.VERSION_CODE.toLong() && VpnService.prepare(context) == null) {
            runCatching { FreeTunnelVpnService.resume(context) }
        }
    }
}
