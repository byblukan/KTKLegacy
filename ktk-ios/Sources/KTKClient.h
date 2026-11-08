#import <Foundation/Foundation.h>
#import "KTKHTTP.h"
#import "KTKSession.h"
#import "KTKWeek.h"

/// Posted on the main thread after sign-in and sign-out.
extern NSString *const KTKSessionDidChangeNotification;

/// User-facing text for any error of this app.
NSString *KTKErrorText(NSError *error);

/// ktk.education-system.ru. Completions run on the main thread.
@interface KTKClient : NSObject
+ (instancetype)shared;
@property (nonatomic, readonly) KTKSession *session;

- (void)signInWithLogin:(NSString *)login password:(NSString *)password
             completion:(void (^)(NSError *error))completion;

/// The week from the site, with tasks, webinars and files of the lessons that have them.
/// When the site cannot be reached, the cached copy comes back with `offline` set, plus the error.
/// KTKErrorLoginRequired signs out locally.
- (void)loadWeek:(NSString *)weekStart completion:(void (^)(KTKWeek *week, NSError *error))completion;
- (KTKWeek *)cachedWeek:(NSString *)weekStart;

- (void)downloadFile:(KTKFile *)file completion:(void (^)(NSString *path, NSError *error))completion;

/// Ends the session on the site (best effort) and forgets it and every cached week and file.
- (void)signOut;
@end
