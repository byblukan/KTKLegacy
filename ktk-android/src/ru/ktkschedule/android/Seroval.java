package ru.ktkschedule.android;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Requests and answers of TanStack server functions in the seroval format (the bot's model.rs). */
final class Seroval {
    /** References copy earlier nodes, so a few kilobytes could expand into gigabytes. Real weeks stay below 20 000 nodes. */
    private static final int MAX_NODES = 1 << 20;

    /** Escapes in the order seroval's deserializeString matches them. */
    private static final String[] ESCAPED = {
        "\\\\", "\\\"", "\\n", "\\r", "\\b", "\\t", "\\f", "\\u2028", "\\u2029", "\\x3C",
    };
    private static final char[] RAW = {'\\', '"', '\n', '\r', '\b', '\t', '\f', (char) 0x2028, (char) 0x2029, '<'};

    private Seroval() {
    }

    static String escape(String value) {
        StringBuilder out = new StringBuilder(value.length() + 8);
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            String escaped = null;
            for (int k = 0; k < RAW.length; k++) {
                if (RAW[k] == c) {
                    escaped = ESCAPED[k];
                    break;
                }
            }
            if (escaped != null) {
                out.append(escaped);
            } else {
                out.append(c);
            }
        }
        return out.toString();
    }

    static String unescape(String value) {
        if (value.indexOf('\\') < 0) {
            return value;
        }
        StringBuilder out = new StringBuilder(value.length());
        int position = 0;
        int length = value.length();
        while (position < length) {
            int slash = value.indexOf('\\', position);
            if (slash < 0) {
                out.append(value, position, length);
                break;
            }
            out.append(value, position, slash);
            boolean matched = false;
            for (int k = 0; k < ESCAPED.length; k++) {
                if (value.startsWith(ESCAPED[k], slash)) {
                    out.append(RAW[k]);
                    position = slash + ESCAPED[k].length();
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                out.append('\\');
                position = slash + 1;
            }
        }
        return out.toString();
    }

    /** {data: {field: value, ...}} as a server function payload; values are String or Number. */
    static JSONObject request(Object[][] fields) {
        try {
            JSONArray keys = new JSONArray();
            JSONArray values = new JSONArray();
            for (Object[] field : fields) {
                keys.put(escape((String) field[0]));
                JSONObject node = new JSONObject();
                if (field[1] instanceof Number) {
                    node.put("t", 0).put("s", field[1]);
                } else {
                    node.put("t", 1).put("s", escape(String.valueOf(field[1])));
                }
                values.put(node);
            }
            JSONObject data = new JSONObject().put("t", 10).put("i", 1)
                .put("p", new JSONObject().put("k", keys).put("v", values)).put("o", 0);
            JSONObject root = new JSONObject().put("t", 10).put("i", 0)
                .put("p", new JSONObject().put("k", new JSONArray().put("data")).put("v", new JSONArray().put(data)))
                .put("o", 0);
            return new JSONObject().put("t", root).put("f", 127).put("m", new JSONArray());
        } catch (JSONException e) {
            throw new AssertionError(e);
        }
    }

    /** Decodes a server function answer; unwraps {result: ...}. */
    static Object decode(Object raw) throws KtkError {
        Object root = raw;
        if (raw instanceof JSONObject && ((JSONObject) raw).opt("t") instanceof JSONObject && ((JSONObject) raw).has("f")) {
            root = ((JSONObject) raw).opt("t");
        }
        Decoder decoder = new Decoder();
        Object value = decoder.decode(root);
        if (decoder.failed) {
            throw new KtkError(KtkError.Code.TOO_LARGE, "Ответ сайта слишком большой.");
        }
        Map<String, Object> map = Json.map(value);
        if (map != null && map.containsKey("result")) {
            return map.get("result");
        }
        return value;
    }

    private static final class Decoder {
        private final Map<Long, Object[]> byId = new HashMap<Long, Object[]>(); // id -> {value, expanded node count}
        private int nodes;
        boolean failed;

        private boolean charge(int count) {
            nodes += count;
            if (nodes > MAX_NODES || nodes < 0) {
                failed = true;
            }
            return !failed;
        }

        Object decode(Object node) {
            if (failed || !charge(1)) {
                return null;
            }
            int start = nodes;
            if (node instanceof JSONArray) {
                return list((JSONArray) node);
            }
            if (!(node instanceof JSONObject)) {
                return node == JSONObject.NULL ? null : node;
            }
            JSONObject map = (JSONObject) node;
            Long nodeId = Json.key(map.opt("i"));
            // Indexed values (t: 4) and repeated ids point at an already decoded node.
            if (nodeId != null) {
                Object[] known = byId.get(nodeId);
                if (known != null) {
                    charge((Integer) known[1]);
                    return known[0];
                }
            }
            Object kind = map.opt("t");
            if (!(kind instanceof Number)) {
                // Plain JSON around the tree, such as an unwrapped envelope.
                Map<String, Object> out = new LinkedHashMap<String, Object>();
                Iterator<?> keys = map.keys();
                while (keys.hasNext() && !failed) {
                    String key = (String) keys.next();
                    out.put(key, decode(map.opt(key)));
                }
                return out;
            }
            Object value;
            switch (((Number) kind).intValue()) {
                case 1: { // string
                    Object s = map.opt("s");
                    value = s instanceof String ? unescape((String) s) : "";
                    break;
                }
                case 0:   // number
                case 3:   // bigint
                case 5: { // date
                    Object s = map.opt("s");
                    value = s == JSONObject.NULL ? null : s;
                    break;
                }
                case 2: { // constant
                    long constant = Json.number(map.opt("s"), -1);
                    value = constant == 2 ? Boolean.TRUE : constant == 3 ? Boolean.FALSE : null;
                    break;
                }
                case 7:   // set
                case 9: { // array
                    Object items = map.opt("a");
                    value = list(items instanceof JSONArray ? (JSONArray) items : new JSONArray());
                    break;
                }
                case 10:  // object
                case 11:  // null-prototype object
                    value = properties(map.opt("p"), false);
                    break;
                case 8:   // Map
                    value = properties(map.opt("e"), true);
                    break;
                default:  // promises, streams, symbols, dangling references: no schedule data
                    value = null;
                    break;
            }
            if (nodeId != null) {
                byId.put(nodeId, new Object[]{value, nodes - start + 1});
            }
            return value;
        }

        private List<Object> list(JSONArray items) {
            List<Object> out = new ArrayList<Object>(items.length());
            for (int i = 0; i < items.length() && !failed; i++) {
                out.add(decode(items.opt(i)));
            }
            return out;
        }

        private Map<String, Object> properties(Object record, boolean keyNodes) {
            Map<String, Object> out = new LinkedHashMap<String, Object>();
            if (!(record instanceof JSONObject)) {
                return out;
            }
            Object keysRaw = ((JSONObject) record).opt("k");
            Object valuesRaw = ((JSONObject) record).opt("v");
            if (!(keysRaw instanceof JSONArray) || !(valuesRaw instanceof JSONArray)) {
                return out;
            }
            JSONArray keys = (JSONArray) keysRaw;
            JSONArray values = (JSONArray) valuesRaw;
            int count = Math.min(keys.length(), values.length());
            for (int i = 0; i < count && !failed; i++) {
                String key = null;
                if (keyNodes) {
                    Object decoded = decode(keys.opt(i));
                    if (decoded instanceof String) {
                        key = (String) decoded;
                    } else if (decoded instanceof Number) {
                        key = String.valueOf(((Number) decoded).longValue());
                    }
                } else if (keys.opt(i) instanceof String) {
                    key = unescape((String) keys.opt(i));
                }
                if (key != null) {
                    out.put(key, decode(values.opt(i)));
                }
            }
            return out;
        }
    }
}
