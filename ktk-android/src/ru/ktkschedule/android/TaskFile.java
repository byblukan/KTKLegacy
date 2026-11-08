package ru.ktkschedule.android;

/** A task file; downloads go to the lesson's file route of the own timetable. */
final class TaskFile {
    long fileId;
    long journalId;
    String caption = "";
    String extension = "";
    String weekStart = "";

    /** "Отчёт" + "pdf" -> "Отчёт.pdf"; a caption already ending with the extension stays. */
    String fileName() {
        String caption = this.caption.length() > 0 ? this.caption : "file_" + fileId;
        String extension = this.extension;
        while (extension.startsWith(".")) {
            extension = extension.substring(1);
        }
        if (extension.isEmpty() || caption.toLowerCase().endsWith("." + extension.toLowerCase())) {
            return caption;
        }
        return caption + "." + extension;
    }

    /** fileName() made safe as one path component: no separators, no leading dots, at most 120 chars. */
    String safeName() {
        String name = fileName().replaceAll("[/:\\\\\\x00]", "_");
        while (name.startsWith(".")) {
            name = name.substring(1);
        }
        if (name.length() > 120) {
            name = name.substring(name.length() - 120);
        }
        return name.isEmpty() ? "file_" + fileId : name;
    }
}
