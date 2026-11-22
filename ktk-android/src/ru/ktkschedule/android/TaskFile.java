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

    /** fileName() made safe as one path component: no separators, no leading dots, at most 120 chars.
     *  Truncates the base name from the end but keeps the file extension intact, so two files with
     *  different beginnings do not collide. */
    String safeName() {
        String name = fileName();
        name = name.replaceAll("[/:\\\\\\x00]", "_");
        while (name.startsWith(".")) {
            name = name.substring(1);
        }
        // Preserve the extension so different files don't collide after truncation.
        int dot = name.lastIndexOf('.');
        String base = (dot > 0) ? name.substring(0, dot) : name;
        String ext = (dot > 0) ? name.substring(dot) : "";
        if (base.length() + ext.length() > 120) {
            base = base.substring(0, 120 - ext.length());
        }
        name = base + ext;
        while (name.startsWith(".")) {
            name = name.substring(1);
        }
        return name.isEmpty() ? "file_" + fileId : name;
    }
}
