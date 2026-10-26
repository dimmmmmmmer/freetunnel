package org.freetunnel.android

import android.app.Activity
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Bundle

/** System installation consent is delivered only through our explicit PendingIntent. */
class InstallResultActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val session = intent.getIntExtra(AndroidUpdater.EXTRA_SESSION, -1)
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            if (savedInstanceState == null) {
                @Suppress("DEPRECATION")
                val confirmation = intent.getParcelableExtra<Intent>(Intent.EXTRA_INTENT)
                if (confirmation == null || runCatching { startActivity(confirmation) }.isFailure) {
                    AndroidUpdater.confirmationFailed(this, session)
                }
            }
        } else {
            AndroidUpdater.installationFinished(this, session, status)
        }
        finish()
    }

}
