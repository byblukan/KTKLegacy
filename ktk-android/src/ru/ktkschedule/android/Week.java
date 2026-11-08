package ru.ktkschedule.android;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

final class Week {
    private static final int MAX_FILES = 64;

    static final class Day {
        String date; // yyyy-MM-dd
        List<Lesson> lessons = new ArrayList<Lesson>();
    }

    String weekStart = "";
    List<Day> days = new ArrayList<Day>();
    boolean teacher;
    long updatedAt;  // ms since the epoch, 0 = unknown
    /** Shown from the cache because the site could not be reached. */
    boolean offline;

    List<Lesson> allLessons() {
        List<Lesson> all = new ArrayList<Lesson>();
        for (Day day : days) {
            all.addAll(day.lessons);
        }
        return all;
    }

    /**
     * payload: {weekStart, week: decoded server answer, teacher, extras: {journalId: {homework, webinar}}, savedAt}.
     * {@code filter} applies the bot's rule: a student's day of distance pairs only is shown when any
     * pair has a webinar or a task. Null when the answer has no days.
     */
    static Week fromPayload(Map<String, Object> payload, boolean filter) {
        Map<String, Object> root = Json.mapAt(payload, "week");
        if (root == null || Json.list(root.get("days")) == null) {
            return null;
        }
        boolean teacher = Json.bool(payload.get("teacher"));
        String weekStart = Json.string(payload.get("weekStart"));
        if (weekStart == null) {
            weekStart = "";
        }
        Map<String, Object> extras = Json.mapAt(payload, "extras");
        Map<String, Object> references = Json.mapAt(root, "references");

        Map<String, String> teachers = new HashMap<String, String>();
        for (Object item : Json.listAt(references, "teachers")) {
            Map<String, Object> t = Json.map(item);
            String id = t != null ? Json.string(t.get("id")) : null;
            if (id == null) {
                continue;
            }
            StringBuilder name = new StringBuilder();
            for (String key : new String[]{"surname", "name", "patronymic"}) {
                String part = Json.text(t.get(key));
                if (part != null) {
                    if (name.length() > 0) {
                        name.append(' ');
                    }
                    name.append(part);
                }
            }
            teachers.put(id, name.toString());
        }

        Map<Long, Object[]> halls = new HashMap<Long, Object[]>(); // id -> {number, building, remote}
        for (Object item : Json.listAt(references, "auditoriums")) {
            Map<String, Object> a = Json.map(item);
            Long id = a != null ? Json.key(a.get("auditoriumId")) : null;
            if (id == null) {
                continue;
            }
            String code = Json.text(a.get("code"));
            if (code == null) {
                code = "";
            }
            boolean virtualHall = "virtual".equals(a.get("variant")) || code.equals("Д");
            String building = virtualHall ? "" : Json.text(a.get("buildingShortCode"));
            halls.put(id, new Object[]{virtualHall ? "Дистанционно" : code, building != null ? building : "", virtualHall});
        }

        Map<Long, String> types = new HashMap<Long, String>();
        for (Object item : Json.listAt(references, "pairTypes")) {
            Map<String, Object> pt = Json.map(item);
            Long id = pt != null ? Json.key(pt.get("id")) : null;
            if (id != null) {
                String name = Json.string(pt.get("name"));
                types.put(id, name != null ? name : "");
            }
        }

        // Bell presets: id -> {start, end} minutes, by pair position.
        Map<Long, List<int[]>> presets = new HashMap<Long, List<int[]>>();
        for (Object item : Json.listAt(root, "callPresets")) {
            Map<String, Object> cp = Json.map(item);
            Long id = cp != null ? Json.key(cp.get("id")) : null;
            if (id == null) {
                continue;
            }
            int minutes = beginMinutes(Json.string(cp.get("beginTime")));
            List<int[]> times = new ArrayList<int[]>();
            for (Object stepItem : Json.listAt(cp, "steps")) {
                Map<String, Object> step = Json.map(stepItem);
                int duration = (int) Json.number(step != null ? step.get("duration") : null, 0);
                int pause = (int) Json.number(step != null ? step.get("breakDuration") : null, 0);
                times.add(new int[]{minutes, minutes + duration});
                minutes += duration + pause;
            }
            presets.put(id, times);
        }

        Map<String, Long> bells = new HashMap<String, Long>(); // date -> preset id
        for (Object scopeItem : Json.listAt(root, "bellScopes")) {
            for (Object assignmentItem : Json.listAt(Json.map(scopeItem), "assignments")) {
                Map<String, Object> assignment = Json.map(assignmentItem);
                if (assignment == null) {
                    continue;
                }
                String date = Json.string(assignment.get("date"));
                Long preset = Json.key(assignment.get("presetId"));
                if (date != null && date.length() >= 10 && preset != null) {
                    bells.put(date.substring(0, 10), preset);
                }
            }
        }

        Week week = new Week();
        week.weekStart = weekStart;
        week.teacher = teacher;
        for (Object dayItem : Json.list(root.get("days"))) {
            Map<String, Object> d = Json.map(dayItem);
            String date = d != null ? Json.string(d.get("date")) : null;
            if (date == null || date.isEmpty()) {
                continue;
            }
            Long presetId = date.length() >= 10 ? bells.get(date.substring(0, 10)) : null;
            List<int[]> times = presetId != null ? presets.get(presetId) : null;
            List<Lesson> lessons = new ArrayList<Lesson>();
            Builder builder = new Builder(date, times, teachers, halls, types, extras, weekStart, lessons);

            for (Object pageItem : Json.listAt(d, "pages")) {
                for (Object pairItem : Json.listAt(Json.map(pageItem), "pairs")) {
                    Map<String, Object> p = Json.map(pairItem);
                    if (p != null) {
                        builder.add((int) Json.number(p.get("pairNumber"), 0), p.get("lesson"), null);
                    }
                }
            }
            for (Object pairItem : Json.listAt(d, "pairs")) {
                Map<String, Object> p = Json.map(pairItem);
                if (p == null) {
                    continue;
                }
                int pair = (int) Json.number(p.get("pairNumber"), 0);
                Map<String, Object> ordinary = Json.mapAt(p, "ordinary");
                String[][] lists = {{"wholeLessons", "middle"}, {"leftLessons", "left"}, {"rightLessons", "right"}};
                for (String[] list : lists) {
                    for (Object raw : Json.listAt(ordinary, list[0])) {
                        builder.add(pair, raw, list[1]);
                    }
                }
                for (Object directionItem : Json.listAt(p, "directions")) {
                    for (Object raw : Json.listAt(Json.map(directionItem), "lessons")) {
                        builder.add(pair, raw, null);
                    }
                }
            }

            // Collections.sort is stable.
            Collections.sort(lessons, new Comparator<Lesson>() {
                @Override
                public int compare(Lesson a, Lesson b) {
                    return a.pair < b.pair ? -1 : a.pair > b.pair ? 1 : 0;
                }
            });
            if (filter && !teacher && !lessons.isEmpty()) {
                boolean allRemote = true;
                boolean anyContent = false;
                for (Lesson lesson : lessons) {
                    allRemote = allRemote && lesson.remote;
                    anyContent = anyContent || lesson.hasTaskContent();
                }
                if (allRemote && !anyContent) {
                    lessons.clear();
                }
            }
            Day day = new Day();
            day.date = date.substring(0, Math.min(date.length(), 10));
            day.lessons = lessons;
            week.days.add(day);
        }
        if (week.days.isEmpty()) {
            return null;
        }
        week.updatedAt = Json.number(payload.get("savedAt"), 0);
        return week;
    }

    /** "08:00" or "1970-01-01T08:00:00Z" -> minutes after midnight. */
    private static int beginMinutes(String begin) {
        if (begin == null) {
            return 0;
        }
        int t = begin.indexOf('T');
        String time = t < 0 ? begin : begin.substring(t + 1);
        String[] parts = time.split(":");
        return leadingInt(parts.length > 0 ? parts[0] : "") * 60 + leadingInt(parts.length > 1 ? parts[1] : "");
    }

    private static int leadingInt(String s) {
        int value = 0;
        for (int i = 0; i < s.length() && Character.isDigit(s.charAt(i)) && i < 9; i++) {
            value = value * 10 + (s.charAt(i) - '0');
        }
        return value;
    }

    private static final class Builder {
        final String date;
        final List<int[]> times;
        final Map<String, String> teachers;
        final Map<Long, Object[]> halls;
        final Map<Long, String> types;
        final Map<String, Object> extras;
        final String weekStart;
        final List<Lesson> out;

        Builder(String date, List<int[]> times, Map<String, String> teachers, Map<Long, Object[]> halls,
                Map<Long, String> types, Map<String, Object> extras, String weekStart, List<Lesson> out) {
            this.date = date;
            this.times = times;
            this.teachers = teachers;
            this.halls = halls;
            this.types = types;
            this.extras = extras;
            this.weekStart = weekStart;
            this.out = out;
        }

        void add(int pair, Object raw, String fallbackSubgroup) {
            Map<String, Object> l = Json.map(raw);
            String discipline = l != null ? Json.string(l.get("disciplineShortName")) : null;
            if (discipline == null || discipline.isEmpty()) {
                return;
            }
            Lesson lesson = new Lesson();
            lesson.date = date;
            lesson.pair = pair;
            lesson.discipline = discipline;
            lesson.disciplineFull = Json.text(l.get("disciplineFullName"));
            String teacherId = Json.string(l.get("teacherId"));
            lesson.teacher = teacherId != null ? teachers.get(teacherId) : null;
            lesson.groupId = Json.string(l.get("groupId"));
            String subgroup = Json.text(l.get("subgroup"));
            lesson.subgroup = subgroup != null ? subgroup : fallbackSubgroup;
            Long hallId = Json.key(l.get("auditoriumId"));
            Object[] hall = hallId != null ? halls.get(hallId) : null;
            if (hall != null) {
                lesson.hall = ((String) hall[0]).length() > 0 ? (String) hall[0] : null;
                lesson.building = ((String) hall[1]).length() > 0 ? (String) hall[1] : null;
                lesson.remote = (Boolean) hall[2];
            } else if (hallId != null && hallId > 0) {
                lesson.hall = String.valueOf(hallId);
            }
            lesson.pairTypeId = Json.number(l.get("pairTypeId"), 0);
            lesson.pairTypeName = types.get(lesson.pairTypeId);
            lesson.academicHours = (int) Json.number(l.get("academicHours"), 2);
            lesson.journalId = Json.number(l.get("journalId"), 0);
            Map<String, Object> assessment = Json.mapAt(l, "assessment");
            lesson.grade = (int) Json.number(assessment != null ? assessment.get("grade") : null, 0);
            lesson.mark = (int) Json.number(assessment != null ? assessment.get("mark") : null, 0);
            lesson.hasHomework = Json.bool(l.get("hasHomework"));
            if (times != null && pair >= 1 && pair <= times.size()) {
                lesson.startMinute = times.get(pair - 1)[0];
                lesson.endMinute = times.get(pair - 1)[1];
            }
            applyExtras(Json.mapAt(extras, String.valueOf(lesson.journalId)), lesson, weekStart);
            out.add(lesson);
        }
    }

    /** Lesson extras answer without {@code status} for the own timetable; any other status withholds them. */
    private static boolean restricted(Map<String, Object> value) {
        return value.containsKey("status") && !"available".equals(value.get("status"));
    }

    /**
     * homework: {homework: {task, deadline, files: [{userFileId, caption, extension, availability}]} | null}
     * webinar: {webinar: "https://..." | null}
     */
    private static void applyExtras(Map<String, Object> extra, Lesson lesson, String weekStart) {
        if (extra == null) {
            return;
        }
        Map<String, Object> homeworkCall = Json.mapAt(extra, "homework");
        Map<String, Object> homework = Json.mapAt(homeworkCall, "homework");
        if (homework != null && !restricted(homeworkCall)) {
            lesson.task = Json.text(homework.get("task"));
            lesson.deadline = Json.text(homework.get("deadline"));
            List<TaskFile> files = new ArrayList<TaskFile>();
            for (Object item : Json.listAt(homework, "files")) {
                Map<String, Object> f = Json.map(item);
                if (f == null) {
                    continue;
                }
                long fileId = positiveId(f.get("userFileId"));
                if (fileId == 0 || !"available".equals(f.get("availability")) || files.size() >= MAX_FILES) {
                    continue;
                }
                TaskFile file = new TaskFile();
                file.fileId = fileId;
                file.journalId = lesson.journalId;
                String caption = Json.text(f.get("caption"));
                String extension = Json.text(f.get("extension"));
                file.caption = caption != null ? caption : "";
                file.extension = extension != null ? extension : "";
                file.weekStart = weekStart;
                files.add(file);
            }
            lesson.files = files;
        }
        Map<String, Object> webinarCall = Json.mapAt(extra, "webinar");
        if (webinarCall != null && !restricted(webinarCall)) {
            lesson.webinar = Json.text(webinarCall.get("webinar"));
        }
    }

    private static long positiveId(Object value) {
        long number = 0;
        if (value instanceof Number) {
            number = ((Number) value).longValue();
        } else if (value instanceof String) {
            try {
                number = Long.parseLong(((String) value).trim());
            } catch (NumberFormatException ignored) {
                number = 0;
            }
        }
        return number > 0 ? number : 0;
    }
}
