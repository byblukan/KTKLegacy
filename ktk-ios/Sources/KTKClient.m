#import "KTKClient.h"
#import "KTKCalendar.h"
#import "KTKSeroval.h"
#import <CommonCrypto/CommonDigest.h>

NSString *const KTKSessionDidChangeNotification = @"KTKSessionDidChange";

static NSString *const KTKBase = @"https://ktk.education-system.ru";
/// Part of the sign-in fingerprint: the site lists this app as its own device.
static NSString *const KTKDeviceName = @"ktk-ios6";
static NSString *const KTKUserAgent =
    @"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36";
static const NSUInteger KTKJSONLimit = 4 * 1024 * 1024;
static const NSUInteger KTKFileLimit = 50 * 1024 * 1024;
/// Lessons per week whose task/webinar is fetched (the bot's MAX_LESSON_EXTRAS / 2).
static const NSUInteger KTKMaxExtras = 20;
static const NSUInteger KTKMaxScripts = 64;
static NSString *const KTKPathsDefaultsKey = @"KTKServerFunctions";

typedef NS_ENUM(NSInteger, KTKServerFn) {
    KTKFnSignIn,
    KTKFnStudent,
    KTKFnTeacher,
    KTKFnGroup,
    KTKFnLogout,
    KTKFnStudentHomework,
    KTKFnStudentWebinar,
};

static NSString *KTKFnName(KTKServerFn fn) {
    switch (fn) {
        case KTKFnSignIn: return @"signIn";
        case KTKFnStudent: return @"student";
        case KTKFnTeacher: return @"teacher";
        case KTKFnGroup: return @"group";
        case KTKFnLogout: return @"logout";
        case KTKFnStudentHomework: return @"studentHomework";
        case KTKFnStudentWebinar: return @"studentWebinar";
    }
    return @"";
}

/// Known ids (content hashes; a site deploy changes them, see -rediscover:).
static NSString *KTKFnDefault(KTKServerFn fn) {
    switch (fn) {
        case KTKFnSignIn: return @"/_serverFn/9cd5b48fd0588913cb64c83d8ae2b42d5f3e97ed5dd47dc10f211cdc2322c1f6";
        case KTKFnStudent: return @"/_serverFn/85d2195b8f64b023a2c16160b65ef47139dea3a839096c4b75b3888cda49f16c";
        case KTKFnTeacher: return @"/_serverFn/9fd2616a2ecddd56f79974f2111a13f2c9d89265cb6832c8c9e7ae2939e56f4a";
        case KTKFnGroup: return @"/_serverFn/d418f54f8d5ba132bd168b1792cc8f0efbff634c7861f6aa66890cb9f7103c05";
        case KTKFnLogout: return @"/_serverFn/2e1ca1208f926608597adee62b6f7a786ebfd7e01b208963ecb9bee6e800f9f5";
        case KTKFnStudentHomework: return @"/_serverFn/6856a131ea327da0ae2769d0e8b9950bce030194dda15acd331dd5ad2c273f38";
        case KTKFnStudentWebinar: return @"/_serverFn/b218948fba15961755e1cf6c0742e352d8709719e99ac7bd3e06315787e13650";
    }
    return @"";
}

static NSDictionary *KTKDict(id value) {
    return [value isKindOfClass:[NSDictionary class]] ? value : nil;
}

static NSString *KTKString(id value) {
    return [value isKindOfClass:[NSString class]] ? value : nil;
}

static NSString *KTKFingerprint(NSString *login) {
    NSMutableData *input = [[login dataUsingEncoding:NSUTF8StringEncoding] mutableCopy];
    [input appendData:[KTKDeviceName dataUsingEncoding:NSUTF8StringEncoding]];
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(input.bytes, (CC_LONG)input.length, hash);
    NSMutableString *hex = [NSMutableString stringWithCapacity:32];
    for (int i = 0; i < 16; i++) {
        [hex appendFormat:@"%02x", hash[i]];
    }
    return hex;
}

/// Server functions report failures in-band (HTTP 200 with {error: {appError: {code}}}).
/// An expired or revoked session must become KTKErrorLoginRequired.
static NSError *KTKServerError(id value) {
    id error = KTKDict(value)[@"error"];
    if (!error || error == [NSNull null]) {
        return nil;
    }
    NSDictionary *details = KTKDict(KTKDict(error)[@"appError"]) ?: KTKDict(error);
    NSString *code = [KTKString(details[@"code"]) lowercaseString] ?: @"";
    NSString *type = [KTKString(details[@"type"]) lowercaseString] ?: @"";
    for (NSString *word in @[@"auth", @"session", @"unauthor", @"forbidden", @"login"]) {
        if ([code rangeOfString:word].location != NSNotFound || [type rangeOfString:word].location != NSNotFound) {
            return KTKMakeError(KTKErrorLoginRequired, nil);
        }
    }
    return KTKMakeError(KTKErrorServer, KTKString(details[@"message"]) ?: KTKString(error));
}

static BOOL KTKShouldRediscover(NSError *error) {
    if (error.code == KTKErrorBadResponse) {
        return YES;
    }
    NSInteger status = [error.userInfo[KTKHTTPStatusKey] integerValue];
    return error.code == KTKErrorHTTPStatus && (status == 404 || status == 410);
}

static NSError *KTKStatusError(NSInteger status) {
    return [NSError errorWithDomain:KTKErrorDomain code:KTKErrorHTTPStatus userInfo:@{
        KTKHTTPStatusKey: @(status),
        NSLocalizedDescriptionKey: [NSString stringWithFormat:@"Сайт ответил ошибкой %ld.", (long)status],
    }];
}

/// Merge Set-Cookie headers into a jar; returns whether anything changed.
static BOOL KTKAbsorbCookies(NSArray *lines, NSMutableDictionary *jar) {
    BOOL changed = NO;
    NSCharacterSet *space = [NSCharacterSet whitespaceCharacterSet];
    @synchronized (jar) {
        for (NSString *line in lines) {
            NSArray *parts = [line componentsSeparatedByString:@";"];
            NSString *pair = [parts[0] stringByTrimmingCharactersInSet:space];
            NSRange equals = [pair rangeOfString:@"="];
            if (equals.location == NSNotFound || equals.location == 0) {
                continue;
            }
            NSString *name = [[pair substringToIndex:equals.location] stringByTrimmingCharactersInSet:space];
            NSString *value = [[pair substringFromIndex:equals.location + 1] stringByTrimmingCharactersInSet:space];
            BOOL expired = value.length == 0;
            for (NSUInteger i = 1; i < parts.count; i++) {
                NSString *attribute = [[parts[i] stringByTrimmingCharactersInSet:space] lowercaseString];
                if ([attribute hasPrefix:@"max-age="] && [[attribute substringFromIndex:8] integerValue] <= 0) {
                    expired = YES;
                }
            }
            if (expired) {
                if (jar[name]) {
                    [jar removeObjectForKey:name];
                    changed = YES;
                }
            } else if (![jar[name] isEqualToString:value]) {
                jar[name] = value;
                changed = YES;
            }
        }
    }
    return changed;
}

static NSString *KTKCachesPath(NSString *name) {
    NSString *caches = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES)[0];
    return [caches stringByAppendingPathComponent:name];
}

NSString *KTKErrorText(NSError *error) {
    if (!error) {
        return nil;
    }
    if (![error.domain isEqualToString:KTKErrorDomain]) {
        return error.localizedDescription;
    }
    NSString *detail = error.userInfo[NSLocalizedDescriptionKey];
    switch ((KTKErrorCode)error.code) {
        case KTKErrorNetwork:
            return detail ? [NSString stringWithFormat:@"Нет связи с сайтом колледжа.\n(%@)", detail]
                          : @"Нет связи с сайтом колледжа.";
        case KTKErrorLoginRequired:
            return @"Сессия закончилась. Войдите снова.";
        case KTKErrorCredentials:
            return detail ?: @"Неверный логин или пароль.";
        case KTKErrorBadResponse:
            return @"Сайт ответил непонятно. Возможно, он обновился.";
        case KTKErrorHTTPStatus:
        case KTKErrorTooLarge:
        case KTKErrorServer:
        case KTKErrorFile:
            return detail ?: @"Ошибка сайта.";
    }
    return detail ?: @"Ошибка.";
}

@implementation KTKClient {
    KTKSession *_session;
    NSMutableDictionary *_jar;   // cookies of the current session
    NSMutableDictionary *_paths; // server function name -> path
    NSLock *_crawlLock;
    NSDictionary *_discovered;   // result of this launch's crawl
}

+ (instancetype)shared {
    static KTKClient *shared;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        shared = [KTKClient new];
    });
    return shared;
}

- (instancetype)init {
    if ((self = [super init])) {
        _session = [KTKSession load];
        _jar = [_session.cookies mutableCopy] ?: [NSMutableDictionary dictionary];
        NSDictionary *saved = [[NSUserDefaults standardUserDefaults] dictionaryForKey:KTKPathsDefaultsKey];
        _paths = [saved mutableCopy] ?: [NSMutableDictionary dictionary];
        _crawlLock = [NSLock new];
    }
    return self;
}

- (KTKSession *)session {
    @synchronized (self) {
        return _session;
    }
}

#pragma mark - server functions

- (NSString *)pathFor:(KTKServerFn)fn {
    @synchronized (self) {
        NSString *path = KTKString(_paths[KTKFnName(fn)]);
        return [path hasPrefix:@"/_serverFn/"] ? path : KTKFnDefault(fn);
    }
}

- (void)setPath:(NSString *)path for:(KTKServerFn)fn {
    @synchronized (self) {
        _paths[KTKFnName(fn)] = path;
        [[NSUserDefaults standardUserDefaults] setObject:[_paths copy] forKey:KTKPathsDefaultsKey];
        [[NSUserDefaults standardUserDefaults] synchronize];
    }
}

- (NSMutableDictionary *)headersWithJar:(NSMutableDictionary *)jar {
    NSMutableDictionary *headers = [@{
        @"User-Agent": KTKUserAgent,
        @"Origin": KTKBase,
        @"Referer": [KTKBase stringByAppendingString:@"/"],
        @"sec-fetch-site": @"same-origin",
        @"sec-fetch-mode": @"cors",
        @"sec-fetch-dest": @"empty",
    } mutableCopy];
    @synchronized (jar) {
        if (jar.count) {
            NSMutableArray *pairs = [NSMutableArray array];
            for (NSString *name in jar) {
                [pairs addObject:[NSString stringWithFormat:@"%@=%@", name, jar[name]]];
            }
            headers[@"Cookie"] = [pairs componentsJoinedByString:@"; "];
        }
    }
    return headers;
}

/// Idempotent requests are retried on connection errors and 5xx, like the bot does.
- (KTKHTTPResponse *)send:(KTKHTTPRequest *)request idempotent:(BOOL)idempotent error:(NSError **)error {
    NSUInteger attempts = idempotent ? 3 : 1;
    for (NSUInteger attempt = 1;; attempt++) {
        NSError *failure = nil;
        KTKHTTPResponse *response = [KTKHTTP perform:request error:&failure];
        BOOL retry = attempt < attempts &&
            ((!response && failure.code == KTKErrorNetwork) || (response && response.status >= 500));
        if (!retry) {
            if (!response && error) {
                *error = failure;
            }
            return response;
        }
        [NSThread sleepForTimeInterval:0.2 * attempt];
    }
}

/// Cookies rotated by the site are saved, so the session survives a restart.
- (void)absorbCookies:(NSArray *)lines into:(NSMutableDictionary *)jar {
    if (!KTKAbsorbCookies(lines, jar)) {
        return;
    }
    @synchronized (self) {
        if (jar == _jar && _session) {
            @synchronized (jar) {
                _session.cookies = [jar copy];
            }
            [_session save];
        }
    }
}

- (id)callOnce:(KTKServerFn)fn path:(NSString *)path fields:(NSArray *)fields
           jar:(NSMutableDictionary *)jar error:(NSError **)error {
    NSData *payload = [NSJSONSerialization dataWithJSONObject:[KTKSeroval requestWithFields:fields] options:0 error:NULL];
    BOOL post = fn == KTKFnSignIn || fn == KTKFnLogout;
    NSString *url = [KTKBase stringByAppendingString:path];
    KTKHTTPRequest *request = [KTKHTTPRequest new];
    if (post) {
        request.method = @"POST";
        request.body = payload;
    } else {
        request.method = @"GET";
        NSString *json = [[NSString alloc] initWithData:payload encoding:NSUTF8StringEncoding];
        url = [url stringByAppendingFormat:@"?payload=%@", [KTKHTTP escape:json]];
    }
    request.URL = [NSURL URLWithString:url];
    NSMutableDictionary *headers = [self headersWithJar:jar];
    if (fn == KTKFnSignIn) {
        headers[@"Referer"] = [KTKBase stringByAppendingString:@"/welcome?redirect=%2F"];
    }
    headers[@"x-tsr-serverfn"] = @"true";
    headers[@"x-application-storage-mode"] = @"ordinary";
    headers[@"Accept"] = @"application/x-tss-framed, application/x-ndjson, application/json";
    if (post) {
        headers[@"Content-Type"] = @"application/json";
    }
    request.headers = headers;
    request.limit = KTKJSONLimit;
    request.timeout = 60;

    KTKHTTPResponse *response = [self send:request idempotent:!post error:error];
    if (!response) {
        return nil;
    }
    [self absorbCookies:response.setCookies into:jar];
    if (fn != KTKFnSignIn && (response.status == 401 || response.status == 403)) {
        *error = KTKMakeError(KTKErrorLoginRequired, nil);
        return nil;
    }
    if (response.status != 200) {
        *error = KTKStatusError(response.status);
        return nil;
    }
    id raw = [NSJSONSerialization JSONObjectWithData:response.body options:NSJSONReadingAllowFragments error:NULL];
    if (!raw) {
        *error = KTKMakeError(KTKErrorBadResponse, nil);
        return nil;
    }
    return [KTKSeroval decode:raw error:error];
}

/// One server function call; after a site deploy its id changes, so look it up once again.
- (id)call:(KTKServerFn)fn fields:(NSArray *)fields jar:(NSMutableDictionary *)jar error:(NSError **)error {
    if (!jar) {
        @synchronized (self) {
            jar = _jar;
        }
    }
    NSString *path = [self pathFor:fn];
    NSError *first = nil;
    id result = [self callOnce:fn path:path fields:fields jar:jar error:&first];
    if (result || !KTKShouldRediscover(first)) {
        if (!result && error) {
            *error = first;
        }
        return result;
    }
    NSString *found = [self rediscover:fn];
    if (!found || [found isEqualToString:path]) {
        if (error) {
            *error = first;
        }
        return nil;
    }
    NSLog(@"KTKLegacy: server function %@ moved, using the rediscovered address", KTKFnName(fn));
    [self setPath:found for:fn];
    NSError *second = nil;
    result = [self callOnce:fn path:found fields:fields jar:jar error:&second];
    if (!result && error) {
        *error = second;
    }
    return result;
}

#pragma mark - sign in / out

- (void)signInWithLogin:(NSString *)login password:(NSString *)password completion:(void (^)(NSError *))completion {
    // The password goes as typed: spaces at its ends may be part of it.
    login = [login stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSMutableDictionary *jar = [NSMutableDictionary dictionary];
        NSError *error = nil;
        NSArray *fields = @[@[@"collegeId", login], @[@"password", password], @[@"fingerprint", KTKFingerprint(login)]];
        NSDictionary *result = KTKDict([self call:KTKFnSignIn fields:fields jar:jar error:&error]);
        KTKSession *session = nil;
        if (result) {
            NSDictionary *user = KTKDict(result[@"user"]);
            if (!user) {
                NSDictionary *failure = KTKDict(result[@"error"]);
                NSString *message = KTKString(KTKDict(failure[@"appError"])[@"message"]) ?: KTKString(failure[@"message"]);
                error = KTKMakeError(KTKErrorCredentials, message);
            } else {
                session = [KTKSession new];
                session.cookies = [jar copy];
                session.sessionId = KTKString(KTKDict(result[@"session"])[@"id"]) ?:
                                    KTKString(KTKDict(user[@"session"])[@"id"]) ?: @"";
                session.login = login;
                session.student = [user[@"isStudent"] isKindOfClass:[NSNumber class]] ? [user[@"isStudent"] boolValue] : YES;
                id group = user[@"group"];
                session.group = [group isKindOfClass:[NSNumber class]] || [group isKindOfClass:[NSString class]] ? [group longLongValue] : 0;
                session.teacherHash = session.student ? @"" : (KTKString(user[@"hash"]) ?: @"teacher");
                session.branch = KTKString(user[@"branch"]) ?: @"";
                if (!session.cookies.count) {
                    session = nil;
                    error = KTKMakeError(KTKErrorBadResponse, nil);
                }
            }
        } else if (!error) {
            error = KTKMakeError(KTKErrorBadResponse, nil);
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (session) {
                [self forgetCaches];
                [session save];
                @synchronized (self) {
                    self->_session = session;
                    self->_jar = [session.cookies mutableCopy];
                }
                [[NSNotificationCenter defaultCenter] postNotificationName:KTKSessionDidChangeNotification object:self];
            }
            completion(error);
        });
    });
}

- (void)signOutLocally {
    @synchronized (self) {
        _session = nil;
        _jar = [NSMutableDictionary dictionary];
    }
    [KTKSession clear];
    [self forgetCaches];
    [[NSNotificationCenter defaultCenter] postNotificationName:KTKSessionDidChangeNotification object:self];
}

- (void)signOut {
    KTKSession *session = self.session;
    [self signOutLocally];
    if (session.sessionId.length) {
        NSMutableDictionary *jar = [session.cookies mutableCopy];
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            [self call:KTKFnLogout fields:@[@[@"id", session.sessionId]] jar:jar error:NULL];
        });
    }
}

- (void)forgetCaches {
    [[NSFileManager defaultManager] removeItemAtPath:KTKCachesPath(@"ktk-weeks") error:NULL];
    [[NSFileManager defaultManager] removeItemAtPath:KTKCachesPath(@"ktk-files") error:NULL];
}

#pragma mark - weeks

- (NSString *)weekCachePath:(NSString *)weekStart {
    return [KTKCachesPath(@"ktk-weeks") stringByAppendingPathComponent:[weekStart stringByAppendingString:@".json"]];
}

- (KTKWeek *)cachedWeek:(NSString *)weekStart {
    NSData *data = [NSData dataWithContentsOfFile:[self weekCachePath:weekStart]];
    NSDictionary *payload = data ? KTKDict([NSJSONSerialization JSONObjectWithData:data options:0 error:NULL]) : nil;
    return payload ? [KTKWeek weekWithPayload:payload filter:YES] : nil;
}

- (void)saveWeekPayload:(NSDictionary *)payload weekStart:(NSString *)weekStart {
    if (![NSJSONSerialization isValidJSONObject:payload]) {
        return;
    }
    NSData *data = [NSJSONSerialization dataWithJSONObject:payload options:0 error:NULL];
    [[NSFileManager defaultManager] createDirectoryAtPath:KTKCachesPath(@"ktk-weeks")
                              withIntermediateDirectories:YES attributes:nil error:NULL];
    [data writeToFile:[self weekCachePath:weekStart]
              options:NSDataWritingAtomic | NSDataWritingFileProtectionCompleteUntilFirstUserAuthentication
                error:NULL];
}

- (NSDictionary *)fetchWeek:(NSString *)weekStart session:(KTKSession *)session error:(NSError **)error {
    BOOL teacher = !session.student;
    id value = [self call:teacher ? KTKFnTeacher : KTKFnStudent fields:@[@[@"weekStart", weekStart]] jar:nil error:error];
    if (!value) {
        return nil;
    }
    NSError *server = KTKServerError(value);
    if (server) {
        *error = server;
        return nil;
    }
    if (![KTKDict(value)[@"days"] isKindOfClass:[NSArray class]]) {
        *error = KTKMakeError(KTKErrorBadResponse, nil);
        return nil;
    }
    NSMutableDictionary *payload = [@{
        @"weekStart": weekStart,
        @"week": value,
        @"teacher": @(teacher),
        @"savedAt": @([[NSDate date] timeIntervalSince1970]),
    } mutableCopy];
    if (!teacher) {
        payload[@"extras"] = [self extrasForPayload:payload weekStart:weekStart];
    }
    return payload;
}

/// Tasks of lessons flagged hasHomework and webinars of distance lessons; best effort.
- (NSDictionary *)extrasForPayload:(NSDictionary *)payload weekStart:(NSString *)weekStart {
    KTKWeek *bare = [KTKWeek weekWithPayload:payload filter:NO];
    NSMutableArray *wanted = [NSMutableArray array];
    NSMutableSet *journals = [NSMutableSet set];
    for (KTKLesson *lesson in bare.allLessons) {
        if (wanted.count >= KTKMaxExtras) {
            break;
        }
        if (lesson.journalId > 0 && (lesson.hasHomework || lesson.remote) && ![journals containsObject:@(lesson.journalId)]) {
            [journals addObject:@(lesson.journalId)];
            [wanted addObject:lesson];
        }
    }
    NSMutableDictionary *extras = [NSMutableDictionary dictionary];
    dispatch_group_t group = dispatch_group_create();
    dispatch_semaphore_t slots = dispatch_semaphore_create(3);
    dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
    for (KTKLesson *lesson in wanted) {
        dispatch_semaphore_wait(slots, DISPATCH_TIME_FOREVER);
        dispatch_group_async(group, queue, ^{
            NSArray *fields = @[@[@"weekStart", weekStart], @[@"journalId", @(lesson.journalId)]];
            NSMutableDictionary *entry = [NSMutableDictionary dictionary];
            if (lesson.hasHomework) {
                id value = [self call:KTKFnStudentHomework fields:fields jar:nil error:NULL];
                if (KTKDict(value) && !KTKServerError(value)) {
                    entry[@"homework"] = value;
                }
            }
            if (lesson.remote) {
                id value = [self call:KTKFnStudentWebinar fields:fields jar:nil error:NULL];
                if (KTKDict(value) && !KTKServerError(value)) {
                    entry[@"webinar"] = value;
                }
            }
            if (entry.count) {
                @synchronized (extras) {
                    extras[[NSString stringWithFormat:@"%lld", lesson.journalId]] = entry;
                }
            }
            dispatch_semaphore_signal(slots);
        });
    }
    dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
    return extras;
}

- (void)loadWeek:(NSString *)weekStart completion:(void (^)(KTKWeek *, NSError *))completion {
    KTKSession *session = self.session;
    if (!session) {
        completion(nil, KTKMakeError(KTKErrorLoginRequired, nil));
        return;
    }
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *error = nil;
        NSDictionary *payload = [self fetchWeek:weekStart session:session error:&error];
        KTKWeek *week = nil;
        if (payload) {
            week = [KTKWeek weekWithPayload:payload filter:YES];
            if (week) {
                [self saveWeekPayload:payload weekStart:weekStart];
            } else {
                error = KTKMakeError(KTKErrorBadResponse, nil);
            }
        }
        if (!week && error.code != KTKErrorLoginRequired) {
            week = [self cachedWeek:weekStart];
            week.offline = YES;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (error.code == KTKErrorLoginRequired && self.session == session) {
                [self signOutLocally];
            }
            completion(week, error);
        });
    });
}

#pragma mark - files

- (void)downloadFile:(KTKFile *)file completion:(void (^)(NSString *, NSError *))completion {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSString *name = [[file.fileName componentsSeparatedByCharactersInSet:
                           [NSCharacterSet characterSetWithCharactersInString:@"/:\\"]] componentsJoinedByString:@"_"];
        while ([name hasPrefix:@"."]) {
            name = [name substringFromIndex:1];
        }
        if (name.length > 120) {
            name = [name substringFromIndex:name.length - 120];
        }
        if (!name.length) {
            name = [NSString stringWithFormat:@"file_%lld", file.fileId];
        }
        NSString *directory = [KTKCachesPath(@"ktk-files") stringByAppendingPathComponent:
                               [NSString stringWithFormat:@"%lld", file.fileId]];
        NSString *path = [directory stringByAppendingPathComponent:name];
        NSFileManager *fm = [NSFileManager defaultManager];
        NSError *error = nil;
        if (![fm fileExistsAtPath:path]) {
            [fm createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:NULL];
            NSString *partial = [path stringByAppendingString:@".part"];
            KTKHTTPRequest *request = [KTKHTTPRequest new];
            request.URL = [NSURL URLWithString:[NSString stringWithFormat:
                @"%@/api/timetable/students/self/lessons/%lld/files/%lld?weekStart=%@",
                KTKBase, file.journalId, file.fileId, [KTKHTTP escape:file.weekStart ?: @""]]];
            NSMutableDictionary *jar;
            @synchronized (self) {
                jar = self->_jar;
            }
            NSMutableDictionary *headers = [self headersWithJar:jar];
            [headers removeObjectForKey:@"Origin"];
            headers[@"Accept"] = @"*/*";
            request.headers = headers;
            request.limit = KTKFileLimit;
            request.timeout = 600;
            request.outputPath = partial;
            KTKHTTPResponse *response = [self send:request idempotent:NO error:&error];
            if (response && response.status != 200) {
                error = (response.status == 401 || response.status == 403)
                    ? KTKMakeError(KTKErrorLoginRequired, nil) : KTKStatusError(response.status);
            }
            if (!error) {
                [fm removeItemAtPath:path error:NULL];
                if (![fm moveItemAtPath:partial toPath:path error:NULL]) {
                    error = KTKMakeError(KTKErrorFile, @"Не удалось сохранить файл.");
                }
            }
            [fm removeItemAtPath:partial error:NULL];
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            completion(error ? nil : path, error);
        });
    });
}

#pragma mark - discovery

/// The page and its scripts name every server function; classify each id by the code around
/// it, exactly like the bot's Candidates::add. One crawl per launch serves every call.
- (NSString *)rediscover:(KTKServerFn)fn {
    if (fn == KTKFnStudentHomework || fn == KTKFnStudentWebinar) {
        return nil; // not told apart by discovery yet: the known ids are used
    }
    [_crawlLock lock];
    if (!_discovered) {
        _discovered = [self crawl];
    }
    NSDictionary *found = _discovered;
    [_crawlLock unlock];
    return found[KTKFnName(fn)];
}

- (NSString *)fetchText:(NSURL *)url finalURL:(NSURL **)finalURL {
    NSURL *current = url;
    for (int hop = 0; hop < 5; hop++) {
        KTKHTTPRequest *request = [KTKHTTPRequest new];
        request.URL = current;
        NSMutableDictionary *headers = [self headersWithJar:nil];
        [headers removeObjectForKey:@"Origin"];
        headers[@"Accept"] = @"application/json,text/html,*/*";
        request.headers = headers;
        request.limit = KTKJSONLimit;
        KTKHTTPResponse *response = [self send:request idempotent:YES error:NULL];
        if (!response) {
            return nil;
        }
        if (response.status >= 300 && response.status < 400 && response.location) {
            NSURL *next = [[NSURL URLWithString:response.location relativeToURL:current] absoluteURL];
            if (![next.scheme isEqualToString:@"https"] || ![next.host isEqualToString:url.host]) {
                return nil; // same-origin redirects only
            }
            current = next;
            continue;
        }
        if (response.status != 200) {
            return nil;
        }
        if (finalURL) {
            *finalURL = current;
        }
        return [[NSString alloc] initWithData:response.body encoding:NSUTF8StringEncoding];
    }
    return nil;
}

- (NSArray *)scriptURLs:(NSString *)text base:(NSURL *)base {
    static NSArray *patterns;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        patterns = @[
            [NSRegularExpression regularExpressionWithPattern:@"<script[^>]+src=[\"']([^\"']+)[\"']"
                                                      options:NSRegularExpressionCaseInsensitive | NSRegularExpressionDotMatchesLineSeparators
                                                        error:NULL],
            [NSRegularExpression regularExpressionWithPattern:@"[\"']([^\"']+\\.js(?:\\?[^\"']*)?)[\"']" options:0 error:NULL],
        ];
    });
    NSMutableArray *urls = [NSMutableArray array];
    NSMutableSet *seen = [NSMutableSet set];
    for (NSRegularExpression *pattern in patterns) {
        for (NSTextCheckingResult *match in [pattern matchesInString:text options:0 range:NSMakeRange(0, text.length)]) {
            NSString *raw = [text substringWithRange:[match rangeAtIndex:1]];
            NSURL *url = [[NSURL URLWithString:raw relativeToURL:base] absoluteURL];
            if (url && [url.scheme isEqualToString:@"https"] && [url.host isEqualToString:base.host] &&
                ![seen containsObject:url.absoluteString] && urls.count < 128) {
                [seen addObject:url.absoluteString];
                [urls addObject:url];
            }
        }
    }
    return urls;
}

- (void)scan:(NSString *)text into:(NSMutableDictionary *)found {
    static NSRegularExpression *serverFn;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        serverFn = [NSRegularExpression regularExpressionWithPattern:@"/_serverFn/([0-9a-fA-F]{32,64})" options:0 error:NULL];
    });
    NSString *marker = @"/_serverFn/";
    NSUInteger length = text.length;
    for (NSTextCheckingResult *match in [serverFn matchesInString:text options:0 range:NSMakeRange(0, length)]) {
        NSString *path = [text substringWithRange:match.range];
        NSUInteger index = match.range.location;
        NSUInteger after = NSMaxRange(match.range);
        NSRange previous = [text rangeOfString:marker options:NSBackwardsSearch range:NSMakeRange(0, index)];
        NSUInteger previousFn = previous.location == NSNotFound ? 0 : NSMaxRange(previous);
        NSRange next = [text rangeOfString:marker options:0 range:NSMakeRange(after, length - after)];
        NSUInteger nextFn = next.location == NSNotFound ? length : next.location;

        NSRange beforeRange = NSMakeRange(previousFn, index - previousFn);
        NSRange semicolon = [text rangeOfString:@";" options:NSBackwardsSearch range:beforeRange];
        if (semicolon.location == NSNotFound) {
            semicolon = [text rangeOfString:@"\n" options:NSBackwardsSearch range:beforeRange];
        }
        NSUInteger start = semicolon.location == NSNotFound ? previousFn : semicolon.location + 1;

        NSRange afterRange = NSMakeRange(after, nextFn - after);
        NSRange end = [text rangeOfString:@";" options:0 range:afterRange];
        if (end.location == NSNotFound) {
            end = [text rangeOfString:@"createServerFn" options:0 range:afterRange];
        }
        NSUInteger stop = end.location == NSNotFound ? nextFn : end.location;
        NSString *snippet = start <= stop ? [text substringWithRange:NSMakeRange(start, stop - start)] : path;

        BOOL (^has)(NSString *) = ^BOOL(NSString *word) {
            return [snippet rangeOfString:word].location != NSNotFound;
        };
        NSString *name = nil;
        if (has(@"collegeId") || has(@"fingerprint")) {
            name = KTKFnName(KTKFnSignIn);
        } else if (has(@"groupId") && has(@"weekStart")) {
            name = KTKFnName(KTKFnGroup);
        } else if (has(@"weekStart")) {
            name = (has(@"teacher") || has(@"Teacher")) ? KTKFnName(KTKFnTeacher) : KTKFnName(KTKFnStudent);
        } else if (has(@"revoke") || has(@"logout") || has(@"signOut") || has(@"sign-out")) {
            name = KTKFnName(KTKFnLogout);
        }
        if (name && !found[name]) {
            found[name] = path;
        }
    }
}

- (NSDictionary *)crawl {
    NSMutableDictionary *found = [NSMutableDictionary dictionary];
    NSURL *root = [NSURL URLWithString:[KTKBase stringByAppendingString:@"/"]];
    NSURL *page = root;
    NSString *html = [self fetchText:root finalURL:&page];
    if (!html) {
        return found;
    }
    [self scan:html into:found];
    NSMutableArray *queue = [[self scriptURLs:html base:page] mutableCopy];
    NSMutableSet *seen = [NSMutableSet set];
    while (queue.count && seen.count < KTKMaxScripts) {
        NSURL *url = queue[0];
        [queue removeObjectAtIndex:0];
        if ([seen containsObject:url.absoluteString]) {
            continue;
        }
        [seen addObject:url.absoluteString];
        NSString *script = [self fetchText:url finalURL:NULL];
        if (!script) {
            continue;
        }
        [self scan:script into:found];
        for (NSURL *next in [self scriptURLs:script base:url]) {
            if (![seen containsObject:next.absoluteString] && queue.count < 256) {
                [queue addObject:next];
            }
        }
    }
    return found;
}

@end
