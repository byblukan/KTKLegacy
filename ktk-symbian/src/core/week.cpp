// KTK for Symbian - a week of lessons.
#include "week.h"
#include "collegecalendar.h"
#include "json.h"

#include <QHash>
#include <QPair>
#include <QRegExp>
#include <QStringList>
#include <QtAlgorithms>

namespace
{
    const int MaxFiles = 64;

    struct Hall
    {
        Hall() : remote(false) {}
        QString number;
        QString building;
        bool remote;
    };

    typedef QList<QPair<int, int> > Times;   // per pair position: start, end minute

    QVariant at(const QVariantMap &m, const char *key) { return m.value(QLatin1String(key)); }

    /// Number keys of the site, as in Java's Json.key: false when not a number.
    bool key(const QVariant &v, qint64 *out)
    {
        if (!Json::isNumber(v)) return false;
        *out = Json::number(v, 0);
        return true;
    }

    int leadingInt(const QString &s)
    {
        int value = 0;
        for (int i = 0; i < s.size() && s.at(i).isDigit() && i < 9; ++i) value = value * 10 + s.at(i).digitValue();
        return value;
    }

    /// "08:00" or "1970-01-01T08:00:00Z" -> minutes after midnight.
    int beginMinutes(const QString &begin)
    {
        if (begin.isNull()) return 0;
        const int t = begin.indexOf(QLatin1Char('T'));
        const QString time = t < 0 ? begin : begin.mid(t + 1);
        const QStringList parts = time.split(QLatin1Char(':'));
        return leadingInt(parts.size() > 0 ? parts.at(0) : QString()) * 60
             + leadingInt(parts.size() > 1 ? parts.at(1) : QString());
    }

    qint64 positiveId(const QVariant &value)
    {
        qint64 number = 0;
        if (Json::isNumber(value)) {
            number = Json::number(value, 0);
        } else if (Json::isString(value)) {
            bool ok = false;
            number = value.toString().trimmed().toLongLong(&ok);
            if (!ok) number = 0;
        }
        return number > 0 ? number : 0;
    }

    /// Lesson extras answer without `status` for the own timetable; any other status withholds them.
    bool restricted(const QVariantMap &value)
    {
        return value.contains(QLatin1String("status")) && Json::string(at(value, "status")) != QLatin1String("available");
    }

    /// homework: {homework: {task, deadline, files: [{userFileId, caption, extension, availability}]} | null}
    /// webinar: {webinar: "https://..." | null}
    void applyExtras(const QVariantMap &extra, Lesson &lesson, const QString &weekStart)
    {
        if (extra.isEmpty()) return;
        const QVariantMap homeworkCall = Json::mapAt(extra, "homework");
        if (Json::hasMap(homeworkCall, "homework") && !restricted(homeworkCall)) {
            const QVariantMap homework = Json::mapAt(homeworkCall, "homework");
            lesson.task = Json::text(at(homework, "task"));
            lesson.deadline = Json::text(at(homework, "deadline"));
            QList<TaskFile> files;
            const QVariantList items = Json::listAt(homework, "files");
            for (int i = 0; i < items.size(); ++i) {
                if (!Json::isMap(items.at(i))) continue;
                const QVariantMap f = items.at(i).toMap();
                const qint64 fileId = positiveId(at(f, "userFileId"));
                if (fileId == 0 || Json::string(at(f, "availability")) != QLatin1String("available")
                    || files.size() >= MaxFiles) {
                    continue;
                }
                TaskFile file;
                file.fileId = fileId;
                file.journalId = lesson.journalId;
                file.caption = Json::text(at(f, "caption"));
                file.extension = Json::text(at(f, "extension"));
                file.weekStart = weekStart;
                files.append(file);
            }
            lesson.files = files;
        }
        if (Json::hasMap(extra, "webinar")) {
            const QVariantMap webinarCall = Json::mapAt(extra, "webinar");
            if (!restricted(webinarCall)) lesson.webinar = Json::text(at(webinarCall, "webinar"));
        }
    }

    class Builder
    {
    public:
        Builder(const QString &date_, const Times *times_, const QHash<QString, QString> &teachers_,
                const QHash<qint64, Hall> &halls_, const QHash<qint64, QString> &types_,
                const QVariantMap &extras_, const QString &weekStart_, QList<Lesson> &out_)
            : date(date_), times(times_), teachers(teachers_), halls(halls_), types(types_),
              extras(extras_), weekStart(weekStart_), out(out_) {}

        void add(int pair, const QVariant &raw, const QString &fallbackSubgroup)
        {
            if (!Json::isMap(raw)) return;
            const QVariantMap l = raw.toMap();
            const QString discipline = Json::string(at(l, "disciplineShortName"));
            if (discipline.isEmpty()) return;
            Lesson lesson;
            lesson.date = date;
            lesson.pair = pair;
            lesson.discipline = discipline;
            lesson.disciplineFull = Json::text(at(l, "disciplineFullName"));
            const QString teacherId = Json::string(at(l, "teacherId"));
            if (!teacherId.isNull()) lesson.teacher = teachers.value(teacherId);
            lesson.groupId = Json::string(at(l, "groupId"));
            const QString subgroup = Json::text(at(l, "subgroup"));
            lesson.subgroup = subgroup.isNull() ? fallbackSubgroup : subgroup;
            qint64 hallId = 0;
            const bool hasHall = key(at(l, "auditoriumId"), &hallId);
            if (hasHall && halls.contains(hallId)) {
                const Hall hall = halls.value(hallId);
                lesson.hall = hall.number;
                lesson.building = hall.building;
                lesson.remote = hall.remote;
            } else if (hasHall && hallId > 0) {
                lesson.hall = QString::number(hallId);
            }
            lesson.pairTypeId = Json::number(at(l, "pairTypeId"), 0);
            lesson.pairTypeName = types.value(lesson.pairTypeId);
            lesson.academicHours = int(Json::number(at(l, "academicHours"), 2));
            lesson.journalId = Json::number(at(l, "journalId"), 0);
            const QVariantMap assessment = Json::mapAt(l, "assessment");
            lesson.grade = int(Json::number(at(assessment, "grade"), 0));
            lesson.mark = int(Json::number(at(assessment, "mark"), 0));
            lesson.hasHomework = Json::boolean(at(l, "hasHomework"));
            if (times && pair >= 1 && pair <= times->size()) {
                lesson.startMinute = times->at(pair - 1).first;
                lesson.endMinute = times->at(pair - 1).second;
            }
            applyExtras(Json::mapAt(extras, QString::number(lesson.journalId).toLatin1().constData()), lesson, weekStart);
            out.append(lesson);
        }

    private:
        const QString date;
        const Times *times;
        const QHash<QString, QString> &teachers;
        const QHash<qint64, Hall> &halls;
        const QHash<qint64, QString> &types;
        const QVariantMap &extras;
        const QString weekStart;
        QList<Lesson> &out;
    };

    bool pairLess(const Lesson &a, const Lesson &b) { return a.pair < b.pair; }
}

// -- TaskFile ------------------------------------------------------------------------------------

QString TaskFile::fileName() const
{
    const QString name = caption.isEmpty() ? QString::fromLatin1("file_%1").arg(fileId) : caption;
    QString ext = extension;
    while (ext.startsWith(QLatin1Char('.'))) ext.remove(0, 1);
    if (ext.isEmpty() || name.toLower().endsWith(QLatin1Char('.') + ext.toLower())) return name;
    return name + QLatin1Char('.') + ext;
}

QString TaskFile::safeName() const
{
    QString name = fileName();
    // Also the characters Symbian file names cannot hold.
    name.replace(QRegExp(QLatin1String("[/:\\\\\\x0000<>\"|?*]")), QLatin1String("_"));
    while (name.startsWith(QLatin1Char('.'))) name.remove(0, 1);
    name = name.trimmed();
    if (name.size() > 120) name = name.right(120);
    return name.isEmpty() ? QString::fromLatin1("file_%1").arg(fileId) : name;
}

// -- Lesson --------------------------------------------------------------------------------------

namespace
{
    QString markSymbol(int mark)
    {
        switch (mark) {
        case 128: return QString::fromLatin1("+");
        case 256: return QString::fromLatin1("-");
        case 16: return QString::fromUtf8("Н");
        case 32: return QString::fromUtf8("О");
        default: return QString::number(mark);
        }
    }

    bool plusMinus(int mark) { return mark == 128 || mark == 256; }
}

QString Lesson::timeText() const
{
    if (startMinute < 0) return QString();
    return CollegeCalendar::timeFromMinutes(startMinute) + QString::fromUtf8("–") + CollegeCalendar::timeFromMinutes(endMinute);
}

QString Lesson::gradeText() const
{
    if (grade == 0 && !plusMinus(mark)) return QString();
    const QString text = grade != 0 ? QString::number(grade) : QString();
    return plusMinus(mark) ? text + markSymbol(mark) : text;
}

QString Lesson::markText() const
{
    return mark != 0 && !plusMinus(mark) ? markSymbol(mark) : QString();
}

QString Lesson::badgeText() const
{
    const QString g = gradeText();
    return !g.isEmpty() ? g : markText();
}

bool Lesson::hasTaskContent() const
{
    return hasHomework || !task.isEmpty() || !webinar.isEmpty() || !files.isEmpty();
}

QString Lesson::subgroupText() const
{
    const QString s = subgroup.toLower();
    if (s == QLatin1String("left") || s == QLatin1String("1")) return QString::fromUtf8("1 подгруппа");
    if (s == QLatin1String("right") || s == QLatin1String("2")) return QString::fromUtf8("2 подгруппа");
    return QString();
}

QString Lesson::shortTeacher() const
{
    const QStringList parts = teacher.trimmed().split(QRegExp(QLatin1String("\\s+")), QString::SkipEmptyParts);
    QString out;
    for (int i = 0; i < parts.size(); ++i) {
        if (out.isEmpty()) out = parts.at(i);
        else out += QLatin1Char(' ') + parts.at(i).left(1) + QLatin1Char('.');
    }
    return out;
}

QString Lesson::hallText() const
{
    if (hall.isEmpty()) return QString();
    return building.isEmpty() ? hall : hall + QString::fromLatin1(" (") + building + QLatin1Char(')');
}

// -- Week ----------------------------------------------------------------------------------------

QList<Lesson> Week::allLessons() const
{
    QList<Lesson> all;
    for (int i = 0; i < days.size(); ++i) all += days.at(i).lessons;
    return all;
}

Week Week::fromPayload(const QVariantMap &payload, bool filter)
{
    Week week;
    if (!Json::hasMap(payload, "week")) return week;
    const QVariantMap root = Json::mapAt(payload, "week");
    if (!Json::isList(at(root, "days"))) return week;
    const bool teacher = Json::boolean(at(payload, "teacher"));
    const QString weekStart = Json::string(at(payload, "weekStart"));
    const QVariantMap extras = Json::mapAt(payload, "extras");
    const QVariantMap references = Json::mapAt(root, "references");

    QHash<QString, QString> teachers;
    const QVariantList teacherItems = Json::listAt(references, "teachers");
    for (int i = 0; i < teacherItems.size(); ++i) {
        const QVariantMap t = Json::map(teacherItems.at(i));
        const QString id = Json::string(at(t, "id"));
        if (id.isNull()) continue;
        QStringList name;
        const char *const keys[] = { "surname", "name", "patronymic" };
        for (int k = 0; k < 3; ++k) {
            const QString part = Json::text(at(t, keys[k]));
            if (!part.isEmpty()) name << part;
        }
        teachers.insert(id, name.join(QLatin1String(" ")));
    }

    QHash<qint64, Hall> halls;
    const QVariantList hallItems = Json::listAt(references, "auditoriums");
    for (int i = 0; i < hallItems.size(); ++i) {
        const QVariantMap a = Json::map(hallItems.at(i));
        qint64 id;
        if (!key(at(a, "auditoriumId"), &id)) continue;
        const QString code = Json::text(at(a, "code"));
        const bool virtualHall = Json::string(at(a, "variant")) == QLatin1String("virtual") || code == QString::fromUtf8("Д");
        Hall hall;
        hall.number = virtualHall ? QString::fromUtf8("Дистанционно") : code;
        hall.building = virtualHall ? QString() : Json::text(at(a, "buildingShortCode"));
        hall.remote = virtualHall;
        halls.insert(id, hall);
    }

    QHash<qint64, QString> types;
    const QVariantList typeItems = Json::listAt(references, "pairTypes");
    for (int i = 0; i < typeItems.size(); ++i) {
        const QVariantMap pt = Json::map(typeItems.at(i));
        qint64 id;
        if (key(at(pt, "id"), &id)) types.insert(id, Json::string(at(pt, "name")));
    }

    // Bell presets: id -> {start, end} minutes, by pair position.
    QHash<qint64, Times> presets;
    const QVariantList presetItems = Json::listAt(root, "callPresets");
    for (int i = 0; i < presetItems.size(); ++i) {
        const QVariantMap cp = Json::map(presetItems.at(i));
        qint64 id;
        if (!key(at(cp, "id"), &id)) continue;
        int minutes = beginMinutes(Json::string(at(cp, "beginTime")));
        Times times;
        const QVariantList steps = Json::listAt(cp, "steps");
        for (int s = 0; s < steps.size(); ++s) {
            const QVariantMap step = Json::map(steps.at(s));
            const int duration = int(Json::number(at(step, "duration"), 0));
            const int pause = int(Json::number(at(step, "breakDuration"), 0));
            times.append(qMakePair(minutes, minutes + duration));
            minutes += duration + pause;
        }
        presets.insert(id, times);
    }

    QHash<QString, qint64> bells;   // date -> preset id
    const QVariantList scopes = Json::listAt(root, "bellScopes");
    for (int i = 0; i < scopes.size(); ++i) {
        const QVariantList assignments = Json::listAt(Json::map(scopes.at(i)), "assignments");
        for (int a = 0; a < assignments.size(); ++a) {
            const QVariantMap assignment = Json::map(assignments.at(a));
            const QString date = Json::string(at(assignment, "date"));
            qint64 preset;
            if (date.size() >= 10 && key(at(assignment, "presetId"), &preset)) bells.insert(date.left(10), preset);
        }
    }

    week.weekStart = weekStart;
    week.teacher = teacher;
    const QVariantList dayItems = Json::list(at(root, "days"));
    for (int i = 0; i < dayItems.size(); ++i) {
        const QVariantMap d = Json::map(dayItems.at(i));
        const QString date = Json::string(at(d, "date"));
        if (date.isEmpty()) continue;
        const Times *times = 0;
        if (date.size() >= 10 && bells.contains(date.left(10))) {
            QHash<qint64, Times>::const_iterator it = presets.constFind(bells.value(date.left(10)));
            if (it != presets.constEnd()) times = &it.value();
        }
        QList<Lesson> lessons;
        Builder builder(date, times, teachers, halls, types, extras, weekStart, lessons);

        const QVariantList pages = Json::listAt(d, "pages");
        for (int p = 0; p < pages.size(); ++p) {
            const QVariantList pairs = Json::listAt(Json::map(pages.at(p)), "pairs");
            for (int k = 0; k < pairs.size(); ++k) {
                if (!Json::isMap(pairs.at(k))) continue;
                const QVariantMap pr = pairs.at(k).toMap();
                builder.add(int(Json::number(at(pr, "pairNumber"), 0)), at(pr, "lesson"), QString());
            }
        }
        const QVariantList pairs = Json::listAt(d, "pairs");
        for (int k = 0; k < pairs.size(); ++k) {
            if (!Json::isMap(pairs.at(k))) continue;
            const QVariantMap pr = pairs.at(k).toMap();
            const int pair = int(Json::number(at(pr, "pairNumber"), 0));
            const QVariantMap ordinary = Json::mapAt(pr, "ordinary");
            const char *const lists[3][2] = { { "wholeLessons", "middle" }, { "leftLessons", "left" }, { "rightLessons", "right" } };
            for (int li = 0; li < 3; ++li) {
                const QVariantList raws = Json::listAt(ordinary, lists[li][0]);
                for (int r = 0; r < raws.size(); ++r) builder.add(pair, raws.at(r), QLatin1String(lists[li][1]));
            }
            const QVariantList directions = Json::listAt(pr, "directions");
            for (int di = 0; di < directions.size(); ++di) {
                const QVariantList raws = Json::listAt(Json::map(directions.at(di)), "lessons");
                for (int r = 0; r < raws.size(); ++r) builder.add(pair, raws.at(r), QString());
            }
        }

        qStableSort(lessons.begin(), lessons.end(), pairLess);
        if (filter && !teacher && !lessons.isEmpty()) {
            bool allRemote = true;
            bool anyContent = false;
            for (int k = 0; k < lessons.size(); ++k) {
                allRemote = allRemote && lessons.at(k).remote;
                anyContent = anyContent || lessons.at(k).hasTaskContent();
            }
            if (allRemote && !anyContent) lessons.clear();
        }
        Day day;
        day.date = date.left(10);
        day.lessons = lessons;
        week.days.append(day);
    }
    if (week.days.isEmpty()) return Week();
    week.updatedAt = Json::number(at(payload, "savedAt"), 0);
    return week;
}
