// KTK for Windows Phone 8.1 - Login page.
using System.Windows;
using System.Windows.Input;
using System.Windows.Navigation;
using Microsoft.Phone.Controls;
using Microsoft.Phone.Shell;

namespace KTKLegacy
{
    public partial class LoginPage : PhoneApplicationPage
    {
        private bool _busy;

        public LoginPage()
        {
            InitializeComponent();
        }

        protected override void OnNavigatedTo(NavigationEventArgs e)
        {
            base.OnNavigatedTo(e);
            // The page that sent us here wants the history dropped (login after the
            // session expired, or a stale entry): keep Back from looping.
            object clear;
            if (PhoneApplicationService.Current.State.TryGetValue("clear_back", out clear))
            {
                PhoneApplicationService.Current.State.Remove("clear_back");
                while (NavigationService.CanGoBack)
                    NavigationService.RemoveBackEntry();
            }
            if (KtkClient.Shared.HasSession)
            {
                // Session alive means this page was reached by accident (Back from
                // the week): jump forward and ask WeekPage to clear the history.
                PhoneApplicationService.Current.State["clear_back"] = true;
                NavigationService.Navigate(new System.Uri("/WeekPage.xaml", System.UriKind.Relative));
                return;
            }
            // Restore last login if the session was ended by the site (not by the user).
            string lastLogin = null;
            object obj;
            if (PhoneApplicationService.Current.State.TryGetValue("last_login", out obj))
                lastLogin = obj as string;
            if (!string.IsNullOrEmpty(lastLogin))
                LoginBox.Text = lastLogin;
        }

        private void PasswordBox_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Enter) SignIn();
        }

        private void SignInButton_Click(object sender, RoutedEventArgs e)
        {
            SignIn();
        }

        private async void SignIn()
        {
            if (_busy) return;
            string login    = LoginBox.Text.Trim();
            string password = PasswordBox.Password;
            if (string.IsNullOrEmpty(login) || string.IsNullOrWhiteSpace(password))
            {
                ShowError("Введите логин и пароль.");
                return;
            }
            SetBusy(true);
            PasswordBox.Password = ""; // don't keep it in memory
            try
            {
                var session = await KtkClient.Shared.SignIn(login, password);
                // Remember the login so it can be offered again after the site ends the session.
                PhoneApplicationService.Current.State["last_login"] = session.Login;
                // WeekPage drops the history so Back exits instead of returning here.
                PhoneApplicationService.Current.State["clear_back"] = true;
                NavigationService.Navigate(new System.Uri("/WeekPage.xaml", System.UriKind.Relative));
            }
            catch (KtkException ex)
            {
                ShowError(KtkException.Text(ex));
            }
            catch (System.Exception ex)
            {
                ShowError(ex.GetType().Name + ": " + ex.Message);
            }
            finally { SetBusy(false); }
        }

        private void SetBusy(bool busy)
        {
            _busy                = busy;
            SignInButton.IsEnabled = !busy;
            SignInButton.Content   = busy ? "Вход…" : "Войти";
            Progress.Visibility    = busy ? Visibility.Visible : Visibility.Collapsed;
            LoginBox.IsEnabled     = !busy;
            PasswordBox.IsEnabled  = !busy;
        }

        private void ShowError(string text)
        {
            ErrorText.Text       = text;
            ErrorText.Visibility = string.IsNullOrEmpty(text) ? Visibility.Collapsed : Visibility.Visible;
        }
    }
}
