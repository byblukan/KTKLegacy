// KTK for Symbian - requests and answers of TanStack server functions in the seroval format
// (the bot's model.rs; the same rules as ktk-ios KTKSeroval and ktk-android Seroval).
#ifndef SEROVAL_H
#define SEROVAL_H

#include "ktkerror.h"

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QVariant>

namespace Seroval
{
    /// One field of a request: a string or an integer.
    typedef QPair<QString, QVariant> Field;
    typedef QList<Field> Fields;

    QString escape(const QString &value);
    QString unescape(const QString &value);

    /// {data: {field: value, ...}} as a server function payload, JSON text.
    QByteArray request(const Fields &fields);

    /// Decodes a parsed server function answer and unwraps {result: ...}. Throws TooLarge when
    /// references would expand it beyond the node limit.
    QVariant decode(const QVariant &raw);
}

#endif // SEROVAL_H
