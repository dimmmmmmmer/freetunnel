package org.freetunnel.android;

import org.json.JSONException;
import org.json.JSONObject;

/** The Android core sends JSON; match qt_trusttunnel_connection_info_line on desktop. */
final class ConnectionLog {
    private ConnectionLog() { }

    static String format(String message) {
        if (!message.trim().startsWith("{")) return message;
        try {
            JSONObject event = new JSONObject(message);
            // Leave unrelated or incomplete JSON messages readable for diagnostics.
            Object rawAction = event.opt("action");
            if (!(rawAction instanceof String)) return message;
            String action = (String) rawAction;
            if (!action.equals("bypass") && !action.equals("tunnel") && !action.equals("reject"))
                action = "unknown";
            String target = string(event, "domain");
            if (target.isEmpty()) target = string(event, "dst");
            if (target.isEmpty()) target = "unknown destination";
            return action + " " + target;
        } catch (JSONException malformed) {
            return message;
        }
    }

    private static String string(JSONObject event, String key) {
        Object value = event.opt(key);
        return value instanceof String ? (String) value : "";
    }
}
