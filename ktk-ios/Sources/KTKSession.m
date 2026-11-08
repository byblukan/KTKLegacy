#import "KTKSession.h"
#import <Security/Security.h>

static NSString *const KTKService = @"ru.ktkschedule.session";

static NSMutableDictionary *KTKQuery(void) {
    return [@{
        (__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: KTKService,
        (__bridge id)kSecAttrAccount: @"session",
    } mutableCopy];
}

static NSString *KTKFallbackPath(void) {
    NSString *support = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES)[0];
    return [support stringByAppendingPathComponent:@"session.json"];
}

@implementation KTKSession

- (NSDictionary *)dictionary {
    return @{
        @"cookies": self.cookies ?: @{},
        @"sessionId": self.sessionId ?: @"",
        @"login": self.login ?: @"",
        @"student": @(self.student),
        @"group": @(self.group),
        @"teacherHash": self.teacherHash ?: @"",
        @"branch": self.branch ?: @"",
    };
}

+ (instancetype)sessionWithData:(NSData *)data {
    NSDictionary *d = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    if (![d isKindOfClass:[NSDictionary class]] || ![d[@"cookies"] isKindOfClass:[NSDictionary class]] ||
        ![d[@"cookies"] count]) {
        return nil;
    }
    KTKSession *session = [KTKSession new];
    session.cookies = d[@"cookies"];
    session.sessionId = [d[@"sessionId"] isKindOfClass:[NSString class]] ? d[@"sessionId"] : @"";
    session.login = [d[@"login"] isKindOfClass:[NSString class]] ? d[@"login"] : @"";
    session.student = [d[@"student"] isKindOfClass:[NSNumber class]] ? [d[@"student"] boolValue] : YES;
    session.group = [d[@"group"] isKindOfClass:[NSNumber class]] ? [d[@"group"] longLongValue] : 0;
    session.teacherHash = [d[@"teacherHash"] isKindOfClass:[NSString class]] ? d[@"teacherHash"] : @"";
    session.branch = [d[@"branch"] isKindOfClass:[NSString class]] ? d[@"branch"] : @"";
    return session;
}

+ (instancetype)load {
    NSMutableDictionary *query = KTKQuery();
    query[(__bridge id)kSecReturnData] = @YES;
    query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
    CFTypeRef result = NULL;
    if (SecItemCopyMatching((__bridge CFDictionaryRef)query, &result) == errSecSuccess && result) {
        KTKSession *session = [self sessionWithData:(__bridge_transfer NSData *)result];
        if (session) {
            return session;
        }
    }
    return [self sessionWithData:[NSData dataWithContentsOfFile:KTKFallbackPath()]];
}

- (BOOL)save {
    NSData *data = [NSJSONSerialization dataWithJSONObject:[self dictionary] options:0 error:NULL];
    if (!data) {
        return NO;
    }
    NSMutableDictionary *query = KTKQuery();
    SecItemDelete((__bridge CFDictionaryRef)query);
    query[(__bridge id)kSecValueData] = data;
    query[(__bridge id)kSecAttrAccessible] = (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
    if (SecItemAdd((__bridge CFDictionaryRef)query, NULL) == errSecSuccess) {
        [[NSFileManager defaultManager] removeItemAtPath:KTKFallbackPath() error:NULL];
        return YES;
    }
    NSString *path = KTKFallbackPath();
    [[NSFileManager defaultManager] createDirectoryAtPath:[path stringByDeletingLastPathComponent]
                              withIntermediateDirectories:YES attributes:nil error:NULL];
    return [data writeToFile:path
                     options:NSDataWritingAtomic | NSDataWritingFileProtectionCompleteUntilFirstUserAuthentication
                       error:NULL];
}

+ (void)clear {
    SecItemDelete((__bridge CFDictionaryRef)KTKQuery());
    [[NSFileManager defaultManager] removeItemAtPath:KTKFallbackPath() error:NULL];
}

@end
