#import "KTKCalendar.h"
#include <math.h>
#include <stdio.h>

static const long KTKOffsetMinutes = 5 * 60;

static NSString *const KTKMonths[12] = {
    @"января", @"февраля", @"марта", @"апреля", @"мая", @"июня",
    @"июля", @"августа", @"сентября", @"октября", @"ноября", @"декабря",
};
static NSString *const KTKWeekdays[7] = {
    @"Понедельник", @"Вторник", @"Среда", @"Четверг", @"Пятница", @"Суббота", @"Воскресенье",
};

// Howard Hinnant's civil date algorithms (proleptic Gregorian).
static long KTKDaysFromCivil(long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static void KTKCivilFromDays(long z, long *year, unsigned *month, unsigned *day) {
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *day = doy - (153 * mp + 2) / 5 + 1;
    *month = mp < 10 ? mp + 3 : mp - 9;
    *year = (long)yoe + era * 400 + (*month <= 2);
}

static long KTKFloorDiv(long a, long b) {
    long q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

@implementation KTKCalendar

+ (long)dayFromDate:(NSDate *)date {
    return (long)floor((date.timeIntervalSince1970 + KTKOffsetMinutes * 60) / 86400.0);
}

+ (long)today {
    return [self dayFromDate:[NSDate date]];
}

+ (NSInteger)weekdayOfDay:(long)day {
    // 1970-01-01 was a Thursday (3 when Monday is 0).
    return (NSInteger)(((day % 7) + 7 + 3) % 7);
}

+ (NSString *)stringFromDay:(long)day {
    long y;
    unsigned m, d;
    KTKCivilFromDays(day, &y, &m, &d);
    return [NSString stringWithFormat:@"%04ld-%02u-%02u", y, m, d];
}

+ (BOOL)parse:(NSString *)string day:(long *)day {
    if (string.length < 10) {
        return NO;
    }
    int y, m, d;
    if (sscanf([string substringToIndex:10].UTF8String, "%4d-%2d-%2d", &y, &m, &d) != 3 ||
        m < 1 || m > 12 || d < 1 || d > 31) {
        return NO;
    }
    *day = KTKDaysFromCivil(y, (unsigned)m, (unsigned)d);
    return YES;
}

+ (NSString *)currentWeekStart {
    long today = [self today];
    if ([self weekdayOfDay:today] == 6) {
        today += 1;
    }
    return [self stringFromDay:today - [self weekdayOfDay:today]];
}

+ (NSString *)weekStart:(NSString *)weekStart shiftedBy:(NSInteger)weeks {
    long day;
    if (![self parse:weekStart day:&day]) {
        return [self currentWeekStart];
    }
    day -= [self weekdayOfDay:day];
    return [self stringFromDay:day + weeks * 7];
}

+ (NSString *)weekTitle:(NSString *)weekStart {
    long start;
    if (![self parse:weekStart day:&start]) {
        return weekStart;
    }
    long y1, y2;
    unsigned m1, d1, m2, d2;
    KTKCivilFromDays(start, &y1, &m1, &d1);
    KTKCivilFromDays(start + 6, &y2, &m2, &d2);
    if (m1 == m2) {
        return [NSString stringWithFormat:@"%u–%u %@", d1, d2, KTKMonths[m1 - 1]];
    }
    return [NSString stringWithFormat:@"%u %@ – %u %@", d1, KTKMonths[m1 - 1], d2, KTKMonths[m2 - 1]];
}

+ (NSString *)dayTitle:(NSString *)date {
    long day;
    if (![self parse:date day:&day]) {
        return date;
    }
    long y;
    unsigned m, d;
    KTKCivilFromDays(day, &y, &m, &d);
    return [NSString stringWithFormat:@"%@, %u %@", KTKWeekdays[[self weekdayOfDay:day]], d, KTKMonths[m - 1]];
}

+ (BOOL)isToday:(NSString *)date {
    long day;
    return [self parse:date day:&day] && day == [self today];
}

+ (NSInteger)weekdayOfDate:(NSString *)date {
    long day;
    return [self parse:date day:&day] ? [self weekdayOfDay:day] : -1;
}

+ (NSString *)timeFromMinutes:(NSInteger)minutes {
    long m = ((minutes % 1440) + 1440) % 1440;
    return [NSString stringWithFormat:@"%02ld:%02ld", m / 60, m % 60];
}

+ (NSString *)deadlineText:(NSString *)raw {
    NSString *text = [raw stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    long day;
    if (![self parse:text day:&day]) {
        return raw;
    }
    long y;
    unsigned m, d;
    int hour, minute;
    if (text.length >= 16 && sscanf([text substringWithRange:NSMakeRange(10, 6)].UTF8String, "T%2d:%2d",
                                    &hour, &minute) == 2) {
        long total = day * 1440 + hour * 60 + minute;
        NSString *tail = [text substringFromIndex:16];
        NSRange sign = [tail rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"+-"]];
        if ([tail hasSuffix:@"Z"]) {
            total += KTKOffsetMinutes;
        } else if (sign.location != NSNotFound) {
            int oh = 0, om = 0;
            sscanf([tail substringFromIndex:sign.location + 1].UTF8String, "%2d:%2d", &oh, &om);
            long offset = (oh * 60 + om) * ([tail characterAtIndex:sign.location] == '-' ? -1 : 1);
            total += KTKOffsetMinutes - offset;
        }
        long localDay = KTKFloorDiv(total, 1440);
        long minutes = total - localDay * 1440;
        KTKCivilFromDays(localDay, &y, &m, &d);
        return [NSString stringWithFormat:@"%02u.%02u.%04ld %02ld:%02ld", d, m, y, minutes / 60, minutes % 60];
    }
    KTKCivilFromDays(day, &y, &m, &d);
    return [NSString stringWithFormat:@"%02u.%02u.%04ld", d, m, y];
}

+ (NSString *)shortDateTime:(NSDate *)date {
    long seconds = (long)floor(date.timeIntervalSince1970) + KTKOffsetMinutes * 60;
    long day = KTKFloorDiv(seconds, 86400);
    long rest = seconds - day * 86400;
    long y;
    unsigned m, d;
    KTKCivilFromDays(day, &y, &m, &d);
    return [NSString stringWithFormat:@"%02u.%02u %02ld:%02ld", d, m, rest / 3600, (rest % 3600) / 60];
}

@end
