package ru.ktkschedule.android;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Plain trees (Map, List, String, Number, Boolean, null) to and from org.json, plus typed lookups
 * that never throw. Every value read from the site goes through these.
 */
final class Json {
    private Json() {
    }

    static Object toJson(Object value) throws JSONException {
        if (value == null) {
            return JSONObject.NULL;
        }
        if (value instanceof Map) {
            JSONObject out = new JSONObject();
            for (Map.Entry<?, ?> entry : ((Map<?, ?>) value).entrySet()) {
                out.put(String.valueOf(entry.getKey()), toJson(entry.getValue()));
            }
            return out;
        }
        if (value instanceof List) {
            JSONArray out = new JSONArray();
            for (Object item : (List<?>) value) {
                out.put(toJson(item));
            }
            return out;
        }
        if (value instanceof Double && (((Double) value).isNaN() || ((Double) value).isInfinite())) {
            return JSONObject.NULL;
        }
        return value;
    }

    static Object fromJson(Object value) {
        if (value == null || value == JSONObject.NULL) {
            return null;
        }
        if (value instanceof JSONObject) {
            JSONObject object = (JSONObject) value;
            Map<String, Object> out = new LinkedHashMap<String, Object>();
            Iterator<?> keys = object.keys();
            while (keys.hasNext()) {
                String key = (String) keys.next();
                out.put(key, fromJson(object.opt(key)));
            }
            return out;
        }
        if (value instanceof JSONArray) {
            JSONArray array = (JSONArray) value;
            List<Object> out = new ArrayList<Object>(array.length());
            for (int i = 0; i < array.length(); i++) {
                out.add(fromJson(array.opt(i)));
            }
            return out;
        }
        return value;
    }

    @SuppressWarnings("unchecked")
    static Map<String, Object> map(Object value) {
        return value instanceof Map ? (Map<String, Object>) value : null;
    }

    @SuppressWarnings("unchecked")
    static List<Object> list(Object value) {
        return value instanceof List ? (List<Object>) value : null;
    }

    static String string(Object value) {
        return value instanceof String ? (String) value : null;
    }

    /** Trimmed, non-empty string or null. */
    static String text(Object value) {
        if (!(value instanceof String)) {
            return null;
        }
        String text = ((String) value).trim();
        return text.isEmpty() ? null : text;
    }

    static long number(Object value, long fallback) {
        return value instanceof Number ? ((Number) value).longValue() : fallback;
    }

    static boolean bool(Object value) {
        return value instanceof Boolean && (Boolean) value;
    }

    /** Numbers as Long, so ids from different places compare equal as map keys. */
    static Long key(Object value) {
        return value instanceof Number ? ((Number) value).longValue() : null;
    }

    static Map<String, Object> mapAt(Map<String, Object> map, String key) {
        return map != null ? map(map.get(key)) : null;
    }

    static List<Object> listAt(Map<String, Object> map, String key) {
        List<Object> list = map != null ? list(map.get(key)) : null;
        return list != null ? list : new ArrayList<Object>();
    }
}
