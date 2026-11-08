// KTK for Symbian - a week of lessons as the site describes it, with the lessons' tasks,
// webinars and files (the same rules as ktk-android Week/Lesson/TaskFile).
#ifndef WEEK_H
#define WEEK_H

#include <QList>
#include <QString>
#include <QVariant>

/// A task file; downloads go to the lesson's file route of the own timetable.
struct TaskFile
{
    TaskFile() : fileId(0), journalId(0) {}

    qint64 fileId;
    qint64 journalId;
    QString caption;
    QString extension;
    QString weekStart;

    /// "Отчёт" + "pdf" -> "Отчёт.pdf"; a caption already ending with the extension stays.
    QString fileName() const;
    /// fileName() made safe as one path component: no separators, no leading dots, at most
    /// 120 characters.
    QString safeName() const;
};

struct Lesson
{
    Lesson() : pair(0), remote(false), pairTypeId(0), academicHours(2), journalId(0), grade(0), mark(0),
               hasHomework(false), startMinute(-1), endMinute(-1) {}

    QString date;
    int pair;
    QString discipline;
    QString disciplineFull;
    QString teacher;        ///< "Фамилия Имя Отчество"
    QString groupId;
    QString subgroup;       ///< left/right/middle or as the site says
    QString hall;           ///< "18А", "Дистанционно" or empty
    QString building;
    bool remote;
    qint64 pairTypeId;
    QString pairTypeName;
    int academicHours;
    qint64 journalId;
    int grade;              ///< 0 = none
    int mark;               ///< 0 = none; 128 "+", 256 "-", 16 "Н", 32 "О"
    bool hasHomework;
    int startMinute;        ///< -1 when the bell preset is unknown
    int endMinute;
    QString task;
    QString deadline;
    QString webinar;
    QList<TaskFile> files;

    QString timeText() const;     ///< "08:00–09:30" or empty
    QString gradeText() const;    ///< "5", "5+" or empty
    QString markText() const;     ///< "Н" etc. or empty
    QString badgeText() const;    ///< "5", "+", "Н" or empty
    bool hasGrade() const { return !badgeText().isEmpty(); }
    bool hasTaskContent() const;
    QString subgroupText() const; ///< "1 подгруппа" or empty
    QString shortTeacher() const; ///< "Иванова А. П." or empty
    QString hallText() const;     ///< "18А (IT)" or empty
};

struct Day
{
    QString date;   // yyyy-MM-dd
    QList<Lesson> lessons;
};

struct Week
{
    Week() : teacher(false), updatedAt(0), offline(false) {}

    QString weekStart;
    QList<Day> days;
    bool teacher;
    qint64 updatedAt;   ///< ms since the epoch, 0 = unknown
    /// Shown from the cache because the site could not be reached.
    bool offline;

    bool isNull() const { return days.isEmpty(); }
    QList<Lesson> allLessons() const;

    /// payload: {weekStart, week: decoded server answer, teacher, extras: {journalId: {homework,
    /// webinar}}, savedAt}. `filter` applies the bot's rule: a student's day of distance pairs
    /// only is shown when any pair has a webinar or a task. A null week when there are no days.
    static Week fromPayload(const QVariantMap &payload, bool filter);
};

#endif // WEEK_H
