// KTK for Windows Phone 8.1 - a week of lessons as the site describes it.
// Port of ktk-android Week.java / Lesson.java / TaskFile.java.
using System;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;

namespace KTKLegacy
{
    public sealed class TaskFile
    {
        public long   FileId;
        public long   JournalId;
        public string Caption   = "";
        public string Extension = "";
        public string WeekStart = "";

        /// <summary>"Отчёт" + "pdf" → "Отчёт.pdf"</summary>
        public string FileName()
        {
            string caption = Caption.Length > 0 ? Caption : "file_" + FileId;
            string ext = Extension;
            while (ext.StartsWith(".")) ext = ext.Substring(1);
            if (ext.Length == 0 || caption.ToLowerInvariant().EndsWith("." + ext.ToLowerInvariant()))
                return caption;
            return caption + "." + ext;
        }

        /// <summary>FileName() made safe as one path component.</summary>
        public string SafeName()
        {
            string name = Regex.Replace(FileName(), @"[/:\\]|\x00", "_");
            while (name.StartsWith(".")) name = name.Substring(1);
            if (name.Length > 120) name = name.Substring(name.Length - 120);
            return name.Length == 0 ? "file_" + FileId : name;
        }
    }

    public sealed class Lesson
    {
        public string Date;
        public int    Pair;
        public string Discipline;
        public string DisciplineFull;
        public string Teacher;      // "Фамилия Имя Отчество"
        public string GroupId;
        public string Subgroup;     // left/right/middle or as the site says
        public string Hall;         // "18А", "Дистанционно" or null
        public string Building;
        public bool   Remote;
        public long   PairTypeId;
        public string PairTypeName;
        public int    AcademicHours = 2;
        public long   JournalId;
        public int    Grade;        // 0 = none
        public int    Mark;         // 0 = none; 128 "+", 256 "-", 16 "Н", 32 "О"
        public bool   HasHomework;
        public int    StartMinute = -1; // -1 when the bell preset is unknown
        public int    EndMinute   = -1;
        public string Task;
        public string Deadline;
        public string Webinar;
        public List<TaskFile> Files = new List<TaskFile>();

        /// <summary>"08:00–09:30" or null.</summary>
        public string TimeText()
        {
            if (StartMinute < 0) return null;
            return CollegeCalendar.TimeFromMinutes(StartMinute) + "–"
                 + CollegeCalendar.TimeFromMinutes(EndMinute);
        }

        private static string MarkSymbol(int mark)
        {
            switch (mark)
            {
                case 128: return "+";
                case 256: return "-";
                case 16:  return "Н";
                case 32:  return "О";
                default:  return mark.ToString();
            }
        }

        private bool PlusMinus()
        {
            return Mark == 128 || Mark == 256;
        }

        /// <summary>"5", "5+" or null.</summary>
        public string GradeText()
        {
            if (Grade == 0 && !PlusMinus()) return null;
            string text = Grade != 0 ? Grade.ToString() : "";
            return PlusMinus() ? text + MarkSymbol(Mark) : text;
        }

        /// <summary>"Н" etc. or null (only marks that are not a +/- next to a grade).</summary>
        public string MarkText()
        {
            return Mark != 0 && !PlusMinus() ? MarkSymbol(Mark) : null;
        }

        /// <summary>"5", "+", "Н" or null.</summary>
        public string BadgeText()
        {
            var g = GradeText();
            return g ?? MarkText();
        }

        public bool HasGrade()
        {
            return BadgeText() != null;
        }

        public bool HasTaskContent()
        {
            return HasHomework || NotEmpty(Task) || NotEmpty(Webinar) || Files.Count > 0;
        }

        public bool IsIndependent()
        {
            return PairTypeId == 9 || (PairTypeName != null && PairTypeName.ToLowerInvariant().Contains("самосто"));
        }

        /// <summary>"1 подгруппа" or null.</summary>
        public string SubgroupText()
        {
            string s = Week.PersonalSubgroup(Subgroup);
            if (s == "left")  return "1 подгруппа";
            if (s == "right") return "2 подгруппа";
            return null;
        }

        /// <summary>"Иванова А. П." or null.</summary>
        public string ShortTeacher()
        {
            if (Teacher == null) return null;
            var sb = new StringBuilder();
            foreach (var part in Teacher.Trim().Split(new char[]{' ','\t'}, StringSplitOptions.RemoveEmptyEntries))
            {
                if (sb.Length == 0) sb.Append(part);
                else sb.Append(' ').Append(part[0]).Append('.');
            }
            return sb.Length > 0 ? sb.ToString() : null;
        }

        /// <summary>"18А (IT)" or null.</summary>
        public string HallText()
        {
            if (!NotEmpty(Hall)) return null;
            return NotEmpty(Building) ? Hall + " (" + Building + ")" : Hall;
        }

        public static bool NotEmpty(string s)
        {
            return s != null && s.Length > 0;
        }
    }

    public sealed class Day
    {
        public string Date; // yyyy-MM-dd
        public List<Lesson> Lessons = new List<Lesson>();
    }

    public sealed class Week
    {
        public string     WeekStart = "";
        public List<Day>  Days      = new List<Day>();
        public bool       Teacher;
        public long       UpdatedAt; // ms since epoch, 0 = unknown
        public bool       Offline;   // shown from cache

        public bool IsNull()
        {
            return Days.Count == 0;
        }

        public List<Lesson> AllLessons()
        {
            var all = new List<Lesson>();
            foreach (var d in Days) all.AddRange(d.Lessons);
            return all;
        }

        public static string PersonalSubgroup(string value)
        {
            if (string.IsNullOrEmpty(value)) return null;
            string clean = value.ToLowerInvariant().Replace(" ", "").Replace("_", "").Replace("-", "");
            if (clean == "1" || clean == "left" || clean == "first" || clean == "one"
                    || clean == "первая" || clean == "первый" || clean == "1я" || clean == "1ая"
                    || clean == "1ый" || clean == "1подгруппа" || clean == "1яподгруппа"
                    || clean == "1аяподгруппа" || clean == "подгруппа1")
            {
                return "left";
            }
            if (clean == "2" || clean == "right" || clean == "second" || clean == "two"
                    || clean == "вторая" || clean == "второй" || clean == "2я" || clean == "2ая"
                    || clean == "2ой" || clean == "2подгруппа" || clean == "2яподгруппа"
                    || clean == "2аяподгруппа" || clean == "подгруппа2")
            {
                return "right";
            }
            return null;
        }

        public static string InferSubgroup(List<Day> days)
        {
            string found = null;
            foreach (var day in days)
            {
                foreach (var lesson in day.Lessons)
                {
                    string sub = PersonalSubgroup(lesson.Subgroup);
                    if (!string.IsNullOrEmpty(sub))
                    {
                        if (found == null)
                        {
                            found = sub;
                        }
                        else if (found != sub)
                        {
                            return null;
                        }
                    }
                }
            }
            return found;
        }

        public static bool MatchesSubgroup(Lesson lesson, string selectedSubgroup, bool teacher)
        {
            if (teacher || string.IsNullOrEmpty(selectedSubgroup) || selectedSubgroup == "all")
                return true;
            string sub = PersonalSubgroup(lesson.Subgroup);
            return string.IsNullOrEmpty(sub) || sub == selectedSubgroup;
        }

        public static List<Lesson> VisibleLessons(List<Lesson> lessons, string selectedSubgroup, bool teacher)
        {
            if (teacher || string.IsNullOrEmpty(selectedSubgroup) || selectedSubgroup == "all")
                return lessons;
            var outList = new List<Lesson>();
            foreach (var l in lessons)
            {
                if (MatchesSubgroup(l, selectedSubgroup, teacher))
                    outList.Add(l);
            }
            return outList;
        }

        /// <summary>payload: {weekStart, week: decoded server answer, teacher,
        /// extras: {journalId: {homework, webinar}}, savedAt}.
        /// filter applies the bot's rule: a student's day of distance pairs only is shown when any
        /// pair has a webinar or a task. Returns null when the answer has no days.</summary>
        public static Week FromPayload(Dictionary<string, object> payload, bool filter)
        {
            var root = Json.MapAt(payload, "week");
            if (root == null || !(root.ContainsKey("days"))) return null;

            bool teacher   = Json.Bool(payload.ContainsKey("teacher") ? payload["teacher"] : null);
            string wStart  = Json.Str(payload.ContainsKey("weekStart") ? payload["weekStart"] : null) ?? "";
            var extras     = Json.MapAt(payload, "extras");
            var references = Json.MapAt(root, "references");

            // Teachers: id -> "Surname Name Patronymic"
            var teachers = new Dictionary<string, string>();
            foreach (var item in Json.ListAt(references, "teachers"))
            {
                var t  = Json.Map(item);
                var id = Json.Str(t != null && t.ContainsKey("id") ? t["id"] : null);
                if (id == null) continue;
                var sb = new StringBuilder();
                foreach (var key in new[]{"surname","name","patronymic"})
                {
                    var part = Json.Text(t.ContainsKey(key) ? t[key] : null);
                    if (part != null) { if (sb.Length > 0) sb.Append(' '); sb.Append(part); }
                }
                teachers[id] = sb.ToString();
            }

            // Halls: id -> (number, building, remote)
            var halls = new Dictionary<long, object[]>();
            foreach (var item in Json.ListAt(references, "auditoriums"))
            {
                var a  = Json.Map(item);
                long? id = Json.Key(a != null && a.ContainsKey("auditoriumId") ? a["auditoriumId"] : null);
                if (id == null) continue;
                string code = Json.Text(a.ContainsKey("code") ? a["code"] : null) ?? "";
                bool virt = Json.Str(a.ContainsKey("variant") ? a["variant"] : null) == "virtual"
                         || code == "Д";
                string bld = virt ? "" : (Json.Text(a.ContainsKey("buildingShortCode") ? a["buildingShortCode"] : null) ?? "");
                halls[id.Value] = new object[]{ virt ? "Дистанционно" : code, bld, virt };
            }

            // Pair types: id -> name
            var types = new Dictionary<long, string>();
            foreach (var item in Json.ListAt(references, "pairTypes"))
            {
                var pt = Json.Map(item);
                long? id = Json.Key(pt != null && pt.ContainsKey("id") ? pt["id"] : null);
                if (id != null)
                    types[id.Value] = Json.Str(pt.ContainsKey("name") ? pt["name"] : null) ?? "";
            }

            // Bell presets: id -> list of {start, end} minutes by pair number (1-based)
            var presets = new Dictionary<long, List<int[]>>();
            foreach (var item in Json.ListAt(root, "callPresets"))
            {
                var cp = Json.Map(item);
                long? id = Json.Key(cp != null && cp.ContainsKey("id") ? cp["id"] : null);
                if (id == null) continue;
                int minutes = BeginMinutes(Json.Str(cp.ContainsKey("beginTime") ? cp["beginTime"] : null));
                var times = new List<int[]>();
                foreach (var stepItem in Json.ListAt(cp, "steps"))
                {
                    var step = Json.Map(stepItem);
                    int dur   = (int)Json.Number(step != null && step.ContainsKey("duration") ? step["duration"] : null, 0);
                    int pause = (int)Json.Number(step != null && step.ContainsKey("breakDuration") ? step["breakDuration"] : null, 0);
                    times.Add(new int[]{ minutes, minutes + dur });
                    minutes += dur + pause;
                }
                presets[id.Value] = times;
            }

            // Bells: date -> preset id
            var bells = new Dictionary<string, long>();
            foreach (var scopeItem in Json.ListAt(root, "bellScopes"))
            {
                foreach (var assignItem in Json.ListAt(Json.Map(scopeItem), "assignments"))
                {
                    var asgn = Json.Map(assignItem);
                    string date = Json.Str(asgn != null && asgn.ContainsKey("date") ? asgn["date"] : null);
                    long? preset = Json.Key(asgn != null && asgn.ContainsKey("presetId") ? asgn["presetId"] : null);
                    if (date != null && date.Length >= 10 && preset != null)
                        bells[date.Substring(0, 10)] = preset.Value;
                }
            }

            var week = new Week { WeekStart = wStart, Teacher = teacher };
            foreach (var dayItem in Json.List(root["days"]) ?? new List<object>())
            {
                var d    = Json.Map(dayItem);
                string date = Json.Str(d != null && d.ContainsKey("date") ? d["date"] : null);
                if (string.IsNullOrEmpty(date)) continue;
                long? presetId = date.Length >= 10 && bells.ContainsKey(date.Substring(0, 10))
                                 ? bells[date.Substring(0, 10)] : (long?)null;
                List<int[]> times = presetId.HasValue && presets.ContainsKey(presetId.Value)
                                    ? presets[presetId.Value] : null;

                var lessons = new List<Lesson>();
                var builder = new Builder(date, times, teachers, halls, types, extras, wStart, lessons);

                // new-style pages layout
                foreach (var pageItem in Json.ListAt(d, "pages"))
                    foreach (var pairItem in Json.ListAt(Json.Map(pageItem), "pairs"))
                    {
                        var p = Json.Map(pairItem);
                        if (p != null)
                            builder.Add((int)Json.Number(p.ContainsKey("pairNumber") ? p["pairNumber"] : null, 0),
                                        p.ContainsKey("lesson") ? p["lesson"] : null, null);
                    }

                // legacy pairs layout
                foreach (var pairItem in Json.ListAt(d, "pairs"))
                {
                    var p = Json.Map(pairItem);
                    if (p == null) continue;
                    int pair = (int)Json.Number(p.ContainsKey("pairNumber") ? p["pairNumber"] : null, 0);
                    var ordinary = Json.MapAt(p, "ordinary");
                    string[][] lists = {
                        new[]{"wholeLessons","middle"}, new[]{"leftLessons","left"}, new[]{"rightLessons","right"}
                    };
                    foreach (var lst in lists)
                        foreach (var raw in Json.ListAt(ordinary, lst[0]))
                            builder.Add(pair, raw, lst[1]);
                    foreach (var dirItem in Json.ListAt(p, "directions"))
                        foreach (var raw in Json.ListAt(Json.Map(dirItem), "lessons"))
                            builder.Add(pair, raw, null);
                }

                lessons.Sort((a, b) => a.Pair.CompareTo(b.Pair));

                var day = new Day
                {
                    Date    = date.Substring(0, Math.Min(date.Length, 10)),
                    Lessons = lessons
                };
                week.Days.Add(day);
            }
            if (week.Days.Count == 0) return null;
            week.UpdatedAt = Json.Number(payload.ContainsKey("savedAt") ? payload["savedAt"] : null, 0);
            return week;
        }

        // ---------------------------------------------------------------- helpers

        private static int BeginMinutes(string begin)
        {
            if (begin == null) return 0;
            int t = begin.IndexOf('T');
            string time = t < 0 ? begin : begin.Substring(t + 1);
            var parts = time.Split(':');
            return LeadingInt(parts.Length > 0 ? parts[0] : "") * 60
                 + LeadingInt(parts.Length > 1 ? parts[1] : "");
        }

        private static int LeadingInt(string s)
        {
            int value = 0;
            for (int i = 0; i < s.Length && char.IsDigit(s[i]) && i < 9; i++)
                value = value * 10 + (s[i] - '0');
            return value;
        }

        private static long PositiveId(object value)
        {
            long n = 0;
            if (value is long) n = (long)value;
            else if (value is double) n = (long)(double)value;
            else if (value is int) n = (int)value;
            else if (value is string)
            {
                string s = (string)value;
                long lp;
                if (long.TryParse(s.Trim(), out lp)) n = lp;
            }
            return n > 0 ? n : 0;
        }

        // ---------------------------------------------------------------- builder

        private sealed class Builder
        {
            private readonly string _date;
            private readonly List<int[]> _times;
            private readonly Dictionary<string, string> _teachers;
            private readonly Dictionary<long, object[]> _halls;
            private readonly Dictionary<long, string> _types;
            private readonly Dictionary<string, object> _extras;
            private readonly string _weekStart;
            private readonly List<Lesson> _out;

            public Builder(string date, List<int[]> times,
                Dictionary<string, string> teachers,
                Dictionary<long, object[]> halls,
                Dictionary<long, string> types,
                Dictionary<string, object> extras,
                string weekStart, List<Lesson> out_)
            {
                _date = date; _times = times; _teachers = teachers;
                _halls = halls; _types = types; _extras = extras;
                _weekStart = weekStart; _out = out_;
            }

            public void Add(int pair, object raw, string fallbackSubgroup)
            {
                var l = Json.Map(raw);
                string discipline = Json.Str(l != null && l.ContainsKey("disciplineShortName") ? l["disciplineShortName"] : null);
                if (string.IsNullOrEmpty(discipline)) return;

                var lesson = new Lesson
                {
                    Date           = _date,
                    Pair           = pair,
                    Discipline     = discipline,
                    DisciplineFull = Json.Text(l.ContainsKey("disciplineFullName") ? l["disciplineFullName"] : null),
                };
                string teacherId = Json.Str(l.ContainsKey("teacherId") ? l["teacherId"] : null);
                if (teacherId != null && _teachers.ContainsKey(teacherId))
                    lesson.Teacher = _teachers[teacherId];
                lesson.GroupId   = Json.Str(l.ContainsKey("groupId") ? l["groupId"] : null);
                string subgroup  = Json.Text(l.ContainsKey("subgroup") ? l["subgroup"] : null);
                lesson.Subgroup  = subgroup ?? fallbackSubgroup;

                long? hallId = Json.Key(l.ContainsKey("auditoriumId") ? l["auditoriumId"] : null);
                if (hallId.HasValue && _halls.ContainsKey(hallId.Value))
                {
                    var h = _halls[hallId.Value];
                    lesson.Hall     = ((string)h[0]).Length > 0 ? (string)h[0] : null;
                    lesson.Building = ((string)h[1]).Length > 0 ? (string)h[1] : null;
                    lesson.Remote   = (bool)h[2];
                }
                else if (hallId.HasValue && hallId.Value > 0)
                    lesson.Hall = hallId.Value.ToString();

                lesson.PairTypeId = Json.Number(l.ContainsKey("pairTypeId") ? l["pairTypeId"] : null, 0);
                if (_types.ContainsKey(lesson.PairTypeId))
                    lesson.PairTypeName = _types[lesson.PairTypeId];
                lesson.AcademicHours = (int)Json.Number(l.ContainsKey("academicHours") ? l["academicHours"] : null, 2);
                lesson.JournalId     = Json.Number(l.ContainsKey("journalId") ? l["journalId"] : null, 0);

                var assessment = Json.MapAt(l, "assessment");
                lesson.Grade = (int)Json.Number(assessment != null && assessment.ContainsKey("grade") ? assessment["grade"] : null, 0);
                lesson.Mark  = (int)Json.Number(assessment != null && assessment.ContainsKey("mark")  ? assessment["mark"]  : null, 0);
                lesson.HasHomework = Json.Bool(l.ContainsKey("hasHomework") ? l["hasHomework"] : null);

                if (_times != null && pair >= 1 && pair <= _times.Count)
                {
                    lesson.StartMinute = _times[pair - 1][0];
                    lesson.EndMinute   = _times[pair - 1][1];
                }

                ApplyExtras(
                    _extras != null && _extras.ContainsKey(lesson.JournalId.ToString())
                        ? Json.Map(_extras[lesson.JournalId.ToString()])
                        : null,
                    lesson, _weekStart);

                _out.Add(lesson);
            }

            private static bool Restricted(Dictionary<string, object> value)
            {
                return value != null && value.ContainsKey("status")
                       && Json.Str(value["status"]) != "available";
            }

            private static void ApplyExtras(Dictionary<string, object> extra, Lesson lesson, string weekStart)
            {
                if (extra == null) return;

                var hwCall = Json.MapAt(extra, "homework");
                var hw     = Json.MapAt(hwCall, "homework");
                if (hw != null && !Restricted(hwCall))
                {
                    lesson.Task     = Json.Text(hw.ContainsKey("task")     ? hw["task"]     : null);
                    lesson.Deadline = Json.Text(hw.ContainsKey("deadline") ? hw["deadline"] : null);
                    const int MaxFiles = 64;
                    foreach (var item in Json.ListAt(hw, "files"))
                    {
                        if (lesson.Files.Count >= MaxFiles) break;
                        var f = Json.Map(item);
                        if (f == null) continue;
                        long fileId = PositiveId(f.ContainsKey("userFileId") ? f["userFileId"] : null);
                        if (fileId == 0 || Json.Str(f.ContainsKey("availability") ? f["availability"] : null) != "available")
                            continue;
                        string caption   = Json.Text(f.ContainsKey("caption")    ? f["caption"]    : null) ?? "";
                        string extension = Json.Text(f.ContainsKey("extension")  ? f["extension"]  : null) ?? "";
                        lesson.Files.Add(new TaskFile
                        {
                            FileId     = fileId,
                            JournalId  = lesson.JournalId,
                            Caption    = caption,
                            Extension  = extension,
                            WeekStart  = weekStart
                        });
                    }
                }

                var wbCall = Json.MapAt(extra, "webinar");
                if (wbCall != null && !Restricted(wbCall))
                    lesson.Webinar = Json.Text(wbCall.ContainsKey("webinar") ? wbCall["webinar"] : null);
            }
        }
    }
}
