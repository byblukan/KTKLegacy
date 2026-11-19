// KTK for Windows Phone 8.1 - Week page (timetable + grades tabs, week navigation).
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Navigation;
using Microsoft.Phone.Controls;
using Microsoft.Phone.Shell;
using Microsoft.Phone.Tasks;

namespace KTKLegacy
{
    // ---------------------------------------------------------------- view model

    public sealed class LessonRow : INotifyPropertyChanged
    {
        public event PropertyChangedEventHandler PropertyChanged;
        private void Notify(string name)
        {
            var handler = PropertyChanged;
            if (handler != null)
                handler(this, new PropertyChangedEventArgs(name));
        }

        public int LessonIndex { get; set; }

        private string _dayHeader;
        public string DayHeader
        {
            get { return _dayHeader; }
            set
            {
                _dayHeader = value;
                Notify("DayHeader");
                Notify("DayHeaderVisibility");
            }
        }

        public Visibility DayHeaderVisibility
        {
            get { return string.IsNullOrEmpty(_dayHeader) ? Visibility.Collapsed : Visibility.Visible; }
        }

        public string Discipline { get; set; }
        public string Details    { get; set; }

        private string _badge;
        public string Badge
        {
            get { return _badge; }
            set
            {
                _badge = value;
                Notify("Badge");
                Notify("BadgeVisibility");
                Notify("BadgeBackground");
            }
        }

        public Visibility BadgeVisibility
        {
            get { return string.IsNullOrEmpty(_badge) ? Visibility.Collapsed : Visibility.Visible; }
        }

        public Brush BadgeBackground
        {
            get
            {
                if (_badge == null) return new SolidColorBrush(Colors.Gray);
                // Green for numeric grades, orange for + marks, red for Н/О
                int g;
                if (int.TryParse(_badge.TrimEnd('+','-'), out g) && g >= 1 && g <= 5)
                    return g >= 4 ? new SolidColorBrush(Color.FromArgb(0xFF,0x27,0xAE,0x60))
                                 : g == 3 ? new SolidColorBrush(Color.FromArgb(0xFF,0xE6,0x7E,0x22))
                                          : new SolidColorBrush(Color.FromArgb(0xFF,0xE7,0x4C,0x3C));
                if (_badge == "+") return new SolidColorBrush(Color.FromArgb(0xFF,0x27,0xAE,0x60));
                if (_badge == "-") return new SolidColorBrush(Color.FromArgb(0xFF,0xE7,0x4C,0x3C));
                return new SolidColorBrush(Color.FromArgb(0xFF,0xE6,0x7E,0x22)); // Н, О
            }
        }
    }

    // ---------------------------------------------------------------- page

    public partial class WeekPage : PhoneApplicationPage
    {
        private string _weekStart;
        private Week   _week;
        private bool   _gradesMode;
        private bool   _loading;
        private int    _loadToken;

        // All lessons in flat order (for index → LessonActivity navigation)
        private List<Lesson> _allLessons = new List<Lesson>();

        private readonly ObservableCollection<LessonRow> _pairsRows  = new ObservableCollection<LessonRow>();
        private readonly ObservableCollection<LessonRow> _gradesRows = new ObservableCollection<LessonRow>();

        public WeekPage()
        {
            InitializeComponent();
            PairsListBox.ItemsSource  = _pairsRows;
            GradesListBox.ItemsSource = _gradesRows;
        }

        protected override void OnNavigatedTo(NavigationEventArgs e)
        {
            base.OnNavigatedTo(e);
            // A page that forwarded us here (login after expiry / successful sign-in)
            // asks to drop the whole back history so Back cannot loop between pages.
            object clear;
            if (PhoneApplicationService.Current.State.TryGetValue("clear_back", out clear))
            {
                PhoneApplicationService.Current.State.Remove("clear_back");
                while (NavigationService.CanGoBack)
                    NavigationService.RemoveBackEntry();
            }
            if (!KtkClient.Shared.HasSession)
            {
                GoToLoginPage();
                return;
            }
            UpdateMenu();
            if (_weekStart == null)
                ShowWeek(CollegeCalendar.CurrentWeekStart());
        }

        private void GoToLoginPage()
        {
            PhoneApplicationService.Current.State["clear_back"] = true;
            NavigationService.Navigate(new Uri("/LoginPage.xaml", UriKind.Relative));
        }

        // ---------------------------------------------------------------- navigation

        private void PreviousButton_Click(object sender, RoutedEventArgs e)
        {
            ShowWeek(CollegeCalendar.ShiftWeek(_weekStart, -1));
        }

        private void NextButton_Click(object sender, RoutedEventArgs e)
        {
            ShowWeek(CollegeCalendar.ShiftWeek(_weekStart, +1));
        }

        private void TodayButton_Click(object sender, EventArgs e)
        {
            ShowWeek(CollegeCalendar.CurrentWeekStart());
        }

        private void RefreshButton_Click(object sender, EventArgs e)
        {
            LoadWeek();
        }

        private void MainPivot_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            _gradesMode = MainPivot.SelectedIndex == 1;
        }

        private void LessonList_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            var lb = sender as System.Windows.Controls.ListBox;
            if (lb != null && lb.SelectedItem is LessonRow)
            {
                LessonRow row = (LessonRow)lb.SelectedItem;
                lb.SelectedItem = null;
                if (_week != null)
                {
                    KtkClient.Shared.SetShownWeek(_week);
                    NavigationService.Navigate(
                        new Uri("/LessonPage.xaml?week=" + Uri.EscapeDataString(_weekStart ?? "")
                               + "&index=" + row.LessonIndex, UriKind.Relative));
                }
            }
        }

        private void UpdateMenu()
        {
            var appbar = ApplicationBar;
            if (appbar == null) return;
            appbar.MenuItems.Clear();

            var session = KtkClient.Shared.Session;
            if (session != null && session.Student)
            {
                string sub = session.Subgroup ?? "left";

                var mi1 = new ApplicationBarMenuItem((sub == "left" ? "✓ " : "") + "1 подгруппа");
                mi1.Click += (s, e) => SetSubgroup("left");
                appbar.MenuItems.Add(mi1);

                var mi2 = new ApplicationBarMenuItem((sub == "right" ? "✓ " : "") + "2 подгруппа");
                mi2.Click += (s, e) => SetSubgroup("right");
                appbar.MenuItems.Add(mi2);

                var miAll = new ApplicationBarMenuItem((sub == "all" ? "✓ " : "") + "Обе подгруппы");
                miAll.Click += (s, e) => SetSubgroup("all");
                appbar.MenuItems.Add(miAll);
            }

            var miLogout = new ApplicationBarMenuItem("Выйти");
            miLogout.Click += SignOutMenuItem_Click;
            appbar.MenuItems.Add(miLogout);
        }

        private void SetSubgroup(string subgroup)
        {
            var session = KtkClient.Shared.Session;
            if (session == null || session.Subgroup == subgroup) return;
            KtkClient.Shared.SetSubgroup(subgroup);
            UpdateMenu();
            var cached = KtkClient.Shared.CachedWeek(_weekStart);
            if (cached != null)
            {
                _week = cached;
            }
            if (_week != null)
            {
                Render(_week);
            }
            LoadWeek();
        }

        private async void SignOutMenuItem_Click(object sender, EventArgs e)
        {
            if (MessageBox.Show("Выйти из аккаунта?", "КТК", MessageBoxButton.OKCancel) != MessageBoxResult.OK)
                return;
            PhoneApplicationService.Current.State.Remove("last_login");
            await KtkClient.Shared.SignOut();
            GoToLoginPage();
        }

        // ---------------------------------------------------------------- load

        private void ShowWeek(string weekStart)
        {
            _weekStart = weekStart;
            WeekTitleText.Text = CollegeCalendar.WeekTitle(weekStart);
            var cached = KtkClient.Shared.CachedWeek(weekStart);
            if (cached != null)
            {
                _week = cached;
                Render(cached);
            }
            LoadWeek();
        }

        private async void LoadWeek()
        {
            int token = ++_loadToken;
            SetLoading(true);
            ShowBanner(null);
            try
            {
                var res = await KtkClient.Shared.LoadWeek(_weekStart);
                if (token != _loadToken) return;
                var week = res.Week;
                var error = res.Error;

                if (KtkException.IsLoginRequired(error))
                {
                    GoToLoginPage();
                    return;
                }
                if (week != null)
                {
                    _week = week;
                    KtkClient.Shared.SetShownWeek(week);
                    Render(week);
                }
                if (error != null)
                    ShowBanner(week != null && week.Offline
                        ? "Нет связи. Показаны сохранённые данные.\n" + KtkException.Text(error)
                        : KtkException.Text(error));
            }
            finally
            {
                if (token == _loadToken)
                    SetLoading(false);
            }
        }

        private void SetLoading(bool loading)
        {
            _loading                       = loading;
            LoadingBar.Visibility          = loading ? Visibility.Visible : Visibility.Collapsed;
            PreviousButton.IsEnabled       = !loading;
            NextButton.IsEnabled           = !loading;
        }

        private void ShowBanner(string text)
        {
            if (string.IsNullOrEmpty(text))
            {
                BannerBorder.Visibility = Visibility.Collapsed;
                return;
            }
            BannerText.Text = text;
            BannerBorder.Visibility = Visibility.Visible;
        }

        // ---------------------------------------------------------------- render

        private void Render(Week week)
        {
            _pairsRows.Clear();
            _gradesRows.Clear();
            _allLessons.Clear();

            if (week == null) return;
            var allLessons = week.AllLessons();
            _allLessons    = allLessons;

            var session = KtkClient.Shared.Session;
            string selectedSubgroup = session != null ? session.Subgroup : "left";

            string prevPairsDay  = null;
            string prevGradesDay = null;
            int globalIndex = 0;

            foreach (var day in week.Days)
            {
                bool todayDay = CollegeCalendar.IsToday(day.Date);
                string dayTitle = CollegeCalendar.DayTitle(day.Date);
                if (todayDay) dayTitle += " ◀";

                foreach (var l in day.Lessons)
                {
                    if (Week.MatchesSubgroup(l, selectedSubgroup, week.Teacher))
                    {
                        // Pairs tab
                        var pRow = MakePairsRow(l, globalIndex, day.Date != prevPairsDay ? dayTitle : null);
                        _pairsRows.Add(pRow);
                        prevPairsDay = day.Date;

                        // Grades tab
                        if (l.HasGrade())
                        {
                            var gRow = MakePairsRow(l, globalIndex, day.Date != prevGradesDay ? dayTitle : null);
                            _gradesRows.Add(gRow);
                            prevGradesDay = day.Date;
                        }
                    }
                    globalIndex++;
                }
            }
            UpdateMenu();
        }

        private static LessonRow MakePairsRow(Lesson l, int index, string dayHeader)
        {
            // Details line: time + hall + teacher
            var parts = new List<string>();
            if (l.TimeText() != null)     parts.Add(l.TimeText());
            if (l.IsIndependent())        parts.Add("СРС");
            if (l.Remote)                 parts.Add("Дистанционно");
            else if (l.HallText() != null) parts.Add(l.HallText());
            if (l.ShortTeacher() != null) parts.Add(l.ShortTeacher());
            if (l.SubgroupText() != null) parts.Add(l.SubgroupText());

            return new LessonRow
            {
                LessonIndex = index,
                DayHeader   = dayHeader,
                Discipline  = l.Discipline,
                Details     = string.Join(" · ", parts),
                Badge       = l.BadgeText(),
            };
        }
    }
}
