#import <Foundation/Foundation.h>

/// A task file; downloads go to the lesson's file route of the own timetable.
@interface KTKFile : NSObject
@property (nonatomic) long long fileId;
@property (nonatomic) long long journalId;
@property (nonatomic, copy) NSString *caption;
@property (nonatomic, copy) NSString *extension;
@property (nonatomic, copy) NSString *weekStart;
/// "Отчёт" + "pdf" -> "Отчёт.pdf"; a caption already ending with the extension stays.
- (NSString *)fileName;
@end

@interface KTKLesson : NSObject
@property (nonatomic, copy) NSString *date;
@property (nonatomic) NSInteger pair;
@property (nonatomic, copy) NSString *discipline;
@property (nonatomic, copy) NSString *disciplineFull;
@property (nonatomic, copy) NSString *teacher;   // "Фамилия Имя Отчество"
@property (nonatomic, copy) NSString *groupId;
@property (nonatomic, copy) NSString *subgroup;  // left/right/middle or as the site says
@property (nonatomic, copy) NSString *hall;      // "18А", "Дистанционно" or nil
@property (nonatomic, copy) NSString *building;
@property (nonatomic) BOOL remote;
@property (nonatomic) long long pairTypeId;
@property (nonatomic, copy) NSString *pairTypeName;
@property (nonatomic) NSInteger academicHours;
@property (nonatomic) long long journalId;
@property (nonatomic) NSInteger grade;           // 0 = none
@property (nonatomic) NSInteger mark;            // 0 = none; 128 "+", 256 "-", 16 "Н", ...
@property (nonatomic) BOOL hasHomework;
@property (nonatomic) NSInteger startMinute;     // -1 when the bell preset is unknown
@property (nonatomic) NSInteger endMinute;
@property (nonatomic, copy) NSString *task;
@property (nonatomic, copy) NSString *deadline;
@property (nonatomic, copy) NSString *webinar;
@property (nonatomic, copy) NSArray *files;      // KTKFile

- (NSString *)timeText;      // "08:00–09:30" or nil
- (NSString *)badgeText;     // "5", "+", "Н" or nil
- (NSString *)gradeText;     // "5", "5+" or nil
- (NSString *)markText;      // "Н" etc. or nil (only marks that are not a +/- next to a grade)
- (NSString *)subgroupText;  // "1 подгруппа" or nil
- (NSString *)shortTeacher;  // "Иванова А. П."
- (NSString *)hallText;      // "18А (IT)" or nil
- (BOOL)hasGrade;
- (BOOL)hasTaskContent;
@end

@interface KTKDay : NSObject
@property (nonatomic, copy) NSString *date; // yyyy-MM-dd
@property (nonatomic, copy) NSArray *lessons;
@end

@interface KTKWeek : NSObject
@property (nonatomic, copy) NSString *weekStart;
@property (nonatomic, copy) NSArray *days;
@property (nonatomic) BOOL teacher;
@property (nonatomic, strong) NSDate *updatedAt;
/// Shown from the cache because the site could not be reached.
@property (nonatomic) BOOL offline;

/// payload: {weekStart, week: decoded server answer, teacher, extras: {journalId: {homework, webinar}}, savedAt}.
/// `filter` applies the bot's rule: a student's day of distance pairs only is shown when any pair
/// has a webinar or a task.
+ (instancetype)weekWithPayload:(NSDictionary *)payload filter:(BOOL)filter;
- (NSArray *)allLessons;
@end
