// KTK for Windows Phone 8.1 - JSON tree (Dictionary, List, string, double, bool, null) to and from
// System.Json, plus typed lookups that never throw. Port of ktk-android Json.java.
using System;
using System.Collections.Generic;
using System.Text;

namespace KTKLegacy
{
    static class Json
    {
        // ---------------------------------------------------------------- parse / serialize

        /// <summary>Parses a JSON string into a plain tree (Dictionary, List, string, double, bool, null).
        /// Returns null on failure.</summary>
        public static object Parse(string text)
        {
            if (text == null) return null;
            int pos = 0;
            try
            {
                SkipWs(text, ref pos);
                object v = ParseValue(text, ref pos);
                return v;
            }
            catch { return null; }
        }

        /// <summary>Serializes a plain tree back to a JSON string.</summary>
        public static string Serialize(object value)
        {
            var sb = new StringBuilder();
            SerializeValue(value, sb);
            return sb.ToString();
        }

        // ---------------------------------------------------------------- typed accessors (never throw)

        public static Dictionary<string, object> Map(object value)
        {
            return value as Dictionary<string, object>;
        }

        public static List<object> List(object value)
        {
            return value as List<object>;
        }

        public static string Str(object value)
        {
            return value as string;
        }

        /// <summary>Trimmed non-empty string or null.</summary>
        public static string Text(object value)
        {
            var s = value as string;
            if (s == null) return null;
            s = s.Trim();
            return s.Length > 0 ? s : null;
        }

        public static long Number(object value, long fallback)
        {
            if (value is long) return (long)value;
            if (value is double) return (long)(double)value;
            if (value is int) return (int)value;
            if (value is string)
            {
                long lv;
                if (long.TryParse(((string)value).Trim(), out lv)) return lv;
            }
            return fallback;
        }

        public static bool Bool(object value)
        {
            return value is bool && (bool)value;
        }

        public static long? Key(object value)
        {
            if (value is long) return (long)value;
            if (value is double) return (long)(double)value;
            if (value is int) return (int)value;
            if (value is string)
            {
                long lv;
                if (long.TryParse(((string)value).Trim(), out lv)) return lv;
            }
            return null;
        }

        public static Dictionary<string, object> MapAt(Dictionary<string, object> map, string key)
        {
            object v;
            if (map != null && map.TryGetValue(key, out v))
                return Map(v);
            return null;
        }

        public static List<object> ListAt(Dictionary<string, object> map, string key)
        {
            if (map == null) return new List<object>();
            object v;
            if (!map.TryGetValue(key, out v)) return new List<object>();
            return List(v) ?? new List<object>();
        }

        // ---------------------------------------------------------------- recursive parser

        private static object ParseValue(string s, ref int pos)
        {
            SkipWs(s, ref pos);
            if (pos >= s.Length) throw new FormatException("eof");
            char c = s[pos];
            if (c == '{') return ParseObject(s, ref pos);
            if (c == '[') return ParseArray(s, ref pos);
            if (c == '"') return ParseString(s, ref pos);
            if (c == 't') { Expect(s, ref pos, "true");  return true; }
            if (c == 'f') { Expect(s, ref pos, "false"); return false; }
            if (c == 'n') { Expect(s, ref pos, "null");  return null; }
            return ParseNumber(s, ref pos);
        }

        private static Dictionary<string, object> ParseObject(string s, ref int pos)
        {
            pos++; // '{'
            var out_ = new Dictionary<string, object>();
            SkipWs(s, ref pos);
            if (pos < s.Length && s[pos] == '}') { pos++; return out_; }
            while (true)
            {
                SkipWs(s, ref pos);
                string key = ParseString(s, ref pos);
                SkipWs(s, ref pos);
                if (pos >= s.Length || s[pos] != ':') throw new FormatException(":");
                pos++;
                object val = ParseValue(s, ref pos);
                out_[key] = val;
                SkipWs(s, ref pos);
                if (pos >= s.Length) throw new FormatException("eof in obj");
                if (s[pos] == '}') { pos++; return out_; }
                if (s[pos] != ',') throw new FormatException(",");
                pos++;
            }
        }

        private static List<object> ParseArray(string s, ref int pos)
        {
            pos++; // '['
            var out_ = new List<object>();
            SkipWs(s, ref pos);
            if (pos < s.Length && s[pos] == ']') { pos++; return out_; }
            while (true)
            {
                out_.Add(ParseValue(s, ref pos));
                SkipWs(s, ref pos);
                if (pos >= s.Length) throw new FormatException("eof in arr");
                if (s[pos] == ']') { pos++; return out_; }
                if (s[pos] != ',') throw new FormatException(",");
                pos++;
            }
        }

        private static string ParseString(string s, ref int pos)
        {
            if (pos >= s.Length || s[pos] != '"') throw new FormatException("\"");
            pos++;
            var sb = new StringBuilder();
            while (pos < s.Length && s[pos] != '"')
            {
                char c = s[pos++];
                if (c == '\\')
                {
                    if (pos >= s.Length) throw new FormatException("eof in string");
                    char e = s[pos++];
                    switch (e)
                    {
                        case '"':  sb.Append('"');  break;
                        case '\\': sb.Append('\\'); break;
                        case '/':  sb.Append('/');  break;
                        case 'n':  sb.Append('\n'); break;
                        case 'r':  sb.Append('\r'); break;
                        case 't':  sb.Append('\t'); break;
                        case 'b':  sb.Append('\b'); break;
                        case 'f':  sb.Append('\f'); break;
                        case 'u':
                            if (pos + 4 > s.Length) throw new FormatException("eof in \\u");
                            sb.Append((char)Convert.ToInt32(s.Substring(pos, 4), 16));
                            pos += 4;
                            break;
                        default:   sb.Append(e);   break;
                    }
                }
                else
                {
                    sb.Append(c);
                }
            }
            if (pos >= s.Length) throw new FormatException("eof in string");
            pos++; // closing '"'
            return sb.ToString();
        }

        private static object ParseNumber(string s, ref int pos)
        {
            int start = pos;
            if (pos < s.Length && s[pos] == '-') pos++;
            while (pos < s.Length && char.IsDigit(s[pos])) pos++;
            bool isFloat = false;
            if (pos < s.Length && s[pos] == '.') { pos++; isFloat = true; while (pos < s.Length && char.IsDigit(s[pos])) pos++; }
            if (pos < s.Length && (s[pos] == 'e' || s[pos] == 'E'))
            {
                isFloat = true; pos++;
                if (pos < s.Length && (s[pos] == '+' || s[pos] == '-')) pos++;
                while (pos < s.Length && char.IsDigit(s[pos])) pos++;
            }
            string text = s.Substring(start, pos - start);
            if (!isFloat)
            {
                long lv;
                if (long.TryParse(text, out lv)) return lv;
            }
            double dv;
            if (double.TryParse(text, System.Globalization.NumberStyles.Float,
                System.Globalization.CultureInfo.InvariantCulture, out dv)) return dv;
            throw new FormatException("bad number: " + text);
        }

        private static void Expect(string s, ref int pos, string token)
        {
            if (s.Length - pos < token.Length || s.Substring(pos, token.Length) != token)
                throw new FormatException("expected " + token);
            pos += token.Length;
        }

        private static void SkipWs(string s, ref int pos)
        {
            while (pos < s.Length && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n'))
                pos++;
        }

        // ---------------------------------------------------------------- serializer

        private static void SerializeValue(object value, StringBuilder sb)
        {
            if (value == null)         { sb.Append("null"); return; }
            if (value is bool)         { sb.Append((bool)value ? "true" : "false"); return; }
            if (value is string)       { SerializeString((string)value, sb); return; }
            if (value is long)         { sb.Append((long)value); return; }
            if (value is int)          { sb.Append((int)value); return; }
            if (value is double)
            {
                double dv = (double)value;
                if (double.IsNaN(dv) || double.IsInfinity(dv)) { sb.Append("null"); return; }
                sb.Append(dv.ToString("R", System.Globalization.CultureInfo.InvariantCulture));
                return;
            }
            var dict = value as Dictionary<string, object>;
            if (dict != null)
            {
                sb.Append('{');
                bool first = true;
                foreach (var kv in dict)
                {
                    if (!first) sb.Append(',');
                    SerializeString(kv.Key, sb);
                    sb.Append(':');
                    SerializeValue(kv.Value, sb);
                    first = false;
                }
                sb.Append('}');
                return;
            }
            var list = value as List<object>;
            if (list != null)
            {
                sb.Append('[');
                for (int i = 0; i < list.Count; i++)
                {
                    if (i > 0) sb.Append(',');
                    SerializeValue(list[i], sb);
                }
                sb.Append(']');
                return;
            }
            sb.Append('"');
            sb.Append(value.ToString());
            sb.Append('"');
        }

        private static void SerializeString(string s, StringBuilder sb)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"':  sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n");  break;
                    case '\r': sb.Append("\\r");  break;
                    case '\t': sb.Append("\\t");  break;
                    case '\b': sb.Append("\\b");  break;
                    case '\f': sb.Append("\\f");  break;
                    default:
                        if (c < 0x20)
                            sb.AppendFormat("\\u{0:X4}", (int)c);
                        else
                            sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }
    }
}
