// KTK for Symbian - college dates. Days are counted from 1970-01-01 in a fixed UTC+5: the
// phone's time zone database is old (Asia/Yekaterinburg may still be UTC+6 there), so a named
// zone could be an hour off. Pure arithmetic, safe on any thread.
#ifndef COLLEGECALENDAR_H
#define COLLEGECALENDAR_H

#include <QString>

namespace CollegeCalendar
{
    const qint64 OffsetMinutes = 5 * 60;

    qint64 daysFromCivil(qint64 y, int m, int d);
    void civilFromDays(qint64 z, int *year, int *month, int *day);
    qint64 floorDiv(qint64 a, qint64 b);

    qint64 nowMillis();
    qint64 dayFromMillis(qint64 millis);
    qint64 today();
    /// 0 = Monday ... 6 = Sunday.
    int weekdayOfDay(qint64 day);
    QString stringFromDay(qint64 day);
    /// Accepts "yyyy-MM-dd" and anything that starts with it; false when unparsable.
    bool parse(const QString &string, qint64 *day);
    bool isWeekStart(const QString &string);

    /// Monday of the week to show now; on Sunday that is the next week.
    QString currentWeekStart();
    QString shiftWeek(const QString &weekStart, int weeks);
    /// "2–8 ноября".
    QString weekTitle(const QString &weekStart);
    /// "Понедельник, 2 ноября".
    QString dayTitle(const QString &date);
    bool isToday(const QString &date);
    /// -1 when unparsable.
    int weekdayOfDate(const QString &date);
    /// 485 -> "08:05".
    QString timeFromMinutes(int minutes);
    /// "2026-11-05T18:59:00.000Z" -> "05.11.2026 23:59" (college time); unknown formats unchanged.
    QString deadlineText(const QString &raw);
    /// "03.11 20:45".
    QString shortDateTime(qint64 millis);
}

#endif // COLLEGECALENDAR_H
