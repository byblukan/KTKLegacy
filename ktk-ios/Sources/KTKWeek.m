#import "KTKWeek.h"
#import "KTKCalendar.h"

static const NSUInteger KTKMaxFiles = 64;

static NSDictionary *KTKDict(id value) {
    return [value isKindOfClass:[NSDictionary class]] ? value : nil;
}

static NSArray *KTKArray(id value) {
    return [value isKindOfClass:[NSArray class]] ? value : nil;
}

static NSString *KTKString(id value) {
    return [value isKindOfClass:[NSString class]] ? value : nil;
}

/// Trimmed, non-empty string or nil.
static NSString *KTKText(id value) {
    NSString *text = [KTKString(value) stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return text.length ? text : nil;
}

static long long KTKNumber(id value, long long fallback) {
    return [value isKindOfClass:[NSNumber class]] ? [value longLongValue] : fallback;
}

static long long KTKPositiveId(id value) {
    long long number = 0;
    if ([value isKindOfClass:[NSNumber class]]) {
        number = [value longLongValue];
    } else if ([value isKindOfClass:[NSString class]]) {
        number = [[value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]] longLongValue];
    }
    return number > 0 ? number : 0;
}

/// Lesson extras answer without `status` for the own timetable; any other status withholds them.
static BOOL KTKRestricted(NSDictionary *value) {
    id status = value[@"status"];
    return status != nil && ![status isEqual:@"available"];
}

/// "08:00" or "1970-01-01T08:00:00Z" -> minutes after midnight.
static NSInteger KTKBeginMinutes(NSString *begin) {
    NSRange t = [begin rangeOfString:@"T"];
    NSString *time = t.location == NSNotFound ? begin : [begin substringFromIndex:t.location + 1];
    NSArray *parts = [time componentsSeparatedByString:@":"];
    NSInteger hours = parts.count > 0 ? [parts[0] integerValue] : 0;
    NSInteger minutes = parts.count > 1 ? [parts[1] integerValue] : 0;
    return hours * 60 + minutes;
}

@implementation KTKFile

- (NSString *)fileName {
    NSString *caption = self.caption.length ? self.caption : [NSString stringWithFormat:@"file_%lld", self.fileId];
    NSString *extension = self.extension ?: @"";
    while ([extension hasPrefix:@"."]) {
        extension = [extension substringFromIndex:1];
    }
    if (!extension.length || [caption.lowercaseString hasSuffix:[@"." stringByAppendingString:extension.lowercaseString]]) {
        return caption;
    }
    return [NSString stringWithFormat:@"%@.%@", caption, extension];
}

@end

@implementation KTKLesson

- (NSString *)timeText {
    if (self.startMinute < 0) {
        return nil;
    }
    return [NSString stringWithFormat:@"%@–%@", [KTKCalendar timeFromMinutes:self.startMinute],
                                      [KTKCalendar timeFromMinutes:self.endMinute]];
}

static NSString *KTKMarkSymbol(NSInteger mark) {
    switch (mark) {
        case 128: return @"+";
        case 256: return @"-";
        case 16: return @"Н";
        case 32: return @"О";
        default: return [NSString stringWithFormat:@"%ld", (long)mark];
    }
}

- (BOOL)plusMinus {
    return self.mark == 128 || self.mark == 256;
}

- (NSString *)gradeText {
    if (self.grade == 0 && ![self plusMinus]) {
        return nil;
    }
    NSString *grade = self.grade ? [NSString stringWithFormat:@"%ld", (long)self.grade] : @"";
    return [self plusMinus] ? [grade stringByAppendingString:KTKMarkSymbol(self.mark)] : grade;
}

- (NSString *)markText {
    return (self.mark != 0 && ![self plusMinus]) ? KTKMarkSymbol(self.mark) : nil;
}

- (NSString *)badgeText {
    return self.gradeText ?: self.markText;
}

- (BOOL)hasGrade {
    return self.badgeText != nil;
}

- (BOOL)hasTaskContent {
    return self.hasHomework || self.task.length || self.webinar.length || self.files.count;
}

- (NSString *)subgroupText {
    NSString *s = self.subgroup.lowercaseString;
    if ([s isEqualToString:@"left"] || [s isEqualToString:@"1"]) {
        return @"1 подгруппа";
    }
    if ([s isEqualToString:@"right"] || [s isEqualToString:@"2"]) {
        return @"2 подгруппа";
    }
    return nil;
}

- (NSString *)shortTeacher {
    NSArray *parts = [self.teacher componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    NSMutableString *out = [NSMutableString string];
    for (NSString *part in parts) {
        if (!part.length) {
            continue;
        }
        if (!out.length) {
            [out appendString:part];
        } else {
            [out appendFormat:@" %@.", [part substringToIndex:1]];
        }
    }
    return out.length ? out : nil;
}

- (NSString *)hallText {
    if (!self.hall.length) {
        return nil;
    }
    return self.building.length ? [NSString stringWithFormat:@"%@ (%@)", self.hall, self.building] : self.hall;
}

@end

@implementation KTKDay
@end

@implementation KTKWeek

- (NSArray *)allLessons {
    NSMutableArray *all = [NSMutableArray array];
    for (KTKDay *day in self.days) {
        [all addObjectsFromArray:day.lessons];
    }
    return all;
}

+ (instancetype)weekWithPayload:(NSDictionary *)payload filter:(BOOL)filter {
    NSDictionary *root = KTKDict(payload[@"week"]);
    NSArray *daysRaw = KTKArray(root[@"days"]);
    if (!daysRaw) {
        return nil;
    }
    BOOL teacher = [payload[@"teacher"] isKindOfClass:[NSNumber class]] && [payload[@"teacher"] boolValue];
    NSString *weekStart = KTKString(payload[@"weekStart"]) ?: @"";
    NSDictionary *extras = KTKDict(payload[@"extras"]) ?: @{};
    NSDictionary *references = KTKDict(root[@"references"]);

    NSMutableDictionary *teachers = [NSMutableDictionary dictionary];
    for (NSDictionary *t in KTKArray(references[@"teachers"])) {
        NSString *teacherId = KTKString(KTKDict(t)[@"id"]);
        if (!teacherId) {
            continue;
        }
        NSMutableArray *names = [NSMutableArray array];
        for (NSString *key in @[@"surname", @"name", @"patronymic"]) {
            NSString *part = KTKText(t[key]);
            if (part) {
                [names addObject:part];
            }
        }
        teachers[teacherId] = [names componentsJoinedByString:@" "];
    }

    NSMutableDictionary *halls = [NSMutableDictionary dictionary]; // id -> @[number, building, remote]
    for (NSDictionary *a in KTKArray(references[@"auditoriums"])) {
        if (![KTKDict(a)[@"auditoriumId"] isKindOfClass:[NSNumber class]]) {
            continue;
        }
        NSString *code = KTKText(a[@"code"]) ?: @"";
        BOOL virtualHall = [a[@"variant"] isEqual:@"virtual"] || [code isEqualToString:@"Д"];
        NSString *building = virtualHall ? @"" : (KTKText(a[@"buildingShortCode"]) ?: @"");
        halls[a[@"auditoriumId"]] = @[virtualHall ? @"Дистанционно" : code, building, @(virtualHall)];
    }

    NSMutableDictionary *types = [NSMutableDictionary dictionary];
    for (NSDictionary *pt in KTKArray(references[@"pairTypes"])) {
        if ([KTKDict(pt)[@"id"] isKindOfClass:[NSNumber class]]) {
            types[pt[@"id"]] = KTKString(pt[@"name"]) ?: @"";
        }
    }

    // Bell presets: id -> array of @[start, end] minutes, by pair position.
    NSMutableDictionary *presets = [NSMutableDictionary dictionary];
    for (NSDictionary *cp in KTKArray(root[@"callPresets"])) {
        if (![KTKDict(cp)[@"id"] isKindOfClass:[NSNumber class]]) {
            continue;
        }
        NSInteger minutes = KTKBeginMinutes(KTKString(cp[@"beginTime"]) ?: @"");
        NSMutableArray *times = [NSMutableArray array];
        for (NSDictionary *step in KTKArray(cp[@"steps"])) {
            NSInteger duration = (NSInteger)KTKNumber(KTKDict(step)[@"duration"], 0);
            NSInteger pause = (NSInteger)KTKNumber(KTKDict(step)[@"breakDuration"], 0);
            [times addObject:@[@(minutes), @(minutes + duration)]];
            minutes += duration + pause;
        }
        presets[cp[@"id"]] = times;
    }

    NSMutableDictionary *bells = [NSMutableDictionary dictionary]; // date -> preset id
    for (NSDictionary *scope in KTKArray(root[@"bellScopes"])) {
        for (NSDictionary *assignment in KTKArray(KTKDict(scope)[@"assignments"])) {
            NSString *date = KTKString(KTKDict(assignment)[@"date"]);
            id preset = assignment[@"presetId"];
            // Keyed by the yyyy-MM-dd part, like the Android and Symbian clients: a timestamp in
            // either date must not lose the bell times.
            if (date.length >= 10 && [preset isKindOfClass:[NSNumber class]]) {
                bells[[date substringToIndex:10]] = preset;
            }
        }
    }

    NSMutableArray *days = [NSMutableArray array];
    for (NSDictionary *d in daysRaw) {
        NSString *date = KTKString(KTKDict(d)[@"date"]);
        if (!date.length) {
            continue;
        }
        id presetId = date.length >= 10 ? bells[[date substringToIndex:10]] : nil;
        NSArray *times = presetId ? presets[presetId] : nil;
        NSMutableArray *lessons = [NSMutableArray array];

        void (^add)(NSInteger, id, NSString *) = ^(NSInteger pair, id raw, NSString *fallbackSubgroup) {
            NSDictionary *l = KTKDict(raw);
            NSString *discipline = KTKString(l[@"disciplineShortName"]);
            if (!discipline.length) {
                return;
            }
            KTKLesson *lesson = [KTKLesson new];
            lesson.date = date;
            lesson.pair = pair;
            lesson.discipline = discipline;
            lesson.disciplineFull = KTKText(l[@"disciplineFullName"]);
            NSString *teacherId = KTKString(l[@"teacherId"]);
            lesson.teacher = teacherId ? teachers[teacherId] : nil;
            lesson.groupId = KTKString(l[@"groupId"]);
            lesson.subgroup = KTKText(l[@"subgroup"]) ?: fallbackSubgroup;
            NSArray *hall = l[@"auditoriumId"] ? halls[l[@"auditoriumId"]] : nil;
            if (hall) {
                lesson.hall = [hall[0] length] ? hall[0] : nil;
                lesson.building = [hall[1] length] ? hall[1] : nil;
                lesson.remote = [hall[2] boolValue];
            } else if (KTKNumber(l[@"auditoriumId"], 0) > 0) {
                lesson.hall = [l[@"auditoriumId"] stringValue];
            }
            lesson.pairTypeId = KTKNumber(l[@"pairTypeId"], 0);
            lesson.pairTypeName = types[@(lesson.pairTypeId)];
            lesson.academicHours = (NSInteger)KTKNumber(l[@"academicHours"], 2);
            lesson.journalId = KTKNumber(l[@"journalId"], 0);
            NSDictionary *assessment = KTKDict(l[@"assessment"]);
            lesson.grade = (NSInteger)KTKNumber(assessment[@"grade"], 0);
            lesson.mark = (NSInteger)KTKNumber(assessment[@"mark"], 0);
            lesson.hasHomework = [l[@"hasHomework"] isKindOfClass:[NSNumber class]] && [l[@"hasHomework"] boolValue];
            lesson.startMinute = -1;
            lesson.endMinute = -1;
            if (pair >= 1 && (NSUInteger)pair <= times.count) {
                lesson.startMinute = [times[pair - 1][0] integerValue];
                lesson.endMinute = [times[pair - 1][1] integerValue];
            }
            [self applyExtras:KTKDict(extras[[NSString stringWithFormat:@"%lld", lesson.journalId]])
                     toLesson:lesson weekStart:weekStart];
            [lessons addObject:lesson];
        };

        for (NSDictionary *page in KTKArray(d[@"pages"])) {
            for (NSDictionary *p in KTKArray(KTKDict(page)[@"pairs"])) {
                add((NSInteger)KTKNumber(KTKDict(p)[@"pairNumber"], 0), p[@"lesson"], nil);
            }
        }
        for (NSDictionary *p in KTKArray(d[@"pairs"])) {
            if (!KTKDict(p)) {
                continue;
            }
            NSInteger pair = (NSInteger)KTKNumber(p[@"pairNumber"], 0);
            NSDictionary *ordinary = KTKDict(p[@"ordinary"]);
            NSDictionary *lists = @{@"wholeLessons": @"middle", @"leftLessons": @"left", @"rightLessons": @"right"};
            for (NSString *key in @[@"wholeLessons", @"leftLessons", @"rightLessons"]) {
                for (id raw in KTKArray(ordinary[key])) {
                    add(pair, raw, lists[key]);
                }
            }
            for (NSDictionary *direction in KTKArray(p[@"directions"])) {
                for (id raw in KTKArray(KTKDict(direction)[@"lessons"])) {
                    add(pair, raw, nil);
                }
            }
        }

        NSArray *sorted = [lessons sortedArrayWithOptions:NSSortStable usingComparator:^NSComparisonResult(KTKLesson *a, KTKLesson *b) {
            return a.pair < b.pair ? NSOrderedAscending : a.pair > b.pair ? NSOrderedDescending : NSOrderedSame;
        }];
        if (filter && !teacher && sorted.count) {
            BOOL allRemote = YES, anyContent = NO;
            for (KTKLesson *lesson in sorted) {
                allRemote = allRemote && lesson.remote;
                anyContent = anyContent || lesson.hasTaskContent;
            }
            if (allRemote && !anyContent) {
                sorted = @[];
            }
        }
        KTKDay *day = [KTKDay new];
        day.date = [date substringToIndex:MIN(date.length, (NSUInteger)10)];
        day.lessons = sorted;
        [days addObject:day];
    }
    if (!days.count) {
        return nil;
    }
    KTKWeek *week = [KTKWeek new];
    week.weekStart = weekStart;
    week.days = days;
    week.teacher = teacher;
    if ([payload[@"savedAt"] isKindOfClass:[NSNumber class]]) {
        week.updatedAt = [NSDate dateWithTimeIntervalSince1970:[payload[@"savedAt"] doubleValue]];
    }
    return week;
}

/// `homework`: {homework: {task, deadline, files: [{userFileId, caption, extension, availability}]} | null}
/// `webinar`: {webinar: "https://..." | null}
+ (void)applyExtras:(NSDictionary *)extra toLesson:(KTKLesson *)lesson weekStart:(NSString *)weekStart {
    if (!extra) {
        return;
    }
    NSDictionary *homeworkCall = KTKDict(extra[@"homework"]);
    NSDictionary *homework = KTKDict(homeworkCall[@"homework"]);
    if (homework && !KTKRestricted(homeworkCall)) {
        lesson.task = KTKText(homework[@"task"]);
        lesson.deadline = KTKText(homework[@"deadline"]);
        NSMutableArray *files = [NSMutableArray array];
        for (NSDictionary *f in KTKArray(homework[@"files"])) {
            long long fileId = KTKPositiveId(KTKDict(f)[@"userFileId"]);
            if (!fileId || ![f[@"availability"] isEqual:@"available"] || files.count >= KTKMaxFiles) {
                continue;
            }
            KTKFile *file = [KTKFile new];
            file.fileId = fileId;
            file.journalId = lesson.journalId;
            file.caption = KTKText(f[@"caption"]) ?: @"";
            file.extension = KTKText(f[@"extension"]) ?: @"";
            file.weekStart = weekStart;
            [files addObject:file];
        }
        lesson.files = files;
    }
    NSDictionary *webinarCall = KTKDict(extra[@"webinar"]);
    if (webinarCall && !KTKRestricted(webinarCall)) {
        lesson.webinar = KTKText(webinarCall[@"webinar"]);
    }
}

@end
