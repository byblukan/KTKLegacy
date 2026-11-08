package ru.ktkschedule.android;

import java.util.Locale;

/**
 * College dates. Days are counted from 1970-01-01 in a fixed UTC+5: old Android ships an old tz
 * database where Asia/Yekaterinburg may still be UTC+6, so a named zone could be an hour off.
 * Pure arithmetic, safe on any thread.
 */
final class CollegeCalendar {
    static final long OFFSET_MINUTES = 5 * 60;

    private static final String[] MONTHS = {
        "января", "февраля", "марта", "апреля", "мая", "июня",
        "июля", "августа", "сентября", "октября", "ноября", "декабря",
    };
    private static final String[] WEEKDAYS = {
        "Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота", "Воскресенье",
    };

    private CollegeCalendar() {
    }

    // Howard Hinnant's civil date algorithms (proleptic Gregorian).
    static long daysFromCivil(long y, int m, int d) {
        y -= m <= 2 ? 1 : 0;
        long era = (y >= 0 ? y : y - 399) / 400;
        long yoe = y - era * 400;
        long doy = (153L * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
        long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    /** {year, month, day}. */
    static long[] civilFromDays(long z) {
        z += 719468;
        long era = (z >= 0 ? z : z - 146096) / 146097;
        long doe = z - era * 146097;
        long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        long mp = (5 * doy + 2) / 153;
        long day = doy - (153 * mp + 2) / 5 + 1;
        long month = mp < 10 ? mp + 3 : mp - 9;
        long year = yoe + era * 400 + (month <= 2 ? 1 : 0);
        return new long[]{year, month, day};
    }

    static long floorDiv(long a, long b) {
        long q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    static long dayFromMillis(long millis) {
        return floorDiv(millis / 1000 + OFFSET_MINUTES * 60, 86400);
    }

    static long today() {
        return dayFromMillis(System.currentTimeMillis());
    }

    /** 0 = Monday ... 6 = Sunday. */
    static int weekdayOfDay(long day) {
        // 1970-01-01 was a Thursday (3 when Monday is 0).
        return (int) (((day % 7) + 7 + 3) % 7);
    }

    static String stringFromDay(long day) {
        long[] c = civilFromDays(day);
        return String.format(Locale.US, "%04d-%02d-%02d", c[0], c[1], c[2]);
    }

    /** Accepts "yyyy-MM-dd" and anything that starts with it; null when unparsable. */
    static Long parse(String string) {
        if (string == null || string.length() < 10) {
            return null;
        }
        String s = string.substring(0, 10);
        if (s.charAt(4) != '-' || s.charAt(7) != '-') {
            return null;
        }
        try {
            int y = Integer.parseInt(s.substring(0, 4));
            int m = Integer.parseInt(s.substring(5, 7));
            int d = Integer.parseInt(s.substring(8, 10));
            if (m < 1 || m > 12 || d < 1 || d > 31) {
                return null;
            }
            return daysFromCivil(y, m, d);
        } catch (NumberFormatException e) {
            return null;
        }
    }

    /** Monday of the week to show now; on Sunday that is the next week. */
    static String currentWeekStart() {
        long today = today();
        if (weekdayOfDay(today) == 6) {
            today += 1;
        }
        return stringFromDay(today - weekdayOfDay(today));
    }

    static String shiftWeek(String weekStart, int weeks) {
        Long day = parse(weekStart);
        if (day == null) {
            return currentWeekStart();
        }
        long monday = day - weekdayOfDay(day);
        return stringFromDay(monday + weeks * 7L);
    }

    /** "2–8 ноября". */
    static String weekTitle(String weekStart) {
        Long start = parse(weekStart);
        if (start == null) {
            return weekStart;
        }
        long[] a = civilFromDays(start);
        long[] b = civilFromDays(start + 6);
        if (a[1] == b[1]) {
            return a[2] + "–" + b[2] + " " + MONTHS[(int) a[1] - 1];
        }
        return a[2] + " " + MONTHS[(int) a[1] - 1] + " – " + b[2] + " " + MONTHS[(int) b[1] - 1];
    }

    /** "Понедельник, 2 ноября". */
    static String dayTitle(String date) {
        Long day = parse(date);
        if (day == null) {
            return date;
        }
        long[] c = civilFromDays(day);
        return WEEKDAYS[weekdayOfDay(day)] + ", " + c[2] + " " + MONTHS[(int) c[1] - 1];
    }

    static boolean isToday(String date) {
        Long day = parse(date);
        return day != null && day == today();
    }

    /** -1 when unparsable. */
    static int weekdayOfDate(String date) {
        Long day = parse(date);
        return day != null ? weekdayOfDay(day) : -1;
    }

    /** 485 -> "08:05". */
    static String timeFromMinutes(int minutes) {
        int m = ((minutes % 1440) + 1440) % 1440;
        return String.format(Locale.US, "%02d:%02d", m / 60, m % 60);
    }

    /** "2026-11-05T18:59:00.000Z" -> "05.11.2026 23:59" (college time); unknown formats unchanged. */
    static String deadlineText(String raw) {
        String text = raw.trim();
        Long day = parse(text);
        if (day == null) {
            return raw;
        }
        if (text.length() >= 16 && text.charAt(10) == 'T' && text.charAt(13) == ':') {
            try {
                int hour = Integer.parseInt(text.substring(11, 13));
                int minute = Integer.parseInt(text.substring(14, 16));
                long total = day * 1440 + hour * 60 + minute;
                String tail = text.substring(16);
                int plus = tail.indexOf('+');
                int minus = tail.indexOf('-');
                int sign = plus < 0 ? minus : minus < 0 ? plus : Math.min(plus, minus);
                if (tail.endsWith("Z")) {
                    total += OFFSET_MINUTES;
                } else if (sign >= 0) {
                    String zone = tail.substring(sign + 1).replace(":", "");
                    int oh = zone.length() >= 2 ? Integer.parseInt(zone.substring(0, 2)) : 0;
                    int om = zone.length() >= 4 ? Integer.parseInt(zone.substring(2, 4)) : 0;
                    long offset = (oh * 60L + om) * (tail.charAt(sign) == '-' ? -1 : 1);
                    total += OFFSET_MINUTES - offset;
                }
                long localDay = floorDiv(total, 1440);
                long minutes = total - localDay * 1440;
                long[] c = civilFromDays(localDay);
                return String.format(Locale.US, "%02d.%02d.%04d %02d:%02d", c[2], c[1], c[0], minutes / 60, minutes % 60);
            } catch (NumberFormatException e) {
                // fall through to the date only
            }
        }
        long[] c = civilFromDays(day);
        return String.format(Locale.US, "%02d.%02d.%04d", c[2], c[1], c[0]);
    }

    /** "03.11 20:45". */
    static String shortDateTime(long millis) {
        long seconds = floorDiv(millis, 1000) + OFFSET_MINUTES * 60;
        long day = floorDiv(seconds, 86400);
        long rest = seconds - day * 86400;
        long[] c = civilFromDays(day);
        return String.format(Locale.US, "%02d.%02d %02d:%02d", c[2], c[1], rest / 3600, (rest % 3600) / 60);
    }
}
