package org.freetunnel.android;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.VpnService;
import android.os.Bundle;
import android.widget.Toast;

/** Private tile consent entry. Exported launcher never accepts control extras. */
public final class VpnPermissionActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) return;
        Intent consent = VpnService.prepare(this);
        if (consent == null) { startVpn(); return; }
        // Some TV firmwares leave out Android's VPN consent dialog.
        try { startActivityForResult(consent, 1); }
        catch (ActivityNotFoundException e) {
            Toast.makeText(this, getString(R.string.text_vpn_consent_unavailable), Toast.LENGTH_LONG).show();
            finish();
        }
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == 1 && result == RESULT_OK) startVpn(); else finish();
    }
    private void startVpn() {
        try {
            FreeTunnelVpnService.start(this, SavedProfile.selected(getSharedPreferences("freetunnel", MODE_PRIVATE)));
        } catch (Exception e) {
            Toast.makeText(this, getString(R.string.text_open_the_app_and_check_the_saved_configuration), Toast.LENGTH_LONG).show();
        }
        finish();
    }
}
