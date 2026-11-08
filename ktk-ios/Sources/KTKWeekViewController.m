#import "KTKWeekViewController.h"
#import "KTKCalendar.h"
#import "KTKClient.h"
#import "KTKLessonViewController.h"

static UIColor *KTKBadgeColor(NSString *badge) {
    switch ([badge integerValue]) {
        case 5: return [UIColor colorWithRed:0.16 green:0.55 blue:0.2 alpha:1];
        case 4: return [UIColor colorWithRed:0.15 green:0.4 blue:0.75 alpha:1];
        case 3: return [UIColor colorWithRed:0.85 green:0.5 blue:0.08 alpha:1];
        case 2: case 1: return [UIColor colorWithRed:0.8 green:0.15 blue:0.15 alpha:1];
    }
    if ([badge isEqualToString:@"Н"]) {
        return [UIColor colorWithRed:0.8 green:0.15 blue:0.15 alpha:1];
    }
    return [UIColor darkGrayColor];
}

#pragma mark - lesson cell

@interface KTKLessonCell : UITableViewCell
+ (CGFloat)heightForLesson:(KTKLesson *)lesson teacher:(BOOL)teacher tableWidth:(CGFloat)tableWidth;
- (void)showLesson:(KTKLesson *)lesson teacher:(BOOL)teacher;
@end

@implementation KTKLessonCell

+ (NSArray *)detailLinesForLesson:(KTKLesson *)lesson teacher:(BOOL)teacher {
    NSMutableArray *lines = [NSMutableArray array];
    NSMutableArray *first = [NSMutableArray array];
    if (lesson.timeText) {
        [first addObject:lesson.timeText];
    }
    if (lesson.remote) {
        [first addObject:@"дистанционно"];
    } else if (lesson.hallText) {
        [first addObject:[@"каб. " stringByAppendingString:lesson.hallText]];
    }
    if (lesson.subgroupText) {
        [first addObject:lesson.subgroupText];
    }
    if (first.count) {
        [lines addObject:[first componentsJoinedByString:@" · "]];
    }
    NSString *who = teacher ? (lesson.groupId.length ? [@"Группа " stringByAppendingString:lesson.groupId] : nil)
                            : lesson.shortTeacher;
    if (who) {
        [lines addObject:who];
    }
    NSMutableArray *extras = [NSMutableArray array];
    if (lesson.task.length || lesson.hasHomework) {
        [extras addObject:@"📝 задание"];
    }
    if (lesson.files.count) {
        [extras addObject:[NSString stringWithFormat:@"📎 %lu", (unsigned long)lesson.files.count]];
    }
    if (lesson.webinar.length) {
        [extras addObject:@"🎥 вебинар"];
    }
    if (extras.count) {
        [lines addObject:[extras componentsJoinedByString:@"  "]];
    }
    return lines;
}

/// The details wrap when a line is too long for the cell (the grade badge makes it narrower),
/// so the height comes from the text laid out at the detail label's width.
+ (CGFloat)heightForLesson:(KTKLesson *)lesson teacher:(BOOL)teacher tableWidth:(CGFloat)tableWidth {
    NSString *details = [[self detailLinesForLesson:lesson teacher:teacher] componentsJoinedByString:@"\n"];
    if (!details.length) {
        return 44;
    }
    // Grouped cell margins, then the accessory (badge or disclosure indicator) with its padding,
    // then the label's own 10 pt on each side. The reserve is generous, so the text is never cut.
    CGFloat margins = UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad ? 90 : 20;
    BOOL badge = !teacher && lesson.badgeText;
    CGFloat accessory = badge ? 36 + 24 : 40;
    CGFloat width = tableWidth - margins - accessory - 20;
    CGSize size = [details sizeWithFont:[UIFont systemFontOfSize:13]
                      constrainedToSize:CGSizeMake(MAX(width, 100), 10000)
                          lineBreakMode:NSLineBreakByWordWrapping];
    return MAX(44, 36 + ceil(size.height));
}

- (instancetype)initWithStyle:(UITableViewCellStyle)style reuseIdentifier:(NSString *)reuseIdentifier {
    if ((self = [super initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:reuseIdentifier])) {
        self.textLabel.font = [UIFont boldSystemFontOfSize:16];
        self.detailTextLabel.font = [UIFont systemFontOfSize:13];
        self.detailTextLabel.textColor = [UIColor darkGrayColor];
        self.detailTextLabel.numberOfLines = 0;
    }
    return self;
}

- (void)showLesson:(KTKLesson *)lesson teacher:(BOOL)teacher {
    self.textLabel.text = [NSString stringWithFormat:@"%ld. %@", (long)lesson.pair, lesson.discipline];
    self.detailTextLabel.text = [[KTKLessonCell detailLinesForLesson:lesson teacher:teacher] componentsJoinedByString:@"\n"];
    NSString *badge = teacher ? nil : lesson.badgeText;
    if (badge) {
        UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, 36, 36)];
        label.text = badge;
        label.font = [UIFont boldSystemFontOfSize:badge.length > 2 ? 15 : 22];
        label.textAlignment = NSTextAlignmentCenter;
        label.textColor = KTKBadgeColor(badge);
        label.backgroundColor = [UIColor clearColor];
        self.accessoryView = label;
    } else {
        self.accessoryView = nil;
        self.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    }
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect content = self.contentView.bounds;
    CGFloat width = content.size.width - 20;
    self.textLabel.frame = CGRectMake(10, 7, width, 21);
    self.detailTextLabel.frame = CGRectMake(10, 30, width, MAX(content.size.height - 36, 0));
}

@end

#pragma mark - week screen

@interface KTKWeekViewController () <UIActionSheetDelegate>
@end

@implementation KTKWeekViewController {
    NSString *_weekStart;
    KTKWeek *_week;
    NSError *_error;
    BOOL _loading;
    BOOL _gradesMode;
    BOOL _scrolledToToday;
    NSUInteger _token;
    NSArray *_sections; // @{@"day": KTKDay, @"lessons": NSArray}
    UISegmentedControl *_mode;
    UIActivityIndicatorView *_spinner;
}

- (instancetype)init {
    return [super initWithStyle:UITableViewStyleGrouped];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (UIBarButtonItem *)flexibleSpace {
    return [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.refreshControl = [[UIRefreshControl alloc] init];
    [self.refreshControl addTarget:self action:@selector(reload) forControlEvents:UIControlEventValueChanged];

    _spinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleWhite];
    _spinner.hidesWhenStopped = YES;
    self.navigationItem.leftBarButtonItem = [[UIBarButtonItem alloc] initWithCustomView:_spinner];
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Выйти"
        style:UIBarButtonItemStyleBordered target:self action:@selector(confirmSignOut)];

    _mode = [[UISegmentedControl alloc] initWithItems:@[@"Пары", @"Оценки"]];
    _mode.segmentedControlStyle = UISegmentedControlStyleBar;
    _mode.selectedSegmentIndex = 0;
    [_mode addTarget:self action:@selector(modeChanged) forControlEvents:UIControlEventValueChanged];
    self.toolbarItems = @[
        [[UIBarButtonItem alloc] initWithTitle:@"  ‹  " style:UIBarButtonItemStyleBordered target:self action:@selector(previousWeek)],
        [self flexibleSpace],
        [[UIBarButtonItem alloc] initWithTitle:@"Сейчас" style:UIBarButtonItemStyleBordered target:self action:@selector(currentWeek)],
        [self flexibleSpace],
        [[UIBarButtonItem alloc] initWithCustomView:_mode],
        [self flexibleSpace],
        [[UIBarButtonItem alloc] initWithTitle:@"  ›  " style:UIBarButtonItemStyleBordered target:self action:@selector(nextWeek)],
    ];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(willEnterForeground)
                                                 name:UIApplicationWillEnterForegroundNotification object:nil];
    [self showWeek:[KTKCalendar currentWeekStart]];
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self.navigationController setToolbarHidden:NO animated:animated];
}

- (void)willEnterForeground {
    // Back from the background after a while: the week may have changed.
    if (!_loading && (!_week.updatedAt || -[_week.updatedAt timeIntervalSinceNow] > 300)) {
        [self reload];
    }
}

#pragma mark - navigation

- (void)showWeek:(NSString *)weekStart {
    _weekStart = weekStart;
    _scrolledToToday = NO;
    self.title = [KTKCalendar weekTitle:weekStart];
    _week = [[KTKClient shared] cachedWeek:weekStart];
    _error = nil;
    [self rebuild];
    [self.tableView setContentOffset:CGPointMake(0, 0) animated:NO];
    [self reload];
}

- (void)previousWeek {
    [self showWeek:[KTKCalendar weekStart:_weekStart shiftedBy:-1]];
}

- (void)nextWeek {
    [self showWeek:[KTKCalendar weekStart:_weekStart shiftedBy:1]];
}

- (void)currentWeek {
    [self showWeek:[KTKCalendar currentWeekStart]];
}

- (void)modeChanged {
    _gradesMode = _mode.selectedSegmentIndex == 1;
    [self rebuild];
}

- (void)reload {
    NSUInteger token = ++_token;
    NSString *weekStart = _weekStart;
    _loading = YES;
    [_spinner startAnimating];
    if (!_week) {
        [self rebuild];
    }
    [[KTKClient shared] loadWeek:weekStart completion:^(KTKWeek *week, NSError *error) {
        if (token != self->_token) {
            return; // the user went to another week meanwhile
        }
        self->_loading = NO;
        [self->_spinner stopAnimating];
        [self.refreshControl endRefreshing];
        if (error.code == KTKErrorLoginRequired && [error.domain isEqualToString:KTKErrorDomain]) {
            [[[UIAlertView alloc] initWithTitle:nil message:KTKErrorText(error) delegate:nil
                              cancelButtonTitle:@"OK" otherButtonTitles:nil] show];
            return; // the client signed out; the app delegate shows the login screen
        }
        if (week) {
            self->_week = week;
        }
        self->_error = error;
        [self rebuild];
        [self scrollToTodayOnce];
    }];
}

- (void)scrollToTodayOnce {
    if (_scrolledToToday || _gradesMode || !_week) {
        return;
    }
    _scrolledToToday = YES;
    for (NSUInteger i = 0; i < _sections.count; i++) {
        KTKDay *day = _sections[i][@"day"];
        if ([KTKCalendar isToday:day.date] && i > 0) {
            [self.tableView scrollToRowAtIndexPath:[NSIndexPath indexPathForRow:0 inSection:i]
                                  atScrollPosition:UITableViewScrollPositionTop animated:NO];
            break;
        }
    }
}

#pragma mark - content

- (void)rebuild {
    NSMutableArray *sections = [NSMutableArray array];
    for (KTKDay *day in _week.days) {
        NSArray *lessons = day.lessons;
        if (_gradesMode) {
            lessons = [lessons filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(KTKLesson *lesson, NSDictionary *bindings) {
                return lesson.hasGrade;
            }]];
            if (!lessons.count) {
                continue;
            }
        } else if (!lessons.count && [KTKCalendar weekdayOfDate:day.date] == 6) {
            continue; // an empty Sunday
        }
        [sections addObject:@{@"day": day, @"lessons": lessons}];
    }
    _sections = sections;

    NSString *banner = nil;
    if (_week.offline && _error) {
        banner = _week.updatedAt
            ? [NSString stringWithFormat:@"%@\nПоказано сохранённое %@.", KTKErrorText(_error), [KTKCalendar shortDateTime:_week.updatedAt]]
            : KTKErrorText(_error);
    }
    self.tableView.tableHeaderView = banner ? [self bannerWithText:banner] : nil;
    [self.tableView reloadData];
}

- (UIView *)bannerWithText:(NSString *)text {
    CGFloat width = self.tableView.bounds.size.width;
    UIFont *font = [UIFont systemFontOfSize:13];
    CGSize size = [text sizeWithFont:font constrainedToSize:CGSizeMake(width - 40, 1000)
                       lineBreakMode:NSLineBreakByWordWrapping];
    UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, width, size.height + 24)];
    label.text = text;
    label.font = font;
    label.numberOfLines = 0;
    label.textAlignment = NSTextAlignmentCenter;
    label.textColor = [UIColor colorWithRed:0.6 green:0.15 blue:0.1 alpha:1];
    label.shadowColor = [UIColor whiteColor];
    label.shadowOffset = CGSizeMake(0, 1);
    label.backgroundColor = [UIColor clearColor];
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    return label;
}

- (NSString *)messageText {
    if (!_week) {
        return _loading ? @"Загрузка…" : (KTKErrorText(_error) ?: @"Нет данных.");
    }
    return _gradesMode ? @"На этой неделе оценок нет." : @"На этой неделе пар нет.";
}

- (KTKLesson *)lessonAt:(NSIndexPath *)indexPath {
    if (indexPath.section >= (NSInteger)_sections.count) {
        return nil;
    }
    NSArray *lessons = _sections[indexPath.section][@"lessons"];
    return indexPath.row < (NSInteger)lessons.count ? lessons[indexPath.row] : nil;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return _sections.count ?: 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    if (!_sections.count) {
        return 1;
    }
    return MAX([_sections[section][@"lessons"] count], (NSUInteger)1);
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    if (!_sections.count) {
        return nil;
    }
    KTKDay *day = _sections[section][@"day"];
    NSString *title = [KTKCalendar dayTitle:day.date];
    return [KTKCalendar isToday:day.date] ? [title stringByAppendingString:@" · сегодня"] : title;
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath {
    KTKLesson *lesson = [self lessonAt:indexPath];
    if (lesson) {
        return [KTKLessonCell heightForLesson:lesson teacher:_week.teacher tableWidth:tableView.bounds.size.width];
    }
    if (!_sections.count) {
        CGSize size = [[self messageText] sizeWithFont:[UIFont systemFontOfSize:15]
                                     constrainedToSize:CGSizeMake(tableView.bounds.size.width - 60, 1000)
                                         lineBreakMode:NSLineBreakByWordWrapping];
        return MAX(44, size.height + 24);
    }
    return 44;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    KTKLesson *lesson = [self lessonAt:indexPath];
    if (lesson) {
        KTKLessonCell *cell = [tableView dequeueReusableCellWithIdentifier:@"lesson"];
        if (!cell) {
            cell = [[KTKLessonCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:@"lesson"];
        }
        [cell showLesson:lesson teacher:_week.teacher];
        return cell;
    }
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"message"];
    if (!cell) {
        cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"message"];
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
        cell.textLabel.font = [UIFont systemFontOfSize:15];
        cell.textLabel.textColor = [UIColor grayColor];
        cell.textLabel.textAlignment = NSTextAlignmentCenter;
        cell.textLabel.numberOfLines = 0;
    }
    cell.textLabel.text = _sections.count ? @"Пар нет" : [self messageText];
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    KTKLesson *lesson = [self lessonAt:indexPath];
    if (!lesson) {
        return;
    }
    KTKLessonViewController *controller = [[KTKLessonViewController alloc] initWithLesson:lesson teacher:_week.teacher];
    [self.navigationController pushViewController:controller animated:YES];
}

#pragma mark - sign out

- (void)confirmSignOut {
    NSString *login = [KTKClient shared].session.login;
    UIActionSheet *sheet = [[UIActionSheet alloc]
        initWithTitle:login.length ? [NSString stringWithFormat:@"Выйти из аккаунта %@?", login] : @"Выйти из аккаунта?"
             delegate:self cancelButtonTitle:@"Отмена" destructiveButtonTitle:@"Выйти" otherButtonTitles:nil];
    [sheet showFromToolbar:self.navigationController.toolbar];
}

- (void)actionSheet:(UIActionSheet *)actionSheet clickedButtonAtIndex:(NSInteger)buttonIndex {
    if (buttonIndex == actionSheet.destructiveButtonIndex) {
        [[KTKClient shared] signOut];
    }
}

@end
