#import "KTKLoginViewController.h"
#import "KTKClient.h"

@interface KTKLoginViewController () <UITextFieldDelegate>
@end

@implementation KTKLoginViewController {
    UITextField *_login;
    UITextField *_password;
    UIActivityIndicatorView *_spinner;
    BOOL _busy;
}

- (instancetype)init {
    return [super initWithStyle:UITableViewStyleGrouped];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Вход";
    _login = [self fieldWithPlaceholder:@"Логин с сайта"];
    _login.keyboardType = UIKeyboardTypeASCIICapable;
    _login.returnKeyType = UIReturnKeyNext;
    _password = [self fieldWithPlaceholder:@"Пароль"];
    _password.secureTextEntry = YES;
    _password.returnKeyType = UIReturnKeyGo;
    _spinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleWhite];
    _spinner.hidesWhenStopped = YES;
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithCustomView:_spinner];
}

- (UITextField *)fieldWithPlaceholder:(NSString *)placeholder {
    UITextField *field = [[UITextField alloc] initWithFrame:CGRectZero];
    field.placeholder = placeholder;
    field.autocapitalizationType = UITextAutocapitalizationTypeNone;
    field.autocorrectionType = UITextAutocorrectionTypeNo;
    field.clearButtonMode = UITextFieldViewModeWhileEditing;
    field.contentVerticalAlignment = UIControlContentVerticalAlignmentCenter;
    field.font = [UIFont systemFontOfSize:17];
    field.delegate = self;
    return field;
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if (!_login.text.length) {
        [_login becomeFirstResponder];
    }
}

#pragma mark - table

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return 2;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return section == 0 ? 2 : 1;
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    return section == 0 ? @"ktk.education-system.ru" : nil;
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    return section == 1
        ? @"Логин и пароль те же, что на сайте колледжа. Пароль отправляется только на сайт и не сохраняется: приложение хранит лишь сессию."
        : nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    if (indexPath.section == 1) {
        UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
        cell.textLabel.text = _busy ? @"Вход…" : @"Войти";
        cell.textLabel.textAlignment = NSTextAlignmentCenter;
        cell.textLabel.textColor = _busy ? [UIColor grayColor] : [UIColor colorWithRed:0.2 green:0.31 blue:0.52 alpha:1];
        cell.selectionStyle = _busy ? UITableViewCellSelectionStyleNone : UITableViewCellSelectionStyleBlue;
        return cell;
    }
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    UITextField *field = indexPath.row == 0 ? _login : _password;
    CGRect bounds = cell.contentView.bounds;
    field.frame = CGRectMake(10, 0, bounds.size.width - 20, bounds.size.height);
    field.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [field removeFromSuperview];
    [cell.contentView addSubview:field];
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    if (indexPath.section == 1) {
        [self signIn];
    } else {
        [(indexPath.row == 0 ? _login : _password) becomeFirstResponder];
    }
}

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
    if (textField == _login) {
        [_password becomeFirstResponder];
    } else {
        [self signIn];
    }
    return NO;
}

#pragma mark - sign in

- (void)signIn {
    if (_busy) {
        return;
    }
    if (!_login.text.length || !_password.text.length) {
        [self alert:@"Введите логин и пароль."];
        return;
    }
    _busy = YES;
    [self.view endEditing:YES];
    [_spinner startAnimating];
    [self.tableView reloadSections:[NSIndexSet indexSetWithIndex:1] withRowAnimation:UITableViewRowAnimationNone];
    NSString *password = _password.text;
    _password.text = @"";
    [[KTKClient shared] signInWithLogin:_login.text password:password completion:^(NSError *error) {
        self->_busy = NO;
        [self->_spinner stopAnimating];
        if (error) {
            [self.tableView reloadSections:[NSIndexSet indexSetWithIndex:1] withRowAnimation:UITableViewRowAnimationNone];
            [self alert:KTKErrorText(error)];
        }
        // On success the app delegate swaps the root controller.
    }];
}

- (void)alert:(NSString *)message {
    [[[UIAlertView alloc] initWithTitle:nil message:message delegate:nil
                      cancelButtonTitle:@"OK" otherButtonTitles:nil] show];
}

@end
