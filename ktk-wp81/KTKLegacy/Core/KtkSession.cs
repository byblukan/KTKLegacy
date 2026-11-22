// KTK for Windows Phone 8.1 - the site session (cookies) in IsolatedStorage; password never stored.
// Port of ktk-android Session.java.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.IsolatedStorage;

namespace KTKLegacy
{
    public sealed class KtkSession
    {
        public Dictionary<string, string> Cookies     = new Dictionary<string, string>();
        public string SessionId  = "";
        public string Login      = "";
        public bool   Student    = true;
        public long   Group;
        public string TeacherHash = "";
        public string Branch      = "";
        public string PersonalSubgroup = "";
        public string Subgroup         = "";

        // ---------------------------------------------------------------- persistence

        private const string FileName = "ktk-session.json";

        public static KtkSession Load()
        {
            try
            {
                using (var store = IsolatedStorageFile.GetUserStoreForApplication())
                {
                    if (!store.FileExists(FileName)) return null;
                    string json;
                    using (var stream = store.OpenFile(FileName, FileMode.Open, FileAccess.Read))
                    using (var reader = new StreamReader(stream))
                        json = reader.ReadToEnd();
                    var d = Json.Map(Json.Parse(json));
                    if (d == null) return null;
                    var cookieMap = Json.MapAt(d, "cookies");
                    if (cookieMap == null || cookieMap.Count == 0) return null;
                    var session = new KtkSession();
                    foreach (var kv in cookieMap)
                    {
                        if (kv.Value is string)
                            session.Cookies[kv.Key] = (string)kv.Value;
                    }
                    if (session.Cookies.Count == 0) return null;
                    session.SessionId   = OrEmpty(Json.Str(d.ContainsKey("sessionId")   ? d["sessionId"]   : null));
                    session.Login       = OrEmpty(Json.Str(d.ContainsKey("login")       ? d["login"]       : null));
                    bool studentBool = true;
                    if (d.ContainsKey("student") && d["student"] is bool)
                        studentBool = (bool)d["student"];
                    session.Student     = studentBool;
                    session.Group       = Json.Number(d.ContainsKey("group") ? d["group"] : null, 0);
                    session.TeacherHash = OrEmpty(Json.Str(d.ContainsKey("teacherHash") ? d["teacherHash"] : null));
                    session.Branch      = OrEmpty(Json.Str(d.ContainsKey("branch")      ? d["branch"]      : null));
                    // "" is the canonical "unknown / not chosen" value. LoadWeek infers
                    // personalSubgroup from the first personal week; an empty subgroup shows all.
                    session.PersonalSubgroup = OrEmpty(Json.Str(d.ContainsKey("personalSubgroup") ? d["personalSubgroup"] : null));
                    session.Subgroup         = OrEmpty(Json.Str(d.ContainsKey("subgroup")         ? d["subgroup"]         : null));
                    return session;
                }
            }
            catch { return null; }
        }

        public void Save()
        {
            try
            {
                var d = new Dictionary<string, object>();
                d["cookies"]     = ToDictObj(Cookies);
                d["sessionId"]   = SessionId;
                d["login"]       = Login;
                d["student"]     = Student;
                d["group"]       = Group;
                d["teacherHash"] = TeacherHash;
                d["branch"]      = Branch;
                d["personalSubgroup"] = PersonalSubgroup;
                d["subgroup"]         = Subgroup;

                using (var store = IsolatedStorageFile.GetUserStoreForApplication())
                using (var stream = store.OpenFile(FileName, FileMode.Create, FileAccess.Write))
                using (var writer = new StreamWriter(stream))
                    writer.Write(Json.Serialize(d));
            }
            catch { /* session lasts until the app closes */ }
        }

        public static void Clear()
        {
            try
            {
                using (var store = IsolatedStorageFile.GetUserStoreForApplication())
                    if (store.FileExists(FileName)) store.DeleteFile(FileName);
            }
            catch { }
        }

        private static string OrEmpty(string s)
        {
            return s ?? "";
        }

        private static Dictionary<string, object> ToDictObj(Dictionary<string, string> src)
        {
            var d = new Dictionary<string, object>();
            foreach (var kv in src) d[kv.Key] = kv.Value;
            return d;
        }
    }
}
