package org.freetunnel.android;

import android.app.PendingIntent;
import android.content.Intent;
import android.content.ComponentName;
import android.content.Context;
import android.os.Build;
import android.net.VpnService;
import android.widget.Toast;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

public final class QuickTileService extends TileService {
    // The tile's words are in the app's language, which Android 8 to 12 only knows from this (AppLocale).
    @Override protected void attachBaseContext(Context base) { super.attachBaseContext(AppLocale.wrap(base)); }

    @Override public void onStartListening() {
        super.onStartListening();
        Tile tile = getQsTile();
        if (tile != null) {
            boolean active = isActive();
            tile.setState(active ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
            if (Build.VERSION.SDK_INT >= 29) {
                int state = FreeTunnelVpnService.state(this);
                tile.setSubtitle(getString(state == 2 ? R.string.text_connected :
                        state == 1 ? R.string.text_connecting : state == 6 ? R.string.vpn_traffic_blocked : active ? R.string.text_recovering_connection : R.string.text_disconnected));
            }
            tile.updateTile();
        }
    }

    @Override public void onClick() {
        super.onClick();
        if (isSecure() && isLocked()) { unlockAndRun(this::toggle); return; }
        toggle();
    }

    private void toggle() {
        if (isActive()) {
            FreeTunnelVpnService.stop(this);
            if (getQsTile() == null) return;
            getQsTile().setState(Tile.STATE_INACTIVE);
            if (Build.VERSION.SDK_INT >= 29) getQsTile().setSubtitle(getString(R.string.text_disconnected));
            getQsTile().updateTile();
            return;
        }
        if (VpnService.prepare(this) == null) {
            try {
                SavedProfile.Choice selected = SavedProfile.selected(getSharedPreferences("freetunnel", MODE_PRIVATE));
                FreeTunnelVpnService.start(this, selected.toml, selected.profile);
            } catch (Exception e) {
                Toast.makeText(this, getString(R.string.text_open_the_app_and_check_the_configuration), Toast.LENGTH_LONG).show();
            }
            return;
        }
        Intent intent = new Intent(this, VpnPermissionActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (Build.VERSION.SDK_INT >= 34) {
            PendingIntent open = PendingIntent.getActivity(this, 0, intent,
                    PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
            startActivityAndCollapse(open);
        } else {
            openConsentLegacy(intent);
        }
    }

    // This overload is the only one available on API 26–33. Caller checks SDK_INT.
    @android.annotation.SuppressLint("StartActivityAndCollapseDeprecated")
    private void openConsentLegacy(Intent intent) { startActivityAndCollapse(intent); }

    private boolean isActive() {
        int state = FreeTunnelVpnService.state(this);
        return state >= 1 && state <= 6;
    }

    // Called on every VPN state change. Android TV has no Quick Settings, and a TV
    // build that refuses the request must not take the app down with it.
    public static void refresh(Context context) {
        try { TileService.requestListeningState(context, new ComponentName(context, QuickTileService.class)); }
        catch (RuntimeException ignored) { }
    }
}
