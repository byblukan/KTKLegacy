package ru.ktkschedule.android;

import android.app.ActionBar;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.FragmentTransaction;
import android.content.DialogInterface;
import android.content.Intent;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.ListView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

public final class WeekActivity extends Activity {
    private static final long STALE_MS = 5 * 60 * 1000;

    private String weekStart;
    private Week week;
    private KtkError error;
    private boolean loading;
    private boolean gradesMode;
    private boolean scrolledToToday;
    private int token;

    private ListView list;
    private TextView banner;
    private final Adapter adapter = new Adapter();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        requestWindowFeature(Window.FEATURE_INDETERMINATE_PROGRESS);
        super.onCreate(savedInstanceState);
        if (Client.get(this).session() == null) {
            openLogin();
            return;
        }
        setContentView(R.layout.activity_week);
        setProgressBarIndeterminateVisibility(false);
        list = (ListView) findViewById(R.id.list);
        banner = (TextView) findViewById(R.id.banner);
        list.setAdapter(adapter);
        list.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                openLesson(adapter.getItem(position).lesson);
            }
        });
        findViewById(R.id.previous).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                showWeek(CollegeCalendar.shiftWeek(weekStart, -1));
            }
        });
        findViewById(R.id.next).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                showWeek(CollegeCalendar.shiftWeek(weekStart, 1));
            }
        });
        findViewById(R.id.current).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                showWeek(CollegeCalendar.currentWeekStart());
            }
        });

        if (savedInstanceState != null) {
            gradesMode = savedInstanceState.getBoolean("grades");
        }
        setUpTabs();
        String saved = savedInstanceState != null ? savedInstanceState.getString("week") : null;
        showWeek(saved != null ? saved : CollegeCalendar.currentWeekStart());
    }

    private void setUpTabs() {
        ActionBar bar = getActionBar();
        bar.setNavigationMode(ActionBar.NAVIGATION_MODE_TABS);
        ActionBar.TabListener listener = new ActionBar.TabListener() {
            @Override
            public void onTabSelected(ActionBar.Tab tab, FragmentTransaction ft) {
                boolean grades = tab.getPosition() == 1;
                if (grades != gradesMode) {
                    gradesMode = grades;
                    rebuild();
                }
            }

            @Override
            public void onTabUnselected(ActionBar.Tab tab, FragmentTransaction ft) {
            }

            @Override
            public void onTabReselected(ActionBar.Tab tab, FragmentTransaction ft) {
            }
        };
        bar.addTab(bar.newTab().setText(R.string.tab_pairs).setTabListener(listener), !gradesMode);
        bar.addTab(bar.newTab().setText(R.string.tab_grades).setTabListener(listener), gradesMode);
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        outState.putString("week", weekStart);
        outState.putBoolean("grades", gradesMode);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (list == null) {
            return;
        }
        if (Client.get(this).session() == null) {
            openLogin();
            return;
        }
        // Back after a while: the week may have changed.
        if (!loading && week != null && (week.updatedAt == 0 || System.currentTimeMillis() - week.updatedAt > STALE_MS)) {
            reload();
        }
        adapter.notifyDataSetChanged(); // "сегодня" may have moved
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.week, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item.getItemId() == R.id.refresh) {
            if (!loading) {
                reload();
            }
            return true;
        }
        if (item.getItemId() == R.id.sign_out) {
            confirmSignOut();
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    // ---------------------------------------------------------------- navigation

    private void showWeek(String start) {
        weekStart = start;
        scrolledToToday = false;
        setTitle(CollegeCalendar.weekTitle(start));
        week = Client.get(this).cachedWeek(start);
        if (week != null) {
            week.offline = false;
        }
        error = null;
        rebuild();
        list.setSelection(0);
        reload();
    }

    private void reload() {
        final int current = ++token;
        loading = true;
        setProgressBarIndeterminateVisibility(true);
        if (week == null) {
            rebuild();
        }
        Client.get(this).loadWeek(weekStart, new Client.Callback<Week>() {
            @Override
            public void done(Week loaded, KtkError failure) {
                if (current != token || isFinishing()) {
                    return; // the user went to another week meanwhile
                }
                loading = false;
                setProgressBarIndeterminateVisibility(false);
                if (KtkError.isLoginRequired(failure)) {
                    // The client signed out locally.
                    new AlertDialog.Builder(WeekActivity.this).setMessage(KtkError.text(failure))
                        .setCancelable(false)
                        .setPositiveButton(android.R.string.ok, new DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialog, int which) {
                                openLogin();
                            }
                        }).show();
                    return;
                }
                if (loaded != null) {
                    week = loaded;
                }
                error = failure;
                rebuild();
                scrollToTodayOnce();
            }
        });
    }

    private void scrollToTodayOnce() {
        if (scrolledToToday || gradesMode || week == null) {
            return;
        }
        scrolledToToday = true;
        for (int i = 1; i < adapter.rows.size(); i++) {
            Row row = adapter.rows.get(i);
            if (row.type == Row.HEADER && CollegeCalendar.isToday(row.date)) {
                list.setSelection(i);
                break;
            }
        }
    }

    private void openLesson(Lesson lesson) {
        if (lesson == null || week == null) {
            return;
        }
        int index = week.allLessons().indexOf(lesson);
        if (index < 0) {
            return;
        }
        Client.get(this).setShownWeek(week);
        startActivity(new Intent(this, LessonActivity.class)
            .putExtra(LessonActivity.EXTRA_WEEK, week.weekStart)
            .putExtra(LessonActivity.EXTRA_INDEX, index)
            .putExtra(LessonActivity.EXTRA_TEACHER, week.teacher));
    }

    private void openLogin() {
        startActivity(new Intent(this, LoginActivity.class));
        finish();
    }

    private void confirmSignOut() {
        Session session = Client.get(this).session();
        String login = session != null ? session.login : "";
        new AlertDialog.Builder(this)
            .setMessage(login.isEmpty() ? "Выйти из аккаунта?" : "Выйти из аккаунта " + login + "?")
            .setNegativeButton(R.string.cancel, null)
            .setPositiveButton(R.string.sign_out, new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface dialog, int which) {
                    Client.get(WeekActivity.this).signOut();
                    openLogin();
                }
            }).show();
    }

    // ---------------------------------------------------------------- content

    private void rebuild() {
        List<Row> rows = new ArrayList<Row>();
        boolean anyDay = false;
        if (week != null) {
            for (Week.Day day : week.days) {
                List<Lesson> lessons = new ArrayList<Lesson>();
                for (Lesson lesson : day.lessons) {
                    if (!gradesMode || lesson.hasGrade()) {
                        lessons.add(lesson);
                    }
                }
                if (gradesMode && lessons.isEmpty()) {
                    continue;
                }
                if (!gradesMode && lessons.isEmpty() && CollegeCalendar.weekdayOfDate(day.date) == 6) {
                    continue; // an empty Sunday
                }
                anyDay = true;
                rows.add(Row.header(day.date));
                if (lessons.isEmpty()) {
                    rows.add(Row.message("Пар нет"));
                }
                for (Lesson lesson : lessons) {
                    rows.add(Row.lesson(lesson));
                }
            }
        }
        if (!anyDay) {
            String message;
            if (week == null) {
                message = loading ? "Загрузка…" : (error != null ? KtkError.text(error) : "Нет данных.");
            } else {
                message = gradesMode ? "На этой неделе оценок нет." : "На этой неделе пар нет.";
            }
            rows.add(Row.message(message));
        }
        adapter.rows = rows;
        adapter.notifyDataSetChanged();

        String bannerText = null;
        if (week != null && week.offline && error != null) {
            bannerText = week.updatedAt != 0
                ? KtkError.text(error) + "\nПоказано сохранённое " + CollegeCalendar.shortDateTime(week.updatedAt) + "."
                : KtkError.text(error);
        }
        banner.setText(bannerText);
        banner.setVisibility(bannerText != null ? View.VISIBLE : View.GONE);
    }

    private static final class Row {
        static final int HEADER = 0;
        static final int LESSON = 1;
        static final int MESSAGE = 2;

        int type;
        String date;
        String text;
        Lesson lesson;

        static Row header(String date) {
            Row row = new Row();
            row.type = HEADER;
            row.date = date;
            return row;
        }

        static Row lesson(Lesson lesson) {
            Row row = new Row();
            row.type = LESSON;
            row.lesson = lesson;
            return row;
        }

        static Row message(String text) {
            Row row = new Row();
            row.type = MESSAGE;
            row.text = text;
            return row;
        }
    }

    private final class Adapter extends BaseAdapter {
        List<Row> rows = new ArrayList<Row>();

        @Override
        public int getCount() {
            return rows.size();
        }

        @Override
        public Row getItem(int position) {
            return rows.get(position);
        }

        @Override
        public long getItemId(int position) {
            return position;
        }

        @Override
        public int getViewTypeCount() {
            return 3;
        }

        @Override
        public int getItemViewType(int position) {
            return rows.get(position).type;
        }

        @Override
        public boolean areAllItemsEnabled() {
            return false;
        }

        @Override
        public boolean isEnabled(int position) {
            return rows.get(position).type == Row.LESSON;
        }

        @Override
        public View getView(int position, View view, ViewGroup parent) {
            Row row = rows.get(position);
            LayoutInflater inflater = getLayoutInflater();
            switch (row.type) {
                case Row.HEADER: {
                    TextView header = (TextView) (view != null ? view : inflater.inflate(R.layout.item_header, parent, false));
                    String title = CollegeCalendar.dayTitle(row.date);
                    header.setText(CollegeCalendar.isToday(row.date) ? title + " · сегодня" : title);
                    return header;
                }
                case Row.MESSAGE: {
                    TextView message = (TextView) (view != null ? view : inflater.inflate(R.layout.item_message, parent, false));
                    message.setText(row.text);
                    return message;
                }
                default: {
                    View cell = view != null ? view : inflater.inflate(R.layout.item_lesson, parent, false);
                    bindLesson(cell, row.lesson);
                    return cell;
                }
            }
        }

        private void bindLesson(View cell, Lesson lesson) {
            boolean teacher = week != null && week.teacher;
            ((TextView) cell.findViewById(R.id.title)).setText(lesson.pair + ". " + lesson.discipline);
            ((TextView) cell.findViewById(R.id.details)).setText(detailText(lesson, teacher));
            TextView badge = (TextView) cell.findViewById(R.id.badge);
            String badgeText = teacher ? null : lesson.badgeText();
            if (badgeText != null) {
                badge.setText(badgeText);
                badge.setTextSize(badgeText.length() > 2 ? 15 : 22);
                badge.setTextColor(Ui.badgeColor(badgeText));
            } else {
                badge.setText("›");
                badge.setTextSize(22);
                badge.setTextColor(0xFFBBBBBB);
            }
        }
    }

    static String detailText(Lesson lesson, boolean teacher) {
        List<String> lines = new ArrayList<String>();
        List<String> first = new ArrayList<String>();
        if (lesson.timeText() != null) {
            first.add(lesson.timeText());
        }
        if (lesson.remote) {
            first.add("дистанционно");
        } else if (lesson.hallText() != null) {
            first.add("каб. " + lesson.hallText());
        }
        if (lesson.subgroupText() != null) {
            first.add(lesson.subgroupText());
        }
        if (!first.isEmpty()) {
            lines.add(join(first, " · "));
        }
        String who = teacher
            ? (Lesson.notEmpty(lesson.groupId) ? "Группа " + lesson.groupId : null)
            : lesson.shortTeacher();
        if (who != null) {
            lines.add(who);
        }
        List<String> extras = new ArrayList<String>();
        if (Lesson.notEmpty(lesson.task) || lesson.hasHomework) {
            extras.add("задание");
        }
        if (!lesson.files.isEmpty()) {
            extras.add("файлов: " + lesson.files.size());
        }
        if (Lesson.notEmpty(lesson.webinar)) {
            extras.add("вебинар");
        }
        if (!extras.isEmpty()) {
            lines.add("● " + join(extras, " · "));
        }
        return join(lines, "\n");
    }

    private static String join(List<String> parts, String separator) {
        StringBuilder out = new StringBuilder();
        for (String part : parts) {
            if (out.length() > 0) {
                out.append(separator);
            }
            out.append(part);
        }
        return out.toString();
    }
}
