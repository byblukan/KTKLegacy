package ru.ktkschedule.android;

import java.util.ArrayList;
import java.util.List;

final class Lesson {
    String date;
    int pair;
    String discipline;
    String disciplineFull;
    String teacher;        // "Фамилия Имя Отчество"
    String groupId;
    String subgroup;       // left/right/middle or as the site says
    String hall;           // "18А", "Дистанционно" or null
    String building;
    boolean remote;
    long pairTypeId;
    String pairTypeName;
    int academicHours;
    long journalId;
    int grade;             // 0 = none
    int mark;              // 0 = none; 128 "+", 256 "-", 16 "Н", 32 "О"
    boolean hasHomework;
    int startMinute = -1;  // -1 when the bell preset is unknown
    int endMinute = -1;
    String task;
    String deadline;
    String webinar;
    List<TaskFile> files = new ArrayList<TaskFile>();

    /** "08:00–09:30" or null. */
    String timeText() {
        if (startMinute < 0) {
            return null;
        }
        return CollegeCalendar.timeFromMinutes(startMinute) + "–" + CollegeCalendar.timeFromMinutes(endMinute);
    }

    private static String markSymbol(int mark) {
        switch (mark) {
            case 128: return "+";
            case 256: return "-";
            case 16: return "Н";
            case 32: return "О";
            default: return String.valueOf(mark);
        }
    }

    private boolean plusMinus() {
        return mark == 128 || mark == 256;
    }

    /** "5", "5+" or null. */
    String gradeText() {
        if (grade == 0 && !plusMinus()) {
            return null;
        }
        String text = grade != 0 ? String.valueOf(grade) : "";
        return plusMinus() ? text + markSymbol(mark) : text;
    }

    /** "Н" etc. or null (only marks that are not a +/- next to a grade). */
    String markText() {
        return mark != 0 && !plusMinus() ? markSymbol(mark) : null;
    }

    /** "5", "+", "Н" or null. */
    String badgeText() {
        String grade = gradeText();
        return grade != null ? grade : markText();
    }

    boolean hasGrade() {
        return badgeText() != null;
    }

    boolean hasTaskContent() {
        return hasHomework || notEmpty(task) || notEmpty(webinar) || !files.isEmpty();
    }

    /** "1 подгруппа" or null. */
    String subgroupText() {
        String s = subgroup != null ? subgroup.toLowerCase() : "";
        if (s.equals("left") || s.equals("1")) {
            return "1 подгруппа";
        }
        if (s.equals("right") || s.equals("2")) {
            return "2 подгруппа";
        }
        return null;
    }

    /** "Иванова А. П." or null. */
    String shortTeacher() {
        if (teacher == null) {
            return null;
        }
        StringBuilder out = new StringBuilder();
        for (String part : teacher.trim().split("\\s+")) {
            if (part.isEmpty()) {
                continue;
            }
            if (out.length() == 0) {
                out.append(part);
            } else {
                out.append(' ').append(part.charAt(0)).append('.');
            }
        }
        return out.length() > 0 ? out.toString() : null;
    }

    /** "18А (IT)" or null. */
    String hallText() {
        if (!notEmpty(hall)) {
            return null;
        }
        return notEmpty(building) ? hall + " (" + building + ")" : hall;
    }

    static boolean notEmpty(String s) {
        return s != null && s.length() > 0;
    }
}
