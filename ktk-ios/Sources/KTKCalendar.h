#import <Foundation/Foundation.h>

/// College dates. Days are counted from 1970-01-01 in a fixed UTC+5: iOS 6 ships the 2012
/// tz database, where Asia/Yekaterinburg is still UTC+6, so a named zone would be an hour off.
/// Pure arithmetic, safe on any thread (NSDateFormatter is not thread-safe before iOS 7).
@interface KTKCalendar : NSObject
+ (long)today;
+ (long)dayFromDate:(NSDate *)date;
/// 0 = Monday ... 6 = Sunday.
+ (NSInteger)weekdayOfDay:(long)day;
+ (NSString *)stringFromDay:(long)day; // yyyy-MM-dd
/// Accepts "yyyy-MM-dd" and anything that starts with it.
+ (BOOL)parse:(NSString *)string day:(long *)day;

/// Monday of the week to show now; on Sunday that is the next week.
+ (NSString *)currentWeekStart;
+ (NSString *)weekStart:(NSString *)weekStart shiftedBy:(NSInteger)weeks;
+ (NSString *)weekTitle:(NSString *)weekStart;  // "2–8 ноября"
+ (NSString *)dayTitle:(NSString *)date;        // "Понедельник, 2 ноября"
+ (BOOL)isToday:(NSString *)date;
+ (NSInteger)weekdayOfDate:(NSString *)date;    // -1 when unparsable

+ (NSString *)timeFromMinutes:(NSInteger)minutes; // 485 -> "08:05"
/// "2026-11-05T18:59:00.000Z" -> "05.11.2026 23:59" (college time); unknown formats unchanged.
+ (NSString *)deadlineText:(NSString *)raw;
+ (NSString *)shortDateTime:(NSDate *)date;       // "03.11 20:45"
@end
