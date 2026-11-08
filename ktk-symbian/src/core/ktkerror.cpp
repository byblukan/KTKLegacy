// KTK for Symbian - every failure of the app, with the text shown to the user.
#include "ktkerror.h"

KtkError KtkError::fromStatus(int status)
{
    return KtkError(HttpStatus, QString::fromUtf8("Сайт ответил ошибкой %1.").arg(status), status);
}

QString KtkError::text() const
{
    switch (code) {
    case None:
        return QString();
    case Network:
        return detail.isEmpty()
            ? QString::fromUtf8("Нет связи с сайтом колледжа.")
            : QString::fromUtf8("Нет связи с сайтом колледжа.\n(%1)").arg(detail);
    case LoginRequired:
        return QString::fromUtf8("Сессия закончилась. Войдите снова.");
    case Credentials:
        return detail.isEmpty() ? QString::fromUtf8("Неверный логин или пароль.") : detail;
    case BadResponse:
        return QString::fromUtf8("Сайт ответил непонятно. Возможно, он обновился.");
    default:
        return detail.isEmpty() ? QString::fromUtf8("Ошибка сайта.") : detail;
    }
}
