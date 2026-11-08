// KTK for Symbian - JSON to and from QVariant trees.
#include "json.h"

#include <QStringList>

#include <math.h>
#include <stdlib.h>

namespace
{
    /// Nesting allowed in parsed text. The phone's thread stacks are small and real answers stay
    /// far below this.
    const int MaxDepth = 200;

    class Parser
    {
    public:
        Parser(const QByteArray &text) : failed(false), p(text.constData()), end(text.constData() + text.size()) {}

        QVariant document(bool firstValueOnly)
        {
            QVariant v = value(0);
            skipSpace();
            if (p != end && !firstValueOnly) failed = true;
            return failed ? QVariant() : v;
        }

        bool failed;

    private:
        const char *p;
        const char *end;

        void skipSpace()
        {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
        }

        bool literal(const char *word)
        {
            const char *q = p;
            for (; *word; ++word, ++q)
                if (q >= end || *q != *word) return false;
            p = q;
            return true;
        }

        QVariant value(int depth)
        {
            if (depth > MaxDepth) { failed = true; return QVariant(); }
            skipSpace();
            if (p >= end) { failed = true; return QVariant(); }
            switch (*p) {
            case '{': return object(depth);
            case '[': return array(depth);
            case '"': {
                QString s;
                if (!string(&s)) failed = true;
                return s;
            }
            case 't': if (literal("true")) return QVariant(true); break;
            case 'f': if (literal("false")) return QVariant(false); break;
            case 'n': if (literal("null")) return QVariant(); break;
            default:
                if (*p == '-' || (*p >= '0' && *p <= '9')) return number();
                break;
            }
            failed = true;
            return QVariant();
        }

        QVariant object(int depth)
        {
            QVariantMap out;
            ++p; // {
            skipSpace();
            if (p < end && *p == '}') { ++p; return out; }
            while (!failed) {
                skipSpace();
                QString key;
                if (p >= end || *p != '"' || !string(&key)) { failed = true; break; }
                skipSpace();
                if (p >= end || *p != ':') { failed = true; break; }
                ++p;
                QVariant v = value(depth + 1);
                if (failed) break;
                out.insert(key, v);
                skipSpace();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; break; }
                failed = true;
            }
            return out;
        }

        QVariant array(int depth)
        {
            QVariantList out;
            ++p; // [
            skipSpace();
            if (p < end && *p == ']') { ++p; return out; }
            while (!failed) {
                QVariant v = value(depth + 1);
                if (failed) break;
                out.append(v);
                skipSpace();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; break; }
                failed = true;
            }
            return out;
        }

        static int hex(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        bool hex4(ushort *out)
        {
            if (end - p < 4) return false;
            ushort v = 0;
            for (int i = 0; i < 4; ++i) {
                int h = hex(p[i]);
                if (h < 0) return false;
                v = ushort((v << 4) | h);
            }
            p += 4;
            *out = v;
            return true;
        }

        bool string(QString *out)
        {
            ++p; // opening quote
            QByteArray utf8;   // runs of raw bytes, decoded as UTF-8 at each escape and the end
            QString result;
            while (p < end) {
                char c = *p;
                if (c == '"') {
                    ++p;
                    result += QString::fromUtf8(utf8.constData(), utf8.size());
                    *out = result;
                    return true;
                }
                if (uchar(c) < 0x20) return false;
                if (c != '\\') { utf8.append(c); ++p; continue; }
                result += QString::fromUtf8(utf8.constData(), utf8.size());
                utf8.clear();
                ++p;
                if (p >= end) return false;
                char e = *p++;
                switch (e) {
                case '"': result += QLatin1Char('"'); break;
                case '\\': result += QLatin1Char('\\'); break;
                case '/': result += QLatin1Char('/'); break;
                case 'b': result += QLatin1Char('\b'); break;
                case 'f': result += QLatin1Char('\f'); break;
                case 'n': result += QLatin1Char('\n'); break;
                case 'r': result += QLatin1Char('\r'); break;
                case 't': result += QLatin1Char('\t'); break;
                case 'u': {
                    ushort u = 0;
                    if (!hex4(&u)) return false;
                    // A surrogate pair arrives as two escapes; QString holds UTF-16, so both
                    // halves simply go in as they are.
                    result += QChar(u);
                    break;
                }
                default:
                    return false;
                }
            }
            return false;
        }

        QVariant number()
        {
            const char *start = p;
            bool integral = true;
            if (p < end && *p == '-') ++p;
            while (p < end && *p >= '0' && *p <= '9') ++p;
            if (p < end && *p == '.') {
                integral = false;
                ++p;
                while (p < end && *p >= '0' && *p <= '9') ++p;
            }
            if (p < end && (*p == 'e' || *p == 'E')) {
                integral = false;
                ++p;
                if (p < end && (*p == '+' || *p == '-')) ++p;
                while (p < end && *p >= '0' && *p <= '9') ++p;
            }
            QByteArray text(start, int(p - start));
            if (text.isEmpty() || text == "-") { failed = true; return QVariant(); }
            if (integral && text.size() <= 18) {
                bool ok = false;
                qlonglong v = text.toLongLong(&ok);
                if (ok) return QVariant(v);
            }
            bool ok = false;
            double d = text.toDouble(&ok);
            if (!ok) { failed = true; return QVariant(); }
            return QVariant(d);
        }
    };

    void appendString(QByteArray &out, const QString &s)
    {
        out.append('"');
        const QByteArray utf8 = s.toUtf8();
        for (int i = 0; i < utf8.size(); ++i) {
            const uchar c = uchar(utf8.at(i));
            switch (c) {
            case '"': out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            default:
                if (c < 0x20) {
                    static const char digits[] = "0123456789abcdef";
                    out.append("\\u00");
                    out.append(digits[c >> 4]);
                    out.append(digits[c & 15]);
                } else {
                    out.append(char(c));
                }
            }
        }
        out.append('"');
    }

    void write(QByteArray &out, const QVariant &v, int depth)
    {
        if (depth > MaxDepth) { out.append("null"); return; }
        switch (v.type()) {
        case QVariant::Map: {
            const QVariantMap m = v.toMap();
            out.append('{');
            bool first = true;
            for (QVariantMap::const_iterator it = m.constBegin(); it != m.constEnd(); ++it) {
                if (!first) out.append(',');
                first = false;
                appendString(out, it.key());
                out.append(':');
                write(out, it.value(), depth + 1);
            }
            out.append('}');
            break;
        }
        case QVariant::List:
        case QVariant::StringList: {
            const QVariantList l = v.toList();
            out.append('[');
            for (int i = 0; i < l.size(); ++i) {
                if (i) out.append(',');
                write(out, l.at(i), depth + 1);
            }
            out.append(']');
            break;
        }
        case QVariant::String:
            appendString(out, v.toString());
            break;
        case QVariant::Bool:
            out.append(v.toBool() ? "true" : "false");
            break;
        case QVariant::Int:
        case QVariant::LongLong:
            out.append(QByteArray::number(v.toLongLong()));
            break;
        case QVariant::UInt:
        case QVariant::ULongLong:
            out.append(QByteArray::number(v.toULongLong()));
            break;
        case QVariant::Double: {
            const double d = v.toDouble();
            if (d != d || d - d != 0) out.append("null");   // NaN, infinities
            else out.append(QByteArray::number(d, 'g', 17));
            break;
        }
        default:
            out.append("null");
            break;
        }
    }
}

namespace Json
{
    QVariant parse(const QByteArray &text, bool *ok, bool firstValueOnly)
    {
        Parser parser(text);
        QVariant v = parser.document(firstValueOnly);
        if (ok) *ok = !parser.failed;
        return v;
    }

    QByteArray serialize(const QVariant &value)
    {
        QByteArray out;
        write(out, value, 0);
        return out;
    }

    bool isMap(const QVariant &v) { return v.type() == QVariant::Map; }
    bool isList(const QVariant &v) { return v.type() == QVariant::List; }
    bool isString(const QVariant &v) { return v.type() == QVariant::String; }
    bool isBool(const QVariant &v) { return v.type() == QVariant::Bool; }

    bool isNumber(const QVariant &v)
    {
        switch (v.type()) {
        case QVariant::Int:
        case QVariant::UInt:
        case QVariant::LongLong:
        case QVariant::ULongLong:
        case QVariant::Double:
            return true;
        default:
            return false;
        }
    }

    QVariantMap map(const QVariant &v) { return isMap(v) ? v.toMap() : QVariantMap(); }
    QVariantList list(const QVariant &v) { return isList(v) ? v.toList() : QVariantList(); }
    QString string(const QVariant &v) { return isString(v) ? v.toString() : QString(); }

    QString text(const QVariant &v)
    {
        if (!isString(v)) return QString();
        const QString t = v.toString().trimmed();
        return t.isEmpty() ? QString() : t;
    }

    qint64 number(const QVariant &v, qint64 fallback)
    {
        if (!isNumber(v)) return fallback;
        if (v.type() == QVariant::Double) {
            const double d = v.toDouble();
            if (d != d) return fallback;
            return qint64(d);   // like Java's longValue(): truncation
        }
        return v.toLongLong();
    }

    bool boolean(const QVariant &v) { return isBool(v) && v.toBool(); }

    QVariantMap mapAt(const QVariantMap &m, const char *key) { return map(m.value(QLatin1String(key))); }
    QVariantList listAt(const QVariantMap &m, const char *key) { return list(m.value(QLatin1String(key))); }
    bool hasMap(const QVariantMap &m, const char *key) { return isMap(m.value(QLatin1String(key))); }
}
