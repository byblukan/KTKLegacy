// KTK for Symbian - college dates in a fixed UTC+5.
#include "collegecalendar.h"

#include <QDateTime>
#include <QRegExp>

namespace
{
    const char *const Months[] = {
        "января", "февраля", "марта", "апреля", "мая", "июня",
        "июля", "августа", "сентября", "октября", "ноября", "декабря"
    };
    const char *const Weekdays[] = {
        "Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота", "Воскресенье"
    };

    QString month(int m) { return QString::fromUtf8(Months[(m - 1) % 12]); }

    QString two(qint64 v) { return QString::number(v).rightJustified(2, QLatin1Char('0')); }

    /// Digits only, like Integer.parseInt on a fixed-width field; false otherwise.
    bool digits(const QString &s, int *out)
    {
        if (s.isEmpty()) return false;
        int v = 0;
        for (int i = 0; i < s.size(); ++i) {
            const QChar c = s.at(i);
            if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
            v = v * 10 + (c.unicode() - '0');
        }
        *out = v;
        return true;
    }
}

namespace CollegeCalendar
{
    // Howard Hinnant's civil date algorithms (proleptic Gregorian).
    qint64 daysFromCivil(qint64 y, int m, int d)
    {
        y -= m <= 2 ? 1 : 0;
        const qint64 era = (y >= 0 ? y : y - 399) / 400;
        const qint64 yoe = y - era * 400;
        const qint64 doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
        const qint64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    void civilFromDays(qint64 z, int *year, int *month, int *day)
    {
        z += 719468;
        const qint64 era = (z >= 0 ? z : z - 146096) / 146097;
        const qint64 doe = z - era * 146097;
        const qint64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const qint64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const qint64 mp = (5 * doy + 2) / 153;
        const qint64 d = doy - (153 * mp + 2) / 5 + 1;
        const qint64 m = mp < 10 ? mp + 3 : mp - 9;
        *year = int(yoe + era * 400 + (m <= 2 ? 1 : 0));
        *month = int(m);
        *day = int(d);
    }

    qint64 floorDiv(qint64 a, qint64 b)
    {
        const qint64 q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    qint64 nowMillis() { return QDateTime::currentDateTime().toMSecsSinceEpoch(); }

    qint64 dayFromMillis(qint64 millis) { return floorDiv(floorDiv(millis, 1000) + OffsetMinutes * 60, 86400); }

    qint64 today() { return dayFromMillis(nowMillis()); }

    int weekdayOfDay(qint64 day)
    {
        // 1970-01-01 was a Thursday (3 when Monday is 0).
        return int(((day % 7) + 7 + 3) % 7);
    }

    QString stringFromDay(qint64 day)
    {
        int y, m, d;
        civilFromDays(day, &y, &m, &d);
        return QString::number(y).rightJustified(4, QLatin1Char('0')) + QLatin1Char('-') + two(m) + QLatin1Char('-') + two(d);
    }

    bool parse(const QString &string, qint64 *day)
    {
        if (string.size() < 10) return false;
        const QString s = string.left(10);
        if (s.at(4) != QLatin1Char('-') || s.at(7) != QLatin1Char('-')) return false;
        int y, m, d;
        if (!digits(s.mid(0, 4), &y) || !digits(s.mid(5, 2), &m) || !digits(s.mid(8, 2), &d)) return false;
        if (m < 1 || m > 12 || d < 1 || d > 31) return false;
        *day = daysFromCivil(y, m, d);
        return true;
    }

    bool isWeekStart(const QString &string)
    {
        return QRegExp(QLatin1String("\\d{4}-\\d{2}-\\d{2}")).exactMatch(string);
    }

    QString currentWeekStart()
    {
        qint64 t = today();
        if (weekdayOfDay(t) == 6) t += 1;
        return stringFromDay(t - weekdayOfDay(t));
    }

    QString shiftWeek(const QString &weekStart, int weeks)
    {
        qint64 day;
        if (!parse(weekStart, &day)) return currentWeekStart();
        const qint64 monday = day - weekdayOfDay(day);
        return stringFromDay(monday + qint64(weeks) * 7);
    }

    QString weekTitle(const QString &weekStart)
    {
        qint64 start;
        if (!parse(weekStart, &start)) return weekStart;
        int ay, am, ad, by, bm, bd;
        civilFromDays(start, &ay, &am, &ad);
        civilFromDays(start + 6, &by, &bm, &bd);
        if (am == bm) return QString::fromUtf8("%1–%2 %3").arg(ad).arg(bd).arg(month(am));
        return QString::fromUtf8("%1 %2 – %3 %4").arg(ad).arg(month(am)).arg(bd).arg(month(bm));
    }

    QString dayTitle(const QString &date)
    {
        qint64 day;
        if (!parse(date, &day)) return date;
        int y, m, d;
        civilFromDays(day, &y, &m, &d);
        return QString::fromUtf8(Weekdays[weekdayOfDay(day)]) + QString::fromUtf8(", %1 ").arg(d) + month(m);
    }

    bool isToday(const QString &date)
    {
        qint64 day;
        return parse(date, &day) && day == today();
    }

    int weekdayOfDate(const QString &date)
    {
        qint64 day;
        return parse(date, &day) ? weekdayOfDay(day) : -1;
    }

    QString timeFromMinutes(int minutes)
    {
        const int m = ((minutes % 1440) + 1440) % 1440;
        return two(m / 60) + QLatin1Char(':') + two(m % 60);
    }

    QString deadlineText(const QString &raw)
    {
        const QString text = raw.trimmed();
        qint64 day;
        if (!parse(text, &day)) return raw;
        int y, m, d;
        if (text.size() >= 16 && text.at(10) == QLatin1Char('T') && text.at(13) == QLatin1Char(':')) {
            int hour, minute;
            if (digits(text.mid(11, 2), &hour) && digits(text.mid(14, 2), &minute)) {
                qint64 total = day * 1440 + hour * 60 + minute;
                const QString tail = text.mid(16);
                const int plus = tail.indexOf(QLatin1Char('+'));
                const int minus = tail.indexOf(QLatin1Char('-'));
                const int sign = plus < 0 ? minus : minus < 0 ? plus : qMin(plus, minus);
                bool ok = true;
                if (tail.endsWith(QLatin1Char('Z'))) {
                    total += OffsetMinutes;
                } else if (sign >= 0) {
                    QString zone = tail.mid(sign + 1);
                    zone.remove(QLatin1Char(':'));
                    int oh = 0, om = 0;
                    if (zone.size() >= 2) ok = digits(zone.mid(0, 2), &oh);
                    if (ok && zone.size() >= 4) ok = digits(zone.mid(2, 2), &om);
                    const qint64 offset = (oh * 60 + om) * (tail.at(sign) == QLatin1Char('-') ? -1 : 1);
                    total += OffsetMinutes - offset;
                }
                if (ok) {
                    const qint64 localDay = floorDiv(total, 1440);
                    const qint64 minutes = total - localDay * 1440;
                    civilFromDays(localDay, &y, &m, &d);
                    return two(d) + QLatin1Char('.') + two(m) + QLatin1Char('.') + QString::number(y)
                        + QLatin1Char(' ') + two(minutes / 60) + QLatin1Char(':') + two(minutes % 60);
                }
            }
        }
        civilFromDays(day, &y, &m, &d);
        return two(d) + QLatin1Char('.') + two(m) + QLatin1Char('.') + QString::number(y);
    }

    QString shortDateTime(qint64 millis)
    {
        const qint64 seconds = floorDiv(millis, 1000) + OffsetMinutes * 60;
        const qint64 day = floorDiv(seconds, 86400);
        const qint64 rest = seconds - day * 86400;
        int y, m, d;
        civilFromDays(day, &y, &m, &d);
        return two(d) + QLatin1Char('.') + two(m) + QLatin1Char(' ') + two(rest / 3600) + QLatin1Char(':') + two((rest % 3600) / 60);
    }
}
