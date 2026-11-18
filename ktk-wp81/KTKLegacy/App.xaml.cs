// KTK for Windows Phone 8.1 - App entry point.
using System.Windows;
using System.Windows.Navigation;
using Microsoft.Phone.Controls;
using Microsoft.Phone.Shell;

namespace KTKLegacy
{
    public partial class App : Application
    {
        public PhoneApplicationFrame RootFrame { get; private set; }

        public App()
        {
            UnhandledException += Application_UnhandledException;
            InitializeComponent();
            InitializePhoneApplication();
        }

        private void Application_Launching(object sender, LaunchingEventArgs e) { }
        private void Application_Activated(object sender, ActivatedEventArgs e) { }
        private void Application_Deactivated(object sender, DeactivatedEventArgs e) { }
        private void Application_Closing(object sender, ClosingEventArgs e) { }

        private void RootFrame_NavigationFailed(object sender, NavigationFailedEventArgs e)
        {
            if (System.Diagnostics.Debugger.IsAttached)
                System.Diagnostics.Debugger.Break();
        }

        private void Application_UnhandledException(object sender, ApplicationUnhandledExceptionEventArgs e)
        {
            e.Handled = true;
            try
            {
                Deployment.Current.Dispatcher.BeginInvoke(() =>
                {
                    MessageBox.Show(e.ExceptionObject.ToString(), "Ошибка приложения", MessageBoxButton.OK);
                });
            }
            catch { }
            if (System.Diagnostics.Debugger.IsAttached)
                System.Diagnostics.Debugger.Break();
        }

        // ---------------------------------------------------------------- Phone application initialization

        private bool _phoneApplicationInitialized;

        private void InitializePhoneApplication()
        {
            if (_phoneApplicationInitialized) return;
            RootFrame = new PhoneApplicationFrame();
            RootFrame.Navigated += CompleteInitializePhoneApplication;
            RootFrame.NavigationFailed += RootFrame_NavigationFailed;
            _phoneApplicationInitialized = true;
        }

        private void CompleteInitializePhoneApplication(object sender, NavigationEventArgs e)
        {
            if (RootVisual != RootFrame) RootVisual = RootFrame;
            RootFrame.Navigated -= CompleteInitializePhoneApplication;
        }
    }
}
