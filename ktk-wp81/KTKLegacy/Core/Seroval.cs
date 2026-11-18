// KTK for Windows Phone 8.1 - requests and answers of TanStack server functions in the seroval
// format (the bot's model.rs; the same rules as ktk-android Seroval.java).
// Port of ktk-android Seroval.java.
using System;
using System.Collections.Generic;
using System.Text;

namespace KTKLegacy
{
    static class Seroval
    {
        /// <summary>References copy earlier nodes, so a few KB could expand into GB.
        /// Real weeks stay below 20 000 nodes.</summary>
        private const int MaxNodes = 1 << 20;

        // Escapes in the order seroval's deserializeString matches them.
        private static readonly string[] Escaped = {
            "\\\\", "\\\"", "\\n", "\\r", "\\b", "\\t", "\\f", "\\u2028", "\\u2029", "\\x3C"
        };
        private static readonly char[] Raw = {
            '\\', '"', '\n', '\r', '\b', '\t', '\f', '\u2028', '\u2029', '<'
        };

        public static string Escape(string value)
        {
            var sb = new StringBuilder(value.Length + 8);
            foreach (char c in value)
            {
                bool found = false;
                for (int k = 0; k < Raw.Length; k++)
                {
                    if (Raw[k] == c) { sb.Append(Escaped[k]); found = true; break; }
                }
                if (!found) sb.Append(c);
            }
            return sb.ToString();
        }

        public static string Unescape(string value)
        {
            if (value.IndexOf('\\') < 0) return value;
            var sb = new StringBuilder(value.Length);
            int pos = 0;
            int len = value.Length;
            while (pos < len)
            {
                int slash = value.IndexOf('\\', pos);
                if (slash < 0) { sb.Append(value, pos, len - pos); break; }
                sb.Append(value, pos, slash - pos);
                bool matched = false;
                for (int k = 0; k < Escaped.Length; k++)
                {
                    string esc = Escaped[k];
                    if (string.CompareOrdinal(value, slash, esc, 0, esc.Length) == 0)
                    {
                        sb.Append(Raw[k]);
                        pos = slash + esc.Length;
                        matched = true;
                        break;
                    }
                }
                if (!matched) { sb.Append('\\'); pos = slash + 1; }
            }
            return sb.ToString();
        }

        /// <summary>{data: {field: value, ...}} as a server function payload; values are string or long.</summary>
        public static string Request(IList<KeyValuePair<string, object>> fields)
        {
            // Build the seroval tree as a JSON string directly (mirrors the Android implementation).
            var keys = new StringBuilder();
            var vals = new StringBuilder();
            for (int i = 0; i < fields.Count; i++)
            {
                if (i > 0) { keys.Append(','); vals.Append(','); }
                var kv = fields[i];
                keys.Append('"').Append(Escape(kv.Key)).Append('"');
                if (kv.Value is long || kv.Value is int)
                {
                    long n = kv.Value is int ? (long)(int)kv.Value : (long)kv.Value;
                    vals.Append("{\"t\":0,\"s\":").Append(n).Append('}');
                }
                else
                {
                    string s = Escape(kv.Value != null ? kv.Value.ToString() : "");
                    vals.Append("{\"t\":1,\"s\":\"").Append(s).Append("\"}");
                }
            }
            // data node i=1, root node i=0
            string data = "{\"t\":10,\"i\":1,\"p\":{\"k\":[" + keys + "],\"v\":[" + vals + "]},\"o\":0}";
            string root = "{\"t\":10,\"i\":0,\"p\":{\"k\":[\"data\"],\"v\":[" + data + "]},\"o\":0}";
            return "{\"t\":" + root + ",\"f\":127,\"m\":[]}";
        }

        /// <summary>Decodes a parsed server function answer; unwraps {result: ...}.
        /// Throws KtkException(TooLarge) when references would expand beyond the node limit.</summary>
        public static object Decode(object raw)
        {
            object root = raw;
            var top = raw as Dictionary<string, object>;
            if (top != null && top.ContainsKey("t") && top.ContainsKey("f"))
                root = top["t"];

            var dec = new Decoder();
            object value = dec.Decode(root, 0);
            if (dec.Failed)
                throw new KtkException(KtkErrorCode.TooLarge, "Ответ сайта слишком большой.");

            var map = Json.Map(value);
            if (map != null && map.ContainsKey("result"))
                return map["result"];
            return value;
        }

        // ---------------------------------------------------------------- decoder

        private sealed class Decoder
        {
            private int _nodes;
            private readonly Dictionary<long, Known> _byId = new Dictionary<long, Known>();
            public bool Failed;

            private struct Known
            {
                public object Value;
                public int Count;
            }

            private bool Charge(int n)
            {
                _nodes += n;
                if (_nodes > MaxNodes) { Failed = true; return false; }
                return true;
            }

            public object Decode(object node, int depth)
            {
                if (Failed || !Charge(1)) return null;
                if (depth > 200) { Failed = true; return null; }
                int start = _nodes;

                var list = Json.List(node);
                if (list != null) return DecodeList(list, depth);

                var map = Json.Map(node);
                if (map == null) return node; // plain JSON scalar

                long? nodeId = null;
                object idVal;
                if (map.TryGetValue("i", out idVal))
                {
                    nodeId = Json.Key(idVal);
                    if (nodeId.HasValue)
                    {
                        Known known;
                        if (_byId.TryGetValue(nodeId.Value, out known))
                        {
                            Charge(known.Count);
                            return known.Value;
                        }
                    }
                }

                object kindVal;
                if (!map.TryGetValue("t", out kindVal) || !(kindVal is long || kindVal is double || kindVal is int))
                {
                    // Plain JSON object in the tree (envelope).
                    var out_ = new Dictionary<string, object>();
                    foreach (var kv in map)
                        out_[kv.Key] = Decode(kv.Value, depth + 1);
                    return out_;
                }

                long kind = Json.Number(kindVal, -1);
                object value = null;
                object sv;
                switch (kind)
                {
                    case 1: // string
                        if (map.TryGetValue("s", out sv) && sv is string)
                            value = Seroval.Unescape((string)sv);
                        else
                            value = "";
                        break;
                    case 0: // number
                    case 3: // bigint
                    case 5: // date
                        value = map.TryGetValue("s", out sv) ? sv : null;
                        break;
                    case 2: // constant
                        if (map.TryGetValue("s", out sv))
                        {
                            long c = Json.Number(sv, -1);
                            if (c == 2) value = true;
                            else if (c == 3) value = false;
                        }
                        break;
                    case 7:  // set
                    case 9:  // array
                        value = map.TryGetValue("a", out sv) ? DecodeList(Json.List(sv) ?? new List<object>(), depth) : new List<object>();
                        break;
                    case 10: // object
                    case 11: // null-prototype object
                        value = map.TryGetValue("p", out sv) ? DecodeProperties(sv, false, depth) : new Dictionary<string, object>();
                        break;
                    case 8:  // Map
                        value = map.TryGetValue("e", out sv) ? DecodeProperties(sv, true, depth) : new Dictionary<string, object>();
                        break;
                    default: // promises, streams, symbols, dangling refs
                        break;
                }

                if (nodeId.HasValue)
                    _byId[nodeId.Value] = new Known { Value = value, Count = _nodes - start + 1 };

                return value;
            }

            private List<object> DecodeList(List<object> arr, int depth)
            {
                var out_ = new List<object>(arr.Count);
                foreach (var item in arr)
                    out_.Add(Decode(item, depth + 1));
                return out_;
            }

            private Dictionary<string, object> DecodeProperties(object p, bool isMap, int depth)
            {
                var props = Json.Map(p);
                var out_ = new Dictionary<string, object>();
                if (props == null) return out_;
                var keys = Json.List(props.ContainsKey("k") ? props["k"] : null) ?? new List<object>();
                var vals = Json.List(props.ContainsKey("v") ? props["v"] : null) ?? new List<object>();
                for (int i = 0; i < keys.Count && i < vals.Count; i++)
                {
                    string key = isMap
                        ? Json.Str(Decode(keys[i], depth + 1))
                        : Json.Str(keys[i]);
                    if (key == null) continue;
                    out_[key] = Decode(vals[i], depth + 1);
                }
                return out_;
            }
        }
    }
}
