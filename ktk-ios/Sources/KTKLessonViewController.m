#import "KTKLessonViewController.h"
#import "KTKCalendar.h"
#import "KTKClient.h"
#import <QuickLook/QuickLook.h>

static const CGFloat KTKLabelWidth = 96;

/// One row: a caption on the left (nil = the text spans the row), the text, and what a tap does.
@interface KTKRow : NSObject
@property (nonatomic, copy) NSString *label;
@property (nonatomic, copy) NSString *text;
@property (nonatomic) BOOL bold;
@property (nonatomic, copy) NSString *link;
@property (nonatomic, strong) KTKFile *file;
@end

@implementation KTKRow
+ (instancetype)label:(NSString *)label text:(NSString *)text {
    KTKRow *row = [KTKRow new];
    row.label = label;
    row.text = text;
    return row;
}
@end

@interface KTKInfoCell : UITableViewCell
@end

@implementation KTKInfoCell
- (void)layoutSubviews {
    [super layoutSubviews];
    CGRect content = self.contentView.bounds;
    CGFloat right = self.accessoryView || self.accessoryType != UITableViewCellAccessoryNone ? 4 : 10;
    if (self.textLabel.text.length) {
        self.textLabel.frame = CGRectMake(10, 11, KTKLabelWidth - 14, 22);
        self.detailTextLabel.frame = CGRectMake(KTKLabelWidth, 11, content.size.width - KTKLabelWidth - right,
                                                content.size.height - 22);
    } else {
        self.detailTextLabel.frame = CGRectMake(10, 11, content.size.width - 10 - right, content.size.height - 22);
    }
}
@end

@interface KTKLessonViewController () <QLPreviewControllerDataSource, UIActionSheetDelegate>
@end

@implementation KTKLessonViewController {
    KTKLesson *_lesson;
    BOOL _teacher;
    NSArray *_sections; // @[title or NSNull, @[KTKRow]]
    NSMutableSet *_downloading; // file ids
    NSURL *_preview;
    UIDocumentInteractionController *_documents;
    NSString *_sheetLink;
}

- (instancetype)initWithLesson:(KTKLesson *)lesson teacher:(BOOL)teacher {
    if ((self = [super initWithStyle:UITableViewStyleGrouped])) {
        _lesson = lesson;
        _teacher = teacher;
        _downloading = [NSMutableSet set];
        self.title = [NSString stringWithFormat:@"%ld пара", (long)lesson.pair];
        [self buildSections];
    }
    return self;
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self.navigationController setToolbarHidden:YES animated:animated];
}

- (void)buildSections {
    KTKLesson *l = _lesson;
    NSMutableArray *sections = [NSMutableArray array];

    KTKRow *title = [KTKRow label:nil text:l.disciplineFull ?: l.discipline];
    title.bold = YES;
    NSMutableArray *main = [NSMutableArray arrayWithObject:title];
    NSString *when = [KTKCalendar dayTitle:l.date];
    if (l.timeText) {
        when = [NSString stringWithFormat:@"%@\n%@", l.timeText, when];
    }
    [main addObject:[KTKRow label:@"Когда" text:when]];
    if (l.remote) {
        [main addObject:[KTKRow label:@"Кабинет" text:@"Дистанционно"]];
    } else if (l.hallText) {
        [main addObject:[KTKRow label:@"Кабинет" text:l.hallText]];
    }
    if (_teacher) {
        if (l.groupId.length) {
            [main addObject:[KTKRow label:@"Группа" text:l.groupId]];
        }
    } else if (l.teacher.length) {
        [main addObject:[KTKRow label:@"Препод." text:l.teacher]];
    }
    if (l.pairTypeName.length) {
        [main addObject:[KTKRow label:@"Тип" text:l.pairTypeName]];
    }
    if (l.subgroupText) {
        [main addObject:[KTKRow label:@"Подгруппа" text:l.subgroupText]];
    }
    [sections addObject:@[[NSNull null], main]];

    if (!_teacher && l.hasGrade) {
        NSMutableArray *rows = [NSMutableArray array];
        if (l.gradeText) {
            [rows addObject:[KTKRow label:@"Оценка" text:l.gradeText]];
        }
        if (l.markText) {
            [rows addObject:[KTKRow label:@"Отметка" text:l.markText]];
        }
        [sections addObject:@[@"Оценка", rows]];
    }

    if (l.task.length || l.deadline.length || l.hasHomework) {
        NSMutableArray *rows = [NSMutableArray array];
        if (l.task.length) {
            [rows addObject:[KTKRow label:nil text:l.task]];
        }
        if (l.deadline.length) {
            [rows addObject:[KTKRow label:@"Срок" text:[KTKCalendar deadlineText:l.deadline]]];
        }
        if (!rows.count && !l.files.count) {
            [rows addObject:[KTKRow label:nil text:@"Задание есть, но не загрузилось. Обновите неделю."]];
        }
        if (rows.count) {
            [sections addObject:@[@"Задание", rows]];
        }
    }

    if (l.webinar.length) {
        KTKRow *row = [KTKRow label:nil text:l.webinar];
        row.link = l.webinar;
        [sections addObject:@[@"Вебинар", @[row]]];
    }

    if (l.files.count) {
        NSMutableArray *rows = [NSMutableArray array];
        for (KTKFile *file in l.files) {
            KTKRow *row = [KTKRow label:nil text:[NSString stringWithFormat:@"📎 %@", file.fileName]];
            row.file = file;
            [rows addObject:row];
        }
        [sections addObject:@[@"Файлы", rows]];
    }
    _sections = sections;
}

- (KTKRow *)rowAt:(NSIndexPath *)indexPath {
    return _sections[indexPath.section][1][indexPath.row];
}

- (UIFont *)fontForRow:(KTKRow *)row {
    return row.bold ? [UIFont boldSystemFontOfSize:17] : [UIFont systemFontOfSize:15];
}

- (CGFloat)textWidthForRow:(KTKRow *)row {
    CGFloat margins = UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad ? 90 : 20;
    CGFloat content = self.tableView.bounds.size.width - margins;
    CGFloat accessory = (row.file || row.link) ? 30 : 0;
    return content - (row.label ? KTKLabelWidth : 10) - 10 - accessory;
}

#pragma mark - table

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return _sections.count;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return [_sections[section][1] count];
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    id title = _sections[section][0];
    return [title isKindOfClass:[NSString class]] ? title : nil;
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    id title = _sections[section][0];
    if ([title isEqual:@"Вебинар"]) {
        return @"Safari на iOS 6 может не открыть современный сайт: ссылку можно скопировать.";
    }
    return nil;
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath {
    KTKRow *row = [self rowAt:indexPath];
    CGSize size = [row.text sizeWithFont:[self fontForRow:row]
                       constrainedToSize:CGSizeMake([self textWidthForRow:row], 10000)
                           lineBreakMode:NSLineBreakByWordWrapping];
    return MAX(44, ceil(size.height) + 24);
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    KTKRow *row = [self rowAt:indexPath];
    KTKInfoCell *cell = [tableView dequeueReusableCellWithIdentifier:@"info"];
    if (!cell) {
        cell = [[KTKInfoCell alloc] initWithStyle:UITableViewCellStyleValue2 reuseIdentifier:@"info"];
        cell.detailTextLabel.numberOfLines = 0;
        cell.detailTextLabel.lineBreakMode = NSLineBreakByWordWrapping;
        cell.textLabel.font = [UIFont boldSystemFontOfSize:13];
    }
    cell.textLabel.text = row.label;
    cell.detailTextLabel.text = row.text;
    cell.detailTextLabel.font = [self fontForRow:row];
    BOOL tappable = row.file || row.link;
    cell.detailTextLabel.textColor = tappable ? [UIColor colorWithRed:0.2 green:0.31 blue:0.52 alpha:1] : [UIColor blackColor];
    cell.selectionStyle = tappable ? UITableViewCellSelectionStyleBlue : UITableViewCellSelectionStyleNone;
    cell.accessoryType = UITableViewCellAccessoryNone;
    cell.accessoryView = nil;
    if (row.file && [_downloading containsObject:@(row.file.fileId)]) {
        UIActivityIndicatorView *spinner = [[UIActivityIndicatorView alloc]
            initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleGray];
        [spinner startAnimating];
        cell.accessoryView = spinner;
    } else if (tappable) {
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    }
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    KTKRow *row = [self rowAt:indexPath];
    if (row.link) {
        _sheetLink = row.link;
        UIActionSheet *sheet = [[UIActionSheet alloc] initWithTitle:row.link delegate:self cancelButtonTitle:@"Отмена"
                                             destructiveButtonTitle:nil otherButtonTitles:@"Открыть в Safari", @"Скопировать ссылку", nil];
        [sheet showInView:self.view];
    } else if (row.file) {
        [self openFile:row.file indexPath:indexPath];
    }
}

- (void)actionSheet:(UIActionSheet *)actionSheet clickedButtonAtIndex:(NSInteger)buttonIndex {
    if (buttonIndex == actionSheet.cancelButtonIndex || !_sheetLink) {
        return;
    }
    if (buttonIndex == actionSheet.firstOtherButtonIndex) {
        NSURL *url = [NSURL URLWithString:_sheetLink];
        if (!url || ![[UIApplication sharedApplication] openURL:url]) {
            [self alert:@"Не удалось открыть ссылку."];
        }
    } else {
        [UIPasteboard generalPasteboard].string = _sheetLink;
    }
}

#pragma mark - files

- (void)openFile:(KTKFile *)file indexPath:(NSIndexPath *)indexPath {
    NSNumber *key = @(file.fileId);
    if ([_downloading containsObject:key]) {
        return;
    }
    [_downloading addObject:key];
    [self.tableView reloadRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationNone];
    [[KTKClient shared] downloadFile:file completion:^(NSString *path, NSError *error) {
        [self->_downloading removeObject:key];
        [self.tableView reloadRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationNone];
        if (!path) {
            [self alert:KTKErrorText(error)];
            return;
        }
        [self present:[NSURL fileURLWithPath:path] fromRow:indexPath];
    }];
}

- (void)present:(NSURL *)url fromRow:(NSIndexPath *)indexPath {
    _preview = url;
    if ([QLPreviewController canPreviewItem:(id<QLPreviewItem>)url]) {
        QLPreviewController *preview = [[QLPreviewController alloc] init];
        preview.dataSource = self;
        [self presentViewController:preview animated:YES completion:nil];
        return;
    }
    _documents = [UIDocumentInteractionController interactionControllerWithURL:url];
    CGRect rect = [self.tableView rectForRowAtIndexPath:indexPath];
    if (![_documents presentOpenInMenuFromRect:rect inView:self.tableView animated:YES]) {
        [self alert:@"Файл скачан, но на устройстве нет приложения, которое его откроет."];
    }
}

- (NSInteger)numberOfPreviewItemsInPreviewController:(QLPreviewController *)controller {
    return _preview ? 1 : 0;
}

- (id<QLPreviewItem>)previewController:(QLPreviewController *)controller previewItemAtIndex:(NSInteger)index {
    return (id<QLPreviewItem>)_preview;
}

- (void)alert:(NSString *)message {
    [[[UIAlertView alloc] initWithTitle:nil message:message delegate:nil
                      cancelButtonTitle:@"OK" otherButtonTitles:nil] show];
}

@end
