package org.freetunnel.android;

import org.json.JSONException;
import org.json.JSONObject;

/** A profile's explicit choice; no geo lists are enabled by migration. */
final class GeoDefinition {
    final String id;
    final String name;
    final String source;
    final GeoLists.Kind kind;
    final boolean bypass;
    final boolean enabled;

    GeoDefinition(String id, String name, String source, GeoLists.Kind kind, boolean bypass, boolean enabled) {
        this.id = id;
        this.name = name;
        this.source = source;
        this.kind = kind;
        this.bypass = bypass;
        this.enabled = enabled;
    }

    boolean valid() { return id != null && !id.isEmpty() && kind != null && GeoLists.parseSource(source) != null; }

    JSONObject toJson() throws JSONException {
        return new JSONObject().put("id", id).put("name", name).put("source", source)
                .put("kind", kind == GeoLists.Kind.IPS ? "ips" : "sites")
                .put("direction", bypass ? "bypass" : "vpn").put("enabled", enabled);
    }

    static GeoDefinition fromJson(JSONObject row) {
        if (row == null) return null;
        String kind = row.optString("kind");
        String direction = row.optString("direction");
        if ((!kind.equals("sites") && !kind.equals("ips"))
                || (!direction.equals("vpn") && !direction.equals("bypass"))) return null;
        GeoDefinition definition = new GeoDefinition(row.optString("id"), row.optString("name"),
                row.optString("source"), kind.equals("ips") ? GeoLists.Kind.IPS : GeoLists.Kind.SITES,
                direction.equals("bypass"), row.optBoolean("enabled", false));
        return definition.valid() ? definition : null;
    }
}
