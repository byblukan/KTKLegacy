package ru.ktkschedule.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.DialogInterface;
import android.content.Intent;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.MenuItem;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class LessonActivity extends Activity {
    static final String EXTRA_WEEK = "week";
    static final String EXTRA_INDEX = "index";
    static final String EXTRA_TEACHER = "teacher";

    private Lesson lesson;
    private boolean teacher;
    private LinearLayout content;
    private final Map<Long, ProgressBar> spinners = new HashMap<Long, ProgressBar>();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Week week = Client.get(this).shownWeek(getIntent().getStringExtra(EXTRA_WEEK));
        int index = getIntent().getIntExtra(EXTRA_INDEX, -1);
        List<Lesson> lessons = week != null ? week.allLessons() : null;
        if (lessons == null || index < 0 || index >= lessons.size()) {
            finish();
            return;
        }
        lesson = lessons.get(index);
        teacher = week.teacher;
        setTitle(lesson.pair + " пара");
        getActionBar().setDisplayHomeAsUpEnabled(true);

        ScrollView scroll = new ScrollView(this);
        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        int pad = Ui.dp(this, 12);
        content.setPadding(pad, pad / 2, pad, pad);
        scroll.addView(content);
        setContentView(scroll);
        build();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (lesson != null && Client.get(this).session() == null) {
            finish();
        }
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item.getItemId() == android.R.id.home) {
            finish();
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    // ---------------------------------------------------------------- content

    private void build() {
        Lesson l = lesson;
        TextView title = text(l.disciplineFull != null ? l.disciplineFull : l.discipline, 18);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setPadding(0, Ui.dp(this, 8), 0, Ui.dp(this, 4));
        content.addView(title);

        String when = CollegeCalendar.dayTitle(l.date);
        if (l.timeText() != null) {
            when = l.timeText() + "\n" + when;
        }
        row("Когда", when);
        if (l.remote) {
            row("Кабинет", "Дистанционно");
        } else if (l.hallText() != null) {
            row("Кабинет", l.hallText());
        }
        if (teacher) {
            if (Lesson.notEmpty(l.groupId)) {
                row("Группа", l.groupId);
            }
        } else if (Lesson.notEmpty(l.teacher)) {
            row("Препод.", l.teacher);
        }
        if (Lesson.notEmpty(l.pairTypeName)) {
            row("Тип", l.pairTypeName);
        }
        if (l.subgroupText() != null) {
            row("Подгруппа", l.subgroupText());
        }

        if (!teacher && l.hasGrade()) {
            header("Оценка");
            if (l.gradeText() != null) {
                TextView grade = row("Оценка", l.gradeText());
                grade.setTextColor(Ui.badgeColor(l.gradeText()));
                grade.setTypeface(Typeface.DEFAULT_BOLD);
            }
            if (l.markText() != null) {
                row("Отметка", l.markText());
            }
        }

        if (Lesson.notEmpty(l.task) || Lesson.notEmpty(l.deadline) || l.hasHomework) {
            boolean any = Lesson.notEmpty(l.task) || Lesson.notEmpty(l.deadline);
            if (any || l.files.isEmpty()) {
                header("Задание");
            }
            if (Lesson.notEmpty(l.task)) {
                TextView task = text(l.task, 15);
                task.setTextIsSelectable(true);
                task.setPadding(0, Ui.dp(this, 6), 0, Ui.dp(this, 6));
                content.addView(task);
            }
            if (Lesson.notEmpty(l.deadline)) {
                row("Срок", CollegeCalendar.deadlineText(l.deadline));
            }
            if (!any && l.files.isEmpty()) {
                content.addView(text("Задание есть, но не загрузилось. Обновите неделю.", 15));
            }
        }

        if (Lesson.notEmpty(l.webinar)) {
            header("Вебинар");
            final String link = l.webinar;
            TextView webinar = text(link, 15);
            webinar.setTextColor(getResources().getColor(R.color.link));
            clickable(webinar, new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    webinarActions(link);
                }
            });
            content.addView(webinar);
            TextView note = text("Старый браузер может не открыть современный сайт: ссылку можно скопировать.", 12);
            note.setTextColor(getResources().getColor(R.color.secondary));
            content.addView(note);
        }

        if (!l.files.isEmpty()) {
            header("Файлы");
            for (TaskFile file : l.files) {
                content.addView(fileRow(file));
            }
        }
    }

    private TextView text(String value, int sp) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(sp);
        view.setTextColor(0xFF000000);
        return view;
    }

    private void header(String title) {
        TextView header = new TextView(this, null, android.R.attr.listSeparatorTextViewStyle);
        header.setText(title);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = Ui.dp(this, 14);
        content.addView(header, params);
    }

    /** A caption on the left, the value on the right; returns the value view. */
    private TextView row(String label, String value) {
        LinearLayout line = new LinearLayout(this);
        line.setOrientation(LinearLayout.HORIZONTAL);
        line.setPadding(0, Ui.dp(this, 6), 0, Ui.dp(this, 6));
        TextView caption = text(label, 13);
        caption.setTypeface(Typeface.DEFAULT_BOLD);
        caption.setTextColor(getResources().getColor(R.color.secondary));
        line.addView(caption, new LinearLayout.LayoutParams(Ui.dp(this, 96), LinearLayout.LayoutParams.WRAP_CONTENT));
        TextView text = text(value, 15);
        text.setTextIsSelectable(true);
        line.addView(text, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        content.addView(line);
        return text;
    }

    private void clickable(View view, View.OnClickListener listener) {
        int pad = Ui.dp(this, 8);
        view.setPadding(pad, Ui.dp(this, 12), pad, Ui.dp(this, 12));
        view.setBackgroundDrawable(Ui.selectableBackground(this));
        view.setClickable(true);
        view.setOnClickListener(listener);
    }

    private View fileRow(final TaskFile file) {
        LinearLayout line = new LinearLayout(this);
        line.setOrientation(LinearLayout.HORIZONTAL);
        line.setGravity(Gravity.CENTER_VERTICAL);
        TextView name = text(file.fileName(), 15);
        name.setTextColor(getResources().getColor(R.color.link));
        line.addView(name, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        ProgressBar spinner = new ProgressBar(this, null, android.R.attr.progressBarStyleSmall);
        spinner.setVisibility(View.GONE);
        line.addView(spinner);
        spinners.put(file.fileId, spinner);
        clickable(line, new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                download(file);
            }
        });
        return line;
    }

    // ---------------------------------------------------------------- actions

    private void webinarActions(final String link) {
        new AlertDialog.Builder(this)
            .setTitle(link)
            .setItems(new String[]{"Открыть в браузере", "Скопировать ссылку"}, new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface dialog, int which) {
                    if (which == 0) {
                        try {
                            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(link)));
                        } catch (ActivityNotFoundException e) {
                            Ui.alert(LessonActivity.this, "Не удалось открыть ссылку.");
                        }
                    } else {
                        ClipboardManager clipboard = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
                        clipboard.setPrimaryClip(ClipData.newPlainText("Вебинар", link));
                        Toast.makeText(LessonActivity.this, "Ссылка скопирована", Toast.LENGTH_SHORT).show();
                    }
                }
            }).show();
    }

    private void download(final TaskFile file) {
        final ProgressBar spinner = spinners.get(file.fileId);
        if (spinner == null || spinner.getVisibility() == View.VISIBLE) {
            return;
        }
        spinner.setVisibility(View.VISIBLE);
        Client.get(this).downloadFile(file, new Client.Callback<File>() {
            @Override
            public void done(File path, KtkError error) {
                spinner.setVisibility(View.GONE);
                if (isFinishing()) {
                    return;
                }
                if (path == null) {
                    Ui.alert(LessonActivity.this, KtkError.text(error));
                    return;
                }
                open(path);
            }
        });
    }

    private void open(File path) {
        Uri uri = FilesProvider.uriFor(path);
        Intent view = new Intent(Intent.ACTION_VIEW)
            .setDataAndType(uri, FilesProvider.mimeType(path.getName()))
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            startActivity(view);
        } catch (ActivityNotFoundException e) {
            Ui.alert(this, "Файл скачан, но на устройстве нет приложения, которое его откроет.");
        }
    }
}
