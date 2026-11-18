// KTK for Windows Phone 8.1 - Lesson detail page.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.IsolatedStorage;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Navigation;
using Microsoft.Phone.Controls;
using Microsoft.Phone.Tasks;

namespace KTKLegacy
{
    public partial class LessonPage : PhoneApplicationPage
    {
        private Lesson _lesson;

        public LessonPage() { InitializeComponent(); }

        protected override void OnNavigatedTo(NavigationEventArgs e)
        {
            base.OnNavigatedTo(e);
            if (!KtkClient.Shared.HasSession) { NavigationService.GoBack(); return; }

            string weekStart = "";
            NavigationContext.QueryString.TryGetValue("week", out weekStart);
            int index = -1;
            string indexStr;
            if (NavigationContext.QueryString.TryGetValue("index", out indexStr))
                int.TryParse(indexStr, out index);

            var week = KtkClient.Shared.ShownWeek(weekStart);
            var all  = week != null ? week.AllLessons() : null;
            if (all == null || index < 0 || index >= all.Count) { NavigationService.GoBack(); return; }
            _lesson = all[index];

            PageTitle.Text = _lesson.Pair + " пара";
            Build();
        }

        // ---------------------------------------------------------------- content builder

        private static readonly SolidColorBrush AccentBrush =
            (SolidColorBrush)Application.Current.Resources["PhoneAccentBrush"];

        private void Build()
        {
            var l = _lesson;
            ContentPanel.Children.Clear();

            // Title
            AddText(l.DisciplineFull ?? l.Discipline, 20, FontWeights.Bold, padding: new Thickness(0, 12, 0, 4));

            // When
            string when = CollegeCalendar.DayTitle(l.Date);
            if (l.TimeText() != null) when = l.TimeText() + "\n" + when;
            AddRow("Когда", when);

            // Hall
            if (l.Remote)                AddRow("Кабинет", "Дистанционно");
            else if (l.HallText() != null) AddRow("Кабинет", l.HallText());

            // Teacher / Group
            var session = KtkClient.Shared.Session;
            bool teacher = session != null && !session.Student;
            if (teacher && Lesson.NotEmpty(l.GroupId))   AddRow("Группа",   l.GroupId);
            else if (!teacher && Lesson.NotEmpty(l.Teacher)) AddRow("Препод.", l.Teacher);

            if (Lesson.NotEmpty(l.PairTypeName)) AddRow("Тип",       l.PairTypeName);
            if (l.SubgroupText() != null)        AddRow("Подгруппа", l.SubgroupText());

            // Grade
            if (!teacher && l.HasGrade())
            {
                AddSectionHeader("Оценка");
                if (l.GradeText() != null)
                {
                    var gradeLabel = AddRow("Оценка", l.GradeText());
                    gradeLabel.Foreground = new SolidColorBrush(GradeColor(l.GradeText()));
                    gradeLabel.FontWeight = FontWeights.Bold;
                }
                if (l.MarkText() != null) AddRow("Отметка", l.MarkText());
            }

            // Task
            if (Lesson.NotEmpty(l.Task) || Lesson.NotEmpty(l.Deadline) || l.HasHomework)
            {
                bool any = Lesson.NotEmpty(l.Task) || Lesson.NotEmpty(l.Deadline);
                if (any || l.Files.Count == 0) AddSectionHeader("Задание");
                if (Lesson.NotEmpty(l.Task))
                {
                    var tb = new TextBlock
                    {
                        Text        = l.Task,
                        TextWrapping = TextWrapping.Wrap,
                        Margin      = new Thickness(0, 6, 0, 6),
                    };
                    ContentPanel.Children.Add(tb);
                }
                if (Lesson.NotEmpty(l.Deadline))
                    AddRow("Срок", CollegeCalendar.DeadlineText(l.Deadline));
                if (!any && l.Files.Count == 0)
                    AddText("Задание есть, но не загрузилось. Обновите неделю.", 15, padding: new Thickness(0, 4, 0, 4));
            }

            // Webinar
            if (Lesson.NotEmpty(l.Webinar))
            {
                AddSectionHeader("Вебинар");
                var link = new HyperlinkButton
                {
                    Content            = l.Webinar,
                    HorizontalAlignment = HorizontalAlignment.Left,
                    NavigateUri        = TryUri(l.Webinar),
                    Margin             = new Thickness(0, 0, 0, 4),
                };
                link.Click += (s, e) => OpenUri(l.Webinar);
                ContentPanel.Children.Add(link);
            }

            // Files
            if (l.Files.Count > 0)
            {
                AddSectionHeader("Файлы");
                for (int i = 0; i < l.Files.Count; i++)
                {
                    var file       = l.Files[i];
                    var fileButton = new Button
                    {
                        Content             = file.FileName(),
                        HorizontalAlignment = HorizontalAlignment.Stretch,
                        HorizontalContentAlignment = HorizontalAlignment.Left,
                        Margin              = new Thickness(0, 2, 0, 2),
                        Tag                 = file,
                    };
                    fileButton.Click += FileButton_Click;
                    ContentPanel.Children.Add(fileButton);
                }
            }
        }

        // ---------------------------------------------------------------- file download / open

        private async void FileButton_Click(object sender, RoutedEventArgs e)
        {
            var button = (Button)sender;
            var file   = (TaskFile)button.Tag;
            button.IsEnabled = false;
            button.Content   = "Загрузка…";
            var res = await KtkClient.Shared.DownloadFile(file);
            var path = res.Path;
            var error = res.Error;
            if (error != null)
            {
                button.Content   = file.FileName();
                button.IsEnabled = true;
                MessageBox.Show(KtkException.Text(error), "Ошибка", MessageBoxButton.OK);
                return;
            }
            button.IsEnabled = true;
            button.Content   = file.FileName();
            // Open the file with the default handler.
            OpenIsoFile(path, file);
        }

        private void OpenIsoFile(string isoPath, TaskFile file)
        {
            // WP8.1: use Launcher to open from IsolatedStorage or move to MediaLibrary.
            // For documents the simplest approach is to use Windows.System.Launcher after
            // copying to a StorageFile in ApplicationData.
            OpenFileWithLauncher(isoPath, file.SafeName());
        }

        private static async void OpenFileWithLauncher(string isoPath, string displayName)
        {
            try
            {
                // Copy from IsolatedStorage to ApplicationData so Launcher can open it.
                var localFolder = Windows.Storage.ApplicationData.Current.LocalFolder;
                var targetFile  = await localFolder.CreateFileAsync(displayName,
                    Windows.Storage.CreationCollisionOption.ReplaceExisting);
                using (var isoStore = IsolatedStorageFile.GetUserStoreForApplication())
                using (var src = isoStore.OpenFile(isoPath, FileMode.Open, FileAccess.Read))
                using (var dst = await targetFile.OpenStreamForWriteAsync())
                    await src.CopyToAsync(dst);
                await Windows.System.Launcher.LaunchFileAsync(targetFile);
            }
            catch (Exception ex)
            {
                MessageBox.Show("Не удалось открыть файл:\n" + ex.Message, "Ошибка", MessageBoxButton.OK);
            }
        }

        // ---------------------------------------------------------------- helpers

        private void AddSectionHeader(string text)
        {
            var tb = new TextBlock
            {
                Text       = text,
                Foreground = AccentBrush,
                FontWeight = FontWeights.Bold,
                Margin     = new Thickness(0, 16, 0, 4),
                Style      = (Style)Resources["PhoneTextSmallStyle"],
            };
            ContentPanel.Children.Add(tb);
        }

        private TextBlock AddRow(string label, string value)
        {
            var grid = new Grid { Margin = new Thickness(0, 2, 0, 2) };
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            var labelTb = new TextBlock
            {
                Text           = label + ":",
                Foreground     = (Brush)Resources["PhoneSubtleBrush"],
                Margin         = new Thickness(0, 0, 8, 0),
                VerticalAlignment = VerticalAlignment.Top,
            };
            var valueTb = new TextBlock
            {
                Text        = value,
                TextWrapping = TextWrapping.Wrap,
            };
            Grid.SetColumn(labelTb, 0);
            Grid.SetColumn(valueTb, 1);
            grid.Children.Add(labelTb);
            grid.Children.Add(valueTb);
            ContentPanel.Children.Add(grid);
            return valueTb;
        }

        private void AddText(string text, int fontSize,
            FontWeight? fontWeight = null, Thickness? padding = null)
        {
            ContentPanel.Children.Add(new TextBlock
            {
                Text        = text,
                FontSize    = fontSize,
                FontWeight  = fontWeight ?? FontWeights.Normal,
                TextWrapping = TextWrapping.Wrap,
                Margin      = padding ?? new Thickness(0),
            });
        }

        private static Color GradeColor(string badge)
        {
            int g;
            if (badge != null && int.TryParse(badge.TrimEnd('+', '-'), out g) && g >= 1 && g <= 5)
            {
                if (g >= 4) return Color.FromArgb(0xFF, 0x27, 0xAE, 0x60);
                if (g == 3) return Color.FromArgb(0xFF, 0xE6, 0x7E, 0x22);
                return Color.FromArgb(0xFF, 0xE7, 0x4C, 0x3C);
            }
            return Colors.Orange;
        }

        private static void OpenUri(string url)
        {
            Uri uri = TryUri(url);
            if (uri != null)
                new WebBrowserTask { Uri = uri }.Show();
        }

        private static Uri TryUri(string url)
        {
            Uri u;
            return Uri.TryCreate(url, UriKind.Absolute, out u) ? u : null;
        }
    }
}
