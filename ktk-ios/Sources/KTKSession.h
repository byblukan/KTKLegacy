#import <Foundation/Foundation.h>

/// A signed-in site session. The password is never stored: only the cookies the site set.
/// Kept in the Keychain; if the Keychain refuses (missing entitlement on a fakesigned app),
/// in a file with data protection.
@interface KTKSession : NSObject
@property (nonatomic, copy) NSDictionary *cookies; // name -> value
@property (nonatomic, copy) NSString *sessionId;
@property (nonatomic, copy) NSString *login;
@property (nonatomic) BOOL student;
@property (nonatomic) long long group;
@property (nonatomic, copy) NSString *teacherHash;
@property (nonatomic, copy) NSString *branch;

+ (instancetype)load;
- (BOOL)save;
+ (void)clear;
@end
