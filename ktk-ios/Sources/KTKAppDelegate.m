#import "KTKAppDelegate.h"
#import "KTKClient.h"
#import "KTKLoginViewController.h"
#import "KTKWeekViewController.h"

@implementation KTKAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    UIColor *tint = [UIColor colorWithRed:0.13 green:0.27 blue:0.47 alpha:1];
    [[UINavigationBar appearance] setTintColor:tint];
    [[UIToolbar appearance] setTintColor:tint];

    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.backgroundColor = [UIColor whiteColor];
    [self showRoot];
    [self.window makeKeyAndVisible];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(showRoot)
                                                 name:KTKSessionDidChangeNotification object:nil];
    return YES;
}

- (void)showRoot {
    UIViewController *root = [KTKClient shared].session
        ? [[KTKWeekViewController alloc] init]
        : [[KTKLoginViewController alloc] init];
    self.window.rootViewController = [[UINavigationController alloc] initWithRootViewController:root];
}

@end
