package org.freetunnel.android;

import android.app.PendingIntent;
import android.content.Intent;
import android.content.ComponentName;
import android.content.Context;
import android.os.Build;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

public final class QuickTileService extends TileService {
    @Override public void onStartListening() {
        super.onStartListening();
        Tile tile = getQsTile();
        if (tile != null) {
            boolean active = isActive();
            tile.setState(active ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
            if (Build.VERSION.SDK_INT >= 29) tile.setSubtitle(active ? "Подключено" : "Отключено");
            tile.updateTile();
        }
    }

    @Override public void onClick() {
        super.onClick();
        if (isActive()) {
            FreeTunnelVpnService.stop(this);
            getQsTile().setState(Tile.STATE_INACTIVE);
            if (Build.VERSION.SDK_INT >= 29) getQsTile().setSubtitle("Отключено");
            getQsTile().updateTile();
            return;
        }
        Intent intent = new Intent(this, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra("tile_connect", true);
        if (Build.VERSION.SDK_INT >= 34) {
            PendingIntent open = PendingIntent.getActivity(this, 0, intent,
                    PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
            startActivityAndCollapse(open);
        } else {
            startActivityAndCollapse(intent);
        }
    }

    private boolean isActive() {
        int state = FreeTunnelVpnService.state(this);
        return state >= 1 && state <= 5;
    }

    public static void refresh(Context context) {
        if (Build.VERSION.SDK_INT >= 24) {
            TileService.requestListeningState(context,
                    new ComponentName(context, QuickTileService.class));
        }
    }
}
