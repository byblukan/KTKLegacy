// KTK for Symbian - seroval requests and answers.
#include "seroval.h"
#include "json.h"

#include <QHash>

namespace
{
    /// References copy earlier nodes, so a few kilobytes could expand into gigabytes. Real weeks
    /// stay below 20 000 nodes.
    const int MaxNodes = 1 << 20;

    /// Escapes in the order seroval's deserializeString matches them.
    const char *const Escaped[] = {
        "\\\\", "\\\"", "\\n", "\\r", "\\b", "\\t", "\\f", "\\u2028", "\\u2029", "\\x3C"
    };
    const ushort Raw[] = { '\\', '"', '\n', '\r', '\b', '\t', '\f', 0x2028, 0x2029, '<' };
    const int EscapeCount = 10;

    class Decoder
    {
    public:
        Decoder() : nodes(0), failed(false) {}

        int nodes;
        bool failed;

        QVariant decode(const QVariant &node, int depth)
        {
            if (failed || !charge(1)) return QVariant();
            if (depth > 200) { failed = true; return QVariant(); }
            const int start = nodes;
            if (Json::isList(node)) return list(node.toList(), depth);
            if (!Json::isMap(node)) return node;

            const QVariantMap map = node.toMap();
            const QVariant idValue = map.value(QLatin1String("i"));
            const bool hasId = Json::isNumber(idValue);
            const qint64 nodeId = Json::number(idValue, 0);
            // Indexed values (t: 4) and repeated ids point at an already decoded node.
            if (hasId) {
                QHash<qint64, Known>::const_iterator known = byId.constFind(nodeId);
                if (known != byId.constEnd()) {
                    charge(known.value().count);
                    return known.value().value;
                }
            }
            const QVariant kind = map.value(QLatin1String("t"));
            if (!Json::isNumber(kind)) {
                // Plain JSON around the tree, such as an unwrapped envelope.
                QVariantMap out;
                for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd() && !failed; ++it)
                    out.insert(it.key(), decode(it.value(), depth + 1));
                return out;
            }
            QVariant value;
            switch (int(Json::number(kind, -1))) {
            case 1: { // string
                const QVariant s = map.value(QLatin1String("s"));
                value = Json::isString(s) ? Seroval::unescape(s.toString()) : QString::fromLatin1("");
                break;
            }
            case 0:   // number
            case 3:   // bigint
            case 5:   // date
                value = map.value(QLatin1String("s"));
                break;
            case 2: { // constant
                const qint64 constant = Json::number(map.value(QLatin1String("s")), -1);
                if (constant == 2) value = QVariant(true);
                else if (constant == 3) value = QVariant(false);
                break;
            }
            case 7:   // set
            case 9:   // array
                value = list(Json::list(map.value(QLatin1String("a"))), depth);
                break;
            case 10:  // object
            case 11:  // null-prototype object
                value = properties(map.value(QLatin1String("p")), false, depth);
                break;
            case 8:   // Map
                value = properties(map.value(QLatin1String("e")), true, depth);
                break;
            default:  // promises, streams, symbols, dangling references: no schedule data
                break;
            }
            if (hasId) {
                Known k;
                k.value = value;
                k.count = nodes - start + 1;
                byId.insert(nodeId, k);
            }
            return value;
        }

    private:
        struct Known
        {
            QVariant value;
            int count;
        };
        QHash<qint64, Known> byId;

        bool charge(int count)
        {
            nodes += count;
            if (nodes > MaxNodes || nodes < 0) failed = true;
            return !failed;
        }

        QVariant list(const QVariantList &items, int depth)
        {
            QVariantList out;
            for (int i = 0; i < items.size() && !failed; ++i) out.append(decode(items.at(i), depth + 1));
            return out;
        }

        QVariant properties(const QVariant &record, bool keyNodes, int depth)
        {
            QVariantMap out;
            if (!Json::isMap(record)) return out;
            const QVariantMap r = record.toMap();
            const QVariant keysRaw = r.value(QLatin1String("k"));
            const QVariant valuesRaw = r.value(QLatin1String("v"));
            if (!Json::isList(keysRaw) || !Json::isList(valuesRaw)) return out;
            const QVariantList keys = keysRaw.toList();
            const QVariantList values = valuesRaw.toList();
            const int count = qMin(keys.size(), values.size());
            for (int i = 0; i < count && !failed; ++i) {
                QString key;
                bool haveKey = false;
                if (keyNodes) {
                    const QVariant decoded = decode(keys.at(i), depth + 1);
                    if (Json::isString(decoded)) {
                        key = decoded.toString();
                        haveKey = true;
                    } else if (Json::isNumber(decoded)) {
                        key = QString::number(Json::number(decoded, 0));
                        haveKey = true;
                    }
                } else if (Json::isString(keys.at(i))) {
                    key = Seroval::unescape(keys.at(i).toString());
                    haveKey = true;
                }
                if (haveKey) out.insert(key, decode(values.at(i), depth + 1));
            }
            return out;
        }
    };
}

namespace Seroval
{
    QString escape(const QString &value)
    {
        QString out;
        out.reserve(value.size() + 8);
        for (int i = 0; i < value.size(); ++i) {
            const ushort c = value.at(i).unicode();
            int k = 0;
            for (; k < EscapeCount; ++k)
                if (Raw[k] == c) break;
            if (k < EscapeCount) out += QLatin1String(Escaped[k]);
            else out += value.at(i);
        }
        return out;
    }

    QString unescape(const QString &value)
    {
        if (value.indexOf(QLatin1Char('\\')) < 0) return value;
        QString out;
        out.reserve(value.size());
        int position = 0;
        const int length = value.size();
        while (position < length) {
            const int slash = value.indexOf(QLatin1Char('\\'), position);
            if (slash < 0) {
                out += value.mid(position);
                break;
            }
            out += value.mid(position, slash - position);
            bool matched = false;
            for (int k = 0; k < EscapeCount; ++k) {
                const QLatin1String escaped(Escaped[k]);
                if (value.midRef(slash, int(qstrlen(Escaped[k]))) == escaped) {
                    out += QChar(Raw[k]);
                    position = slash + int(qstrlen(Escaped[k]));
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                out += QLatin1Char('\\');
                position = slash + 1;
            }
        }
        return out;
    }

    QByteArray request(const Fields &fields)
    {
        QVariantList keys;
        QVariantList values;
        for (int i = 0; i < fields.size(); ++i) {
            keys.append(escape(fields.at(i).first));
            QVariantMap node;
            const QVariant &v = fields.at(i).second;
            if (Json::isNumber(v)) {
                node.insert(QLatin1String("t"), 0);
                node.insert(QLatin1String("s"), v);
            } else {
                node.insert(QLatin1String("t"), 1);
                node.insert(QLatin1String("s"), escape(v.toString()));
            }
            values.append(node);
        }
        QVariantMap dataProps;
        dataProps.insert(QLatin1String("k"), keys);
        dataProps.insert(QLatin1String("v"), values);
        QVariantMap data;
        data.insert(QLatin1String("t"), 10);
        data.insert(QLatin1String("i"), 1);
        data.insert(QLatin1String("p"), dataProps);
        data.insert(QLatin1String("o"), 0);

        QVariantMap rootProps;
        rootProps.insert(QLatin1String("k"), QVariantList() << QString::fromLatin1("data"));
        rootProps.insert(QLatin1String("v"), QVariantList() << QVariant(data));
        QVariantMap root;
        root.insert(QLatin1String("t"), 10);
        root.insert(QLatin1String("i"), 0);
        root.insert(QLatin1String("p"), rootProps);
        root.insert(QLatin1String("o"), 0);

        QVariantMap envelope;
        envelope.insert(QLatin1String("t"), root);
        envelope.insert(QLatin1String("f"), 127);
        envelope.insert(QLatin1String("m"), QVariantList());
        return Json::serialize(envelope);
    }

    QVariant decode(const QVariant &raw)
    {
        QVariant root = raw;
        if (Json::isMap(raw)) {
            const QVariantMap m = raw.toMap();
            if (Json::isMap(m.value(QLatin1String("t"))) && m.contains(QLatin1String("f")))
                root = m.value(QLatin1String("t"));
        }
        Decoder decoder;
        const QVariant value = decoder.decode(root, 0);
        if (decoder.failed)
            throw KtkError(KtkError::TooLarge, QString::fromUtf8("Ответ сайта слишком большой."));
        if (Json::isMap(value)) {
            const QVariantMap m = value.toMap();
            if (m.contains(QLatin1String("result"))) return m.value(QLatin1String("result"));
        }
        return value;
    }
}
