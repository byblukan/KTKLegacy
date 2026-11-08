// KTK for Symbian - JSON to and from QVariant trees (Qt 4 has no JSON), plus typed lookups that
// never fail. Every value read from the site goes through these.
//
// Objects are QVariantMap, arrays QVariantList, strings QString, integers qlonglong, other
// numbers double, true/false bool, and null an invalid QVariant.
#ifndef JSON_H
#define JSON_H

#include <QByteArray>
#include <QString>
#include <QVariant>

namespace Json
{
    /// Parses UTF-8 JSON text. On malformed input `ok` is false and an invalid QVariant comes back.
    /// With `firstValueOnly`, text after the first complete value is ignored (like Java's
    /// JSONTokener.nextValue, which the Android client relies on).
    QVariant parse(const QByteArray &text, bool *ok, bool firstValueOnly = false);
    /// Compact JSON text, UTF-8. NaN and infinities become null.
    QByteArray serialize(const QVariant &value);

    bool isMap(const QVariant &v);
    bool isList(const QVariant &v);
    bool isString(const QVariant &v);
    bool isNumber(const QVariant &v);
    bool isBool(const QVariant &v);

    QVariantMap map(const QVariant &v);          ///< empty when not an object
    QVariantList list(const QVariant &v);        ///< empty when not an array
    QString string(const QVariant &v);           ///< null QString when not a string
    /// Trimmed, non-empty string; null QString otherwise.
    QString text(const QVariant &v);
    qint64 number(const QVariant &v, qint64 fallback);
    bool boolean(const QVariant &v);             ///< true only for JSON true

    /// The member as a map/list (empty when absent or of another type).
    QVariantMap mapAt(const QVariantMap &m, const char *key);
    QVariantList listAt(const QVariantMap &m, const char *key);
    bool hasMap(const QVariantMap &m, const char *key);
}

#endif // JSON_H
