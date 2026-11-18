// KTK for Windows Phone 8.1 - college dates. Days are counted from 1970-01-01 in a fixed UTC+5:
// old tzdata may still have Asia/Yekaterinburg as UTC+6, so pure arithmetic is used.
// Port of ktk-android CollegeCalendar.java.
using System;

namespace KTKLegacy
{
    static class CollegeCalendar
    {
        public const long OffsetMinutes = 5 * 60;

        private static readonly string[] Months = {
            "января","февраля","марта","апреля","мая","июня",
            "июля","августа","сентября","октября","ноября","декабря"
        };
        private static readonly string[] Weekdays = {
            "Понедельник","Вторник","Среда","Четверг","Пятница","Суббота","Воскресенье"
        };

        // Howard Hinnant's civil date algorithms (proleptic Gregorian).
        public static long DaysFromCivil(long y, int m, int d)
        {
            y -= m <= 2 ? 1 : 0;
            long era = (y >= 0 ? y : y - 399) / 400;
            long yoe = y - era * 400;
            long doy = (153L * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
            long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + doe - 719468;
        }

        /// <summary>Returns {year, month (1-12), day (1-31)}.</summary>
        public static void CivilFromDays(long z, out long year, out long month, out long day)
        {
            z += 719468;
            long era = (z >= 0 ? z : z - 146096) / 146097;
            long doe = z - era * 146097;
            long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            long mp = (5 * doy + 2) / 153;
            day   = doy - (153 * mp + 2) / 5 + 1;
            month = mp < 10 ? mp + 3 : mp - 9;
            year  = yoe + era * 400 + (month <= 2 ? 1 : 0);
        }

        private static long FloorDiv(long a, long b)
        {
            long q = a / b;
            return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
        }

        public static long CurrentUnixTimeMillis()
        {
            return (long)(DateTime.UtcNow - new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc)).TotalMilliseconds;
        }

        public static long DayFromMillis(long millis)
        {
            return FloorDiv(millis / 1000 + OffsetMinutes * 60, 86400);
        }

        public static long Today()
        {
            return DayFromMillis(CurrentUnixTimeMillis());
        }

        /// <summary>0 = Monday … 6 = Sunday. 1970-01-01 was a Thursday (index 3).</summary>
        public static int WeekdayOfDay(long day)
        {
            return (int)(((day % 7) + 7 + 3) % 7);
        }

        public static string StringFromDay(long day)
        {
            long y, m, d;
            CivilFromDays(day, out y, out m, out d);
            return string.Format("{0:D4}-{1:D2}-{2:D2}", y, m, d);
        }

        /// <summary>Accepts "yyyy-MM-dd" and anything starting with it; null when unparsable.</summary>
        public static long? Parse(string s)
        {
            if (s == null || s.Length < 10) return null;
            string sub = s.Substring(0, 10);
            if (sub[4] != '-' || sub[7] != '-') return null;
            int y, m, d;
            if (!int.TryParse(sub.Substring(0, 4), out y)) return null;
            if (!int.TryParse(sub.Substring(5, 2), out m)) return null;
            if (!int.TryParse(sub.Substring(8, 2), out d)) return null;
            if (m < 1 || m > 12 || d < 1 || d > 31) return null;
            return DaysFromCivil(y, m, d);
        }

        /// <summary>Monday of the week to show now; on Sunday that is the next week.</summary>
        public static string CurrentWeekStart()
        {
            long today = Today();
            if (WeekdayOfDay(today) == 6) today += 1;
            return StringFromDay(today - WeekdayOfDay(today));
        }

        public static string ShiftWeek(string weekStart, int weeks)
        {
            long? day = Parse(weekStart);
            if (day == null) return CurrentWeekStart();
            long monday = day.Value - WeekdayOfDay(day.Value);
            return StringFromDay(monday + weeks * 7L);
        }

        /// <summary>"2–8 ноября"</summary>
        public static string WeekTitle(string weekStart)
        {
            long? start = Parse(weekStart);
            if (start == null) return weekStart;
            long unusedY, ma, da, mb, db;
            CivilFromDays(start.Value,     out unusedY, out ma, out da);
            CivilFromDays(start.Value + 6, out unusedY, out mb, out db);
            if (ma == mb)
                return da + "–" + db + " " + Months[(int)ma - 1];
            return da + " " + Months[(int)ma - 1] + " – " + db + " " + Months[(int)mb - 1];
        }

        /// <summary>"Понедельник, 2 ноября"</summary>
        public static string DayTitle(string date)
        {
            long? day = Parse(date);
            if (day == null) return date;
            long unusedY, m, d;
            CivilFromDays(day.Value, out unusedY, out m, out d);
            return Weekdays[WeekdayOfDay(day.Value)] + ", " + d + " " + Months[(int)m - 1];
        }

        public static bool IsToday(string date)
        {
            long? day = Parse(date);
            return day.HasValue && day.Value == Today();
        }

        public static int WeekdayOfDate(string date)
        {
            long? day = Parse(date);
            return day.HasValue ? WeekdayOfDay(day.Value) : -1;
        }

        /// <summary>485 → "08:05"</summary>
        public static string TimeFromMinutes(int minutes)
        {
            int m = ((minutes % 1440) + 1440) % 1440;
            return string.Format("{0:D2}:{1:D2}", m / 60, m % 60);
        }

        /// <summary>"2026-11-05T18:59:00.000Z" → "05.11.2026 23:59" (college time UTC+5).</summary>
        public static string DeadlineText(string raw)
        {
            if (raw == null) return raw;
            string text = raw.Trim();
            long? day = Parse(text);
            if (day == null) return raw;
            if (text.Length >= 16 && text[10] == 'T' && text[13] == ':')
            {
                try
                {
                    int hour   = int.Parse(text.Substring(11, 2));
                    int minute = int.Parse(text.Substring(14, 2));
                    long total = day.Value * 1440 + hour * 60 + minute;
                    string tail = text.Substring(16);
                    int plus  = tail.IndexOf('+');
                    int minus = tail.IndexOf('-');
                    int sign  = plus < 0 ? minus : minus < 0 ? plus : Math.Min(plus, minus);
                    if (tail.EndsWith("Z"))
                        total += OffsetMinutes;
                    else if (sign >= 0)
                    {
                        string zone = tail.Substring(sign + 1).Replace(":", "");
                        int oh = zone.Length >= 2 ? int.Parse(zone.Substring(0, 2)) : 0;
                        int om = zone.Length >= 4 ? int.Parse(zone.Substring(2, 2)) : 0;
                        long offset = (oh * 60L + om) * (tail[sign] == '-' ? -1 : 1);
                        total += OffsetMinutes - offset;
                    }
                    long localDay  = FloorDiv(total, 1440);
                    long mins      = total - localDay * 1440;
                    long y, m, d;
                    CivilFromDays(localDay, out y, out m, out d);
                    return string.Format("{0:D2}.{1:D2}.{2:D4} {3:D2}:{4:D2}", d, m, y, mins / 60, mins % 60);
                }
                catch { /* fall through to date only */ }
            }
            long yr, mo, dy;
            CivilFromDays(day.Value, out yr, out mo, out dy);
            return string.Format("{0:D2}.{1:D2}.{2:D4}", dy, mo, yr);
        }
    }
}
