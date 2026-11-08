#import <UIKit/UIKit.h>
#import "KTKWeek.h"

/// One lesson: time, room, teacher, grade, task, webinar and files.
@interface KTKLessonViewController : UITableViewController
- (instancetype)initWithLesson:(KTKLesson *)lesson teacher:(BOOL)teacher;
@end
