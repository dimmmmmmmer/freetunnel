package org.freetunnel.android;
import android.content.Context;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONObject;
final class GeoPresets {
    private GeoPresets() {}
    static List<GeoDefinition> read(Context context) {
        List<GeoDefinition> result = new ArrayList<>();
        try (InputStream input = context.getAssets().open("geo/presets.json")) {
            java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
            byte[] buffer = new byte[4096]; int count;
            while ((count = input.read(buffer)) != -1) bytes.write(buffer, 0, count);
            JSONArray rows = new JSONArray(new String(bytes.toByteArray(), StandardCharsets.UTF_8));
            for (int i = 0; i < rows.length(); i++) {
                JSONObject row = rows.getJSONObject(i);
                String id = row.getString("id");
                int name;
                switch (id) {
                    case "ru-blocked-sites": name = R.string.geo_blocked_sites; break;
                    case "ru-blocked-ips": name = R.string.geo_blocked_ips; break;
                    case "ru-sites": name = R.string.geo_ru_sites; break;
                    case "ru-ips": name = R.string.geo_ru_ips; break;
                    default: continue;
                }
                result.add(new GeoDefinition(id, context.getString(name), row.getString("source"),
                        "ips".equals(row.getString("kind")) ? GeoLists.Kind.IPS : GeoLists.Kind.SITES,
                        "bypass".equals(row.getString("direction")), true));
            }
        } catch (Exception unreadable) { return new ArrayList<>(); }
        return result;
    }
}
