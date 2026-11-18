// KTK for Windows Phone 8.1 - sign-in, the week with tasks and webinars, files, sign-out,
// and finding server functions again after a site deploy.
// Port of ktk-android Client.java.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.IsolatedStorage;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace KTKLegacy
{
    public sealed class WeekResult
    {
        public readonly Week Week;
        public readonly KtkException Error;
        public WeekResult(Week week, KtkException error) { Week = week; Error = error; }
    }

    public sealed class FileResult
    {
        public readonly string Path;
        public readonly KtkException Error;
        public FileResult(string path, KtkException error) { Path = path; Error = error; }
    }

    sealed class TextResult
    {
        public readonly string Text;
        public readonly Uri FinalUri;
        public TextResult(string text, Uri finalUri) { Text = text; FinalUri = finalUri; }
    }

    public sealed class KtkClient
    {
        public const string Base       = "https://ktk.education-system.ru";
        private const string DeviceName = "ktk-wp81";
        private const string UserAgent  =
            "Mozilla/5.0 (Windows Phone 8.1; ARM; Trident/7.0; Touch; rv:11.0; IEMobile/11.0) KTKLegacy/1.0";

        private const int JsonLimit  = KtkHttp.JsonLimit;
        private const int FileLimit  = KtkHttp.FileLimit;
        private const int MaxExtras  = 20; // lessons per week whose task/webinar is fetched
        private const int MaxScripts = 64;

        private static readonly Regex WeekStartRx = new Regex(@"^\d{4}-\d{2}-\d{2}$");

        // ---------------------------------------------------------------- server function table

        public enum Fn { SignIn, Student, Teacher, Group, Logout, StudentHomework, StudentWebinar }

        private struct FnInfo { public string Id; public string Hash; }

        private static readonly FnInfo[] Functions = {
            new FnInfo { Id = "signIn",          Hash = "9cd5b48fd0588913cb64c83d8ae2b42d5f3e97ed5dd47dc10f211cdc2322c1f6" },
            new FnInfo { Id = "student",         Hash = "85d2195b8f64b023a2c16160b65ef47139dea3a839096c4b75b3888cda49f16c" },
            new FnInfo { Id = "teacher",         Hash = "9fd2616a2ecddd56f79974f2111a13f2c9d89265cb6832c8c9e7ae2939e56f4a" },
            new FnInfo { Id = "group",           Hash = "d418f54f8d5ba132bd168b1792cc8f0efbff634c7861f6aa66890cb9f7103c05" },
            new FnInfo { Id = "logout",          Hash = "2e1ca1208f926608597adee62b6f7a786ebfd7e01b208963ecb9bee6e800f9f5" },
            new FnInfo { Id = "studentHomework", Hash = "6856a131ea327da0ae2769d0e8b9950bce030194dda15acd331dd5ad2c273f38" },
            new FnInfo { Id = "studentWebinar",  Hash = "b218948fba15961755e1cf6c0742e352d8709719e99ac7bd3e06315787e13650" },
        };

        private static string DefaultPath(Fn fn)
        {
            return "/_serverFn/" + Functions[(int)fn].Hash;
        }

        // ---------------------------------------------------------------- state

        private KtkSession _session;
        private Dictionary<string, string> _jar = new Dictionary<string, string>();
        private readonly object _sessionLock = new object();

        // Persisted server-function paths (may be updated after rediscovery).
        private Dictionary<string, string> _paths = new Dictionary<string, string>();
        private readonly object _pathsLock = new object();

        // One-time crawl result per launch.
        private Dictionary<string, string> _discovered;
        private readonly SemaphoreSlim _crawlLock = new SemaphoreSlim(1, 1);

        // The week on screen (held for the lesson detail page).
        private Week _shown;

        private static KtkClient _shared;
        private static readonly object _sharedLock = new object();
        public static KtkClient Shared
        {
            get
            {
                lock (_sharedLock)
                {
                    if (_shared == null) _shared = new KtkClient();
                    return _shared;
                }
            }
        }

        private KtkClient()
        {
            _session = KtkSession.Load();
            if (_session != null)
                _jar = new Dictionary<string, string>(_session.Cookies);
            LoadPaths();
        }

        // ---------------------------------------------------------------- session accessors

        public KtkSession Session { get { lock (_sessionLock) return _session; } }
        public bool HasSession    { get { lock (_sessionLock) return _session != null; } }

        public void SetShownWeek(Week week)  { _shown = week; }
        public Week ShownWeek(string weekStart)
        {
            var w = _shown;
            return w != null && w.WeekStart == weekStart ? w : CachedWeek(weekStart);
        }

        // ---------------------------------------------------------------- paths (persisted)

        private const string PathsFile = "ktk-paths.json";

        private void LoadPaths()
        {
            try
            {
                using (var store = IsolatedStorageFile.GetUserStoreForApplication())
                {
                    if (!store.FileExists(PathsFile)) return;
                    string json;
                    using (var stream = store.OpenFile(PathsFile, FileMode.Open, FileAccess.Read))
                    using (var reader = new StreamReader(stream))
                        json = reader.ReadToEnd();
                    var d = Json.Map(Json.Parse(json));
                    if (d == null) return;
                    lock (_pathsLock)
                    {
                        foreach (var kv in d)
                        {
                            if (kv.Value is string)
                            {
                                string sv = (string)kv.Value;
                                if (sv.StartsWith("/_serverFn/"))
                                    _paths[kv.Key] = sv;
                            }
                        }
                    }
                }
            }
            catch { }
        }

        private void SavePaths()
        {
            try
            {
                Dictionary<string, object> d;
                lock (_pathsLock) d = DictStrToObj(_paths);
                using (var store = IsolatedStorageFile.GetUserStoreForApplication())
                using (var stream = store.OpenFile(PathsFile, FileMode.Create, FileAccess.Write))
                using (var writer = new StreamWriter(stream))
                    writer.Write(Json.Serialize(d));
            }
            catch { }
        }

        private string PathFor(Fn fn)
        {
            lock (_pathsLock)
            {
                string id = Functions[(int)fn].Id;
                string p;
                if (_paths.TryGetValue(id, out p) && p.StartsWith("/_serverFn/")) return p;
                return DefaultPath(fn);
            }
        }

        private void SetPath(Fn fn, string path)
        {
            lock (_pathsLock) _paths[Functions[(int)fn].Id] = path;
            SavePaths();
        }

        // ---------------------------------------------------------------- fingerprint

        private static string Fingerprint(string login)
        {
            using (var sha = new SHA256Managed())
            {
                byte[] data = Encoding.UTF8.GetBytes(login + DeviceName);
                byte[] hash = sha.ComputeHash(data);
                var sb = new StringBuilder(32);
                for (int i = 0; i < 16; i++) sb.AppendFormat("{0:x2}", hash[i]);
                return sb.ToString();
            }
        }

        // ---------------------------------------------------------------- in-band server errors

        private static KtkException ServerError(object value)
        {
            var map = Json.Map(value);
            object errObj;
            if (map == null || !map.TryGetValue("error", out errObj) || errObj == null) return null;
            var details = Json.MapAt(Json.Map(errObj), "appError") ?? Json.Map(errObj);
            string code = "";
            string type = "";
            string msg = null;
            if (details != null)
            {
                if (details.ContainsKey("code"))
                {
                    string s = Json.Str(details["code"]);
                    if (s != null) code = s.ToLowerInvariant();
                }
                if (details.ContainsKey("type"))
                {
                    string s = Json.Str(details["type"]);
                    if (s != null) type = s.ToLowerInvariant();
                }
                if (details.ContainsKey("message"))
                {
                    msg = Json.Str(details["message"]);
                }
            }
            foreach (var word in new[]{"auth","session","unauthor","forbidden","login"})
                if (code.Contains(word) || type.Contains(word))
                    return new KtkException(KtkErrorCode.LoginRequired, null);
            return new KtkException(KtkErrorCode.Server, msg ?? Json.Str(errObj));
        }

        private static bool ShouldRediscover(KtkException e)
        {
            return e.Code == KtkErrorCode.BadResponse
                || (e.Code == KtkErrorCode.HttpStatus && (e.Status == 404 || e.Status == 410));
        }

        // ---------------------------------------------------------------- cookie jar

        private Dictionary<string, string> CurrentJar()
        {
            lock (_sessionLock) return new Dictionary<string, string>(_jar);
        }

        private void AbsorbCookies(List<string> lines, Dictionary<string, string> jar)
        {
            bool changed = false;
            foreach (string line in lines)
            {
                var parts = line.Split(';');
                string pair = parts[0].Trim();
                int eq = pair.IndexOf('=');
                if (eq <= 0) continue;
                string name  = pair.Substring(0, eq).Trim();
                string val   = pair.Substring(eq + 1).Trim();
                bool expired = val.Length == 0;
                for (int i = 1; i < parts.Length; i++)
                {
                    string attr = parts[i].Trim().ToLowerInvariant();
                    if (attr.StartsWith("max-age="))
                    {
                        long maxAge;
                        if (long.TryParse(attr.Substring(8).Trim(), out maxAge))
                            expired |= maxAge <= 0;
                    }
                }
                lock (jar)
                {
                    if (expired)       { changed |= jar.Remove(name); }
                    else if (!jar.ContainsKey(name) || jar[name] != val)
                    { jar[name] = val; changed = true; }
                }
            }
            if (!changed) return;
            lock (_sessionLock)
            {
                if (_session != null)
                {
                    lock (jar) _session.Cookies = new Dictionary<string, string>(jar);
                    _session.Save();
                }
            }
        }

        // ---------------------------------------------------------------- low-level HTTP

        private KtkHttp.Request MakeRequest(string url, Dictionary<string, string> jar)
        {
            var req = new KtkHttp.Request { Url = url };
            req.Headers["User-Agent"]                   = UserAgent;
            req.Headers["Origin"]                       = Base;
            req.Headers["Referer"]                      = Base + "/";
            req.Headers["sec-fetch-site"]               = "same-origin";
            req.Headers["sec-fetch-mode"]               = "cors";
            req.Headers["sec-fetch-dest"]               = "empty";
            if (jar != null)
            {
                lock (jar)
                {
                    if (jar.Count > 0)
                    {
                        var sb = new StringBuilder();
                        foreach (var kv in jar)
                        {
                            if (sb.Length > 0) sb.Append("; ");
                            sb.Append(kv.Key).Append('=').Append(kv.Value);
                        }
                        req.Headers["Cookie"] = sb.ToString();
                    }
                }
            }
            return req;
        }

        private async Task<KtkHttp.Response> Send(KtkHttp.Request request, bool idempotent)
        {
            int attempts = idempotent ? 3 : 1;
            for (int attempt = 1; ; attempt++)
            {
                KtkHttp.Response response = null;
                KtkException failure = null;
                try { response = await KtkHttp.Perform(request); }
                catch (KtkException e) { failure = e; }

                bool retry = attempt < attempts
                          && ((failure != null && failure.Code == KtkErrorCode.Network)
                           || (response != null && response.Status >= 500));
                if (!retry)
                {
                    if (failure != null) throw failure;
                    return response;
                }
                await Task.Delay(200 * attempt);
            }
        }

        // ---------------------------------------------------------------- server function call

        private async Task<object> CallOnce(Fn fn, string path, IList<KeyValuePair<string, object>> fields,
            Dictionary<string, string> jar)
        {
            string payload = Seroval.Request(fields);
            bool post = fn == Fn.SignIn || fn == Fn.Logout;
            string url = Base + path;
            if (!post) url += "?payload=" + KtkHttp.Escape(payload);

            var req = MakeRequest(url, jar);
            if (post)
            {
                req.Method = "POST";
                req.Body   = Encoding.UTF8.GetBytes(payload);
                req.Headers["Content-Type"] = "application/json";
            }
            if (fn == Fn.SignIn)
                req.Headers["Referer"] = Base + "/welcome?redirect=%2F";
            req.Headers["x-tsr-serverfn"]                = "true";
            req.Headers["x-application-storage-mode"]    = "ordinary";
            req.Headers["Accept"]                        = "application/x-tss-framed, application/x-ndjson, application/json";
            req.Limit     = JsonLimit;
            req.TimeoutMs = 60000;

            var resp = await Send(req, !post);
            AbsorbCookies(resp.SetCookies, jar);

            if (fn != Fn.SignIn && (resp.Status == 401 || resp.Status == 403))
                throw new KtkException(KtkErrorCode.LoginRequired, null);
            if (resp.Status != 200)
                throw KtkException.ForStatus(resp.Status);

            object raw;
            try { raw = Json.Parse(Encoding.UTF8.GetString(resp.Body, 0, resp.Body.Length)); }
            catch { throw new KtkException(KtkErrorCode.BadResponse, null); }
            if (raw == null) throw new KtkException(KtkErrorCode.BadResponse, null);
            return Seroval.Decode(raw);
        }

        private async Task<object> Call(Fn fn, IList<KeyValuePair<string, object>> fields,
            Dictionary<string, string> jar = null)
        {
            if (jar == null) jar = CurrentJar();
            string path = PathFor(fn);
            KtkException first = null;
            try
            {
                return await CallOnce(fn, path, fields, jar);
            }
            catch (KtkException ex)
            {
                first = ex;
            }

            if (!ShouldRediscover(first)) throw first;
            string found = await Rediscover(fn);
            if (found == null || found == path) throw first;
            SetPath(fn, found);
            return await CallOnce(fn, found, fields, jar);
        }

        private async Task<object> QuietCall(Fn fn, IList<KeyValuePair<string, object>> fields)
        {
            try { return await Call(fn, fields); }
            catch { return null; }
        }

        // ---------------------------------------------------------------- sign in / out

        /// <summary>Signs in. Returns the new session on success, throws KtkException on failure.</summary>
        public async Task<KtkSession> SignIn(string rawLogin, string rawPassword)
        {
            string login    = rawLogin.Trim();
            string password = rawPassword; // as typed
            var jar = new Dictionary<string, string>();
            var fields = new List<KeyValuePair<string, object>>
            {
                new KeyValuePair<string, object>("collegeId",   login),
                new KeyValuePair<string, object>("password",    password),
                new KeyValuePair<string, object>("fingerprint", Fingerprint(login)),
            };
            var value = await Call(Fn.SignIn, fields, jar);
            var map = Json.Map(value);
            if (map == null) throw new KtkException(KtkErrorCode.BadResponse, null);

            var user = Json.MapAt(map, "user");
            if (user == null)
            {
                var failure = Json.MapAt(map, "error");
                string msg = null;
                var appErr = Json.MapAt(failure, "appError");
                if (appErr != null && appErr.ContainsKey("message"))
                    msg = Json.Str(appErr["message"]);
                else if (failure != null && failure.ContainsKey("message"))
                    msg = Json.Str(failure["message"]);
                throw new KtkException(KtkErrorCode.Credentials, msg);
            }

            var session = new KtkSession();
            lock (jar) session.Cookies = new Dictionary<string, string>(jar);
            if (session.Cookies.Count == 0) throw new KtkException(KtkErrorCode.BadResponse, null);

            var sessionMap = Json.MapAt(map, "session") ?? Json.MapAt(user, "session");
            string sessionId = null;
            if (sessionMap != null && sessionMap.ContainsKey("id"))
                sessionId = Json.Str(sessionMap["id"]);
            session.SessionId   = sessionId ?? "";
            session.Login       = login;
            bool isStudent = true;
            if (user.ContainsKey("isStudent") && user["isStudent"] is bool)
                isStudent = (bool)user["isStudent"];
            session.Student     = isStudent;
            session.Group       = Json.Number(user.ContainsKey("group") ? user["group"] : null, 0);
            string hash         = Json.Str(user.ContainsKey("hash")   ? user["hash"]   : null);
            session.TeacherHash = session.Student ? "" : (hash ?? "teacher");
            session.Branch      = Json.Str(user.ContainsKey("branch") ? user["branch"] : null) ?? "";

            ForgetCaches();
            session.Save();
            lock (_sessionLock) { _session = session; _jar = new Dictionary<string, string>(session.Cookies); }
            return session;
        }

        public void SignOutLocally()
        {
            lock (_sessionLock) { _session = null; _jar = new Dictionary<string, string>(); }
            KtkSession.Clear();
            Task.Run(() => ForgetCaches());
        }

        public async Task SignOut()
        {
            KtkSession old;
            lock (_sessionLock) old = _session;
            SignOutLocally();
            if (old != null && old.SessionId.Length > 0)
            {
                var oldJar = new Dictionary<string, string>(old.Cookies);
                try
                {
                    await Call(Fn.Logout,
                        new[]{ new KeyValuePair<string, object>("id", (object)old.SessionId) },
                        oldJar);
                }
                catch { /* the local session is gone anyway */ }
            }
        }

        private void ForgetCaches()
        {
            DeleteIsoDir("ktk-weeks");
            DeleteIsoDir("ktk-files");
        }

        private static void DeleteIsoDir(string dir)
        {
            try
            {
                using (var store = IsolatedStorageFile.GetUserStoreForApplication())
                {
                    if (!store.DirectoryExists(dir)) return;
                    foreach (var f in store.GetFileNames(dir + "/*"))
                        try { store.DeleteFile(dir + "/" + f); } catch { }
                    try { store.DeleteDirectory(dir); } catch { }
                }
            }
            catch { }
        }

        // ---------------------------------------------------------------- weeks

        private string WeekCachePath(string weekStart)
        {
            return "ktk-weeks/" + weekStart + ".json";
        }

        public Week CachedWeek(string weekStart)
        {
            if (weekStart == null || !WeekStartRx.IsMatch(weekStart)) return null;
            try
            {
                using (var store = IsolatedStorageFile.GetUserStoreForApplication())
                {
                    string path = WeekCachePath(weekStart);
                    if (!store.FileExists(path)) return null;
                    string json;
                    using (var stream = store.OpenFile(path, FileMode.Open, FileAccess.Read))
                    using (var reader = new StreamReader(stream))
                        json = reader.ReadToEnd();
                    var payload = Json.Map(Json.Parse(json));
                    return payload != null ? Week.FromPayload(payload, true) : null;
                }
            }
            catch { return null; }
        }

        private void SaveWeekPayload(Dictionary<string, object> payload, string weekStart)
        {
            try
            {
                using (var store = IsolatedStorageFile.GetUserStoreForApplication())
                {
                    if (!store.DirectoryExists("ktk-weeks")) store.CreateDirectory("ktk-weeks");
                    string path  = WeekCachePath(weekStart);
                    string part  = path + ".part";
                    using (var stream = store.OpenFile(part, FileMode.Create, FileAccess.Write))
                    using (var writer = new StreamWriter(stream))
                        writer.Write(Json.Serialize(payload));
                    if (store.FileExists(path)) store.DeleteFile(path);
                    store.MoveFile(part, path);
                }
            }
            catch { }
        }

        private async Task<Dictionary<string, object>> FetchWeek(string weekStart, KtkSession session)
        {
            bool teacher = !session.Student;
            var fields = new[]{ new KeyValuePair<string, object>("weekStart", weekStart) };
            var value = await Call(teacher ? Fn.Teacher : Fn.Student, fields);
            var err = ServerError(value);
            if (err != null) throw err;
            var week = Json.Map(value);
            if (week == null || !week.ContainsKey("days"))
                throw new KtkException(KtkErrorCode.BadResponse, null);
            var payload = new Dictionary<string, object>();
            payload["weekStart"] = weekStart;
            payload["week"]      = week;
            payload["teacher"]   = teacher;
            payload["savedAt"]   = CollegeCalendar.CurrentUnixTimeMillis();
            if (!teacher) payload["extras"] = await Extras(payload, weekStart);
            return payload;
        }

        /// <summary>Tasks and webinars for lessons flagged hasHomework / remote; best effort, 3 at a time.</summary>
        private async Task<Dictionary<string, object>> Extras(Dictionary<string, object> payload, string weekStart)
        {
            var bare = Week.FromPayload(payload, false);
            var wanted = new List<Lesson>();
            var journals = new HashSet<long>();
            if (bare != null)
                foreach (var l in bare.AllLessons())
                {
                    if (wanted.Count >= MaxExtras) break;
                    if (l.JournalId > 0 && (l.HasHomework || l.Remote) && journals.Add(l.JournalId))
                        wanted.Add(l);
                }

            var extras = new Dictionary<string, object>();
            var sem = new SemaphoreSlim(3, 3);
            var tasks = new List<Task>();
            foreach (var lesson in wanted)
            {
                var capturedLesson = lesson;
                tasks.Add(Task.Run(async () =>
                {
                    await sem.WaitAsync();
                    try
                    {
                        var fields = new[]
                        {
                            new KeyValuePair<string, object>("weekStart", weekStart),
                            new KeyValuePair<string, object>("journalId", capturedLesson.JournalId),
                        };
                        var entry = new Dictionary<string, object>();
                        if (capturedLesson.HasHomework)
                        {
                            var v = await QuietCall(Fn.StudentHomework, fields);
                            if (Json.Map(v) != null && ServerError(v) == null) entry["homework"] = v;
                        }
                        if (capturedLesson.Remote)
                        {
                            var v = await QuietCall(Fn.StudentWebinar, fields);
                            if (Json.Map(v) != null && ServerError(v) == null) entry["webinar"] = v;
                        }
                        if (entry.Count > 0)
                            lock (extras) extras[capturedLesson.JournalId.ToString()] = entry;
                    }
                    finally { sem.Release(); }
                }));
            }
            await Task.WhenAll(tasks);
            lock (extras) return new Dictionary<string, object>(extras);
        }

        /// <summary>The week from the site, with tasks and files. When offline returns a cached copy
        /// (week.Offline=true) + the error. Returns (null, LoginRequired) on session expiry.</summary>
        public async Task<WeekResult> LoadWeek(string weekStart)
        {
            KtkSession session;
            lock (_sessionLock) session = _session;
            if (session == null || !WeekStartRx.IsMatch(weekStart ?? ""))
                return new WeekResult(null, new KtkException(KtkErrorCode.LoginRequired, null));

            KtkException error = null;
            Week week = null;
            try
            {
                var payload = await FetchWeek(weekStart, session);
                week = Week.FromPayload(payload, true);
                if (week != null) SaveWeekPayload(payload, weekStart);
                else error = new KtkException(KtkErrorCode.BadResponse, null);
            }
            catch (KtkException e) { error = e; }
            catch { error = new KtkException(KtkErrorCode.BadResponse, null); }

            if (week == null && (error == null || error.Code != KtkErrorCode.LoginRequired))
            {
                week = CachedWeek(weekStart);
                if (week != null) week.Offline = true;
            }
            if (error != null && error.Code == KtkErrorCode.LoginRequired)
            {
                lock (_sessionLock)
                {
                    if (_session == session) { _session = null; _jar = new Dictionary<string, string>(); }
                }
                KtkSession.Clear();
            }
            return new WeekResult(week, error);
        }

        // ---------------------------------------------------------------- files

        public string FilesDirectory
        {
            get { return "ktk-files"; }
        }

        public async Task<FileResult> DownloadFile(TaskFile file)
        {
            string dir     = FilesDirectory + "/" + file.FileId;
            string target  = dir + "/" + file.SafeName();
            using (var store = IsolatedStorageFile.GetUserStoreForApplication())
            {
                if (store.FileExists(target)) return new FileResult(target, null);
            }

            string partial = target + ".part";
            KtkException error = null;
            try
            {
                using (var store = IsolatedStorageFile.GetUserStoreForApplication())
                    if (!store.DirectoryExists(dir)) store.CreateDirectory(dir);

                var req = MakeRequest(Base + "/api/timetable/students/self/lessons/"
                    + file.JournalId + "/files/" + file.FileId
                    + "?weekStart=" + KtkHttp.Escape(file.WeekStart), CurrentJar());
                req.Headers.Remove("Origin");
                req.Headers["Accept"] = "*/*";
                req.Limit      = FileLimit;
                req.TimeoutMs  = 120000;
                req.OutputPath = partial;

                var resp = await Send(req, false);
                if (resp.Status == 401 || resp.Status == 403)
                    throw new KtkException(KtkErrorCode.LoginRequired, null);
                if (resp.Status != 200)
                    throw KtkException.ForStatus(resp.Status);

                using (var store = IsolatedStorageFile.GetUserStoreForApplication())
                {
                    if (store.FileExists(target)) store.DeleteFile(target);
                    store.MoveFile(partial, target);
                }
                return new FileResult(target, null);
            }
            catch (KtkException e) { error = e; }
            finally
            {
                try
                {
                    using (var store = IsolatedStorageFile.GetUserStoreForApplication())
                        if (store.FileExists(partial)) store.DeleteFile(partial);
                }
                catch { }
            }
            return new FileResult(null, error);
        }

        // ---------------------------------------------------------------- server function discovery

        private async Task<string> Rediscover(Fn fn)
        {
            if (fn == Fn.StudentHomework || fn == Fn.StudentWebinar) return null;
            await _crawlLock.WaitAsync();
            try
            {
                if (_discovered == null) _discovered = await Crawl();
                string id = Functions[(int)fn].Id;
                string found;
                return _discovered.TryGetValue(id, out found) ? found : null;
            }
            finally { _crawlLock.Release(); }
        }

        private static readonly Regex ScriptTagRx  = new Regex(@"<script[^>]+src=[""']([^""']+)[""']", RegexOptions.IgnoreCase | RegexOptions.Singleline);
        private static readonly Regex ScriptPathRx  = new Regex(@"[""']([^""']+\.js(?:\?[^""']*)?)[""']");
        private static readonly Regex ServerFnRx    = new Regex(@"/_serverFn/([0-9a-fA-F]{32,64})");

        private static List<string> ScriptUrls(string text, Uri baseUri)
        {
            var urls = new List<string>();
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var rx in new[]{ ScriptTagRx, ScriptPathRx })
                foreach (Match m in rx.Matches(text))
                {
                    Uri u;
                    if (Uri.TryCreate(baseUri, m.Groups[1].Value, out u)
                        && u.Scheme == "https" && u.Host == baseUri.Host
                        && seen.Add(u.ToString()))
                        urls.Add(u.ToString());
                }
            return urls;
        }

        private static void Scan(string text, Dictionary<string, string> found)
        {
            const string marker = "/_serverFn/";
            int length = text.Length;
            foreach (Match m in ServerFnRx.Matches(text))
            {
                string path  = m.Value;
                int index    = m.Index;
                int after    = m.Index + m.Length;

                int prevOcc  = index - marker.Length >= 0 ? text.LastIndexOf(marker, index - marker.Length) : -1;
                int prevFn   = prevOcc < 0 ? 0 : prevOcc + marker.Length;
                int nextOcc  = text.IndexOf(marker, after);
                int nextFn   = nextOcc < 0 ? length : nextOcc;

                int semi     = text.LastIndexOf(';', index - 1);
                if (semi < prevFn) semi = text.LastIndexOf('\n', index - 1);
                if (semi < prevFn) semi = -1;
                int start    = semi < 0 ? prevFn : semi + 1;

                int end      = text.IndexOf(';', after);
                if (end < 0 || end >= nextFn) end = text.IndexOf("createServerFn", after);
                if (end >= 0 && end + "createServerFn".Length > nextFn) end = -1;
                int stop     = end < 0 ? nextFn : end;

                string snippet = start <= stop ? text.Substring(start, stop - start) : path;

                string name = null;
                if (snippet.Contains("collegeId") || snippet.Contains("fingerprint"))
                    name = Functions[(int)Fn.SignIn].Id;
                else if (snippet.Contains("groupId") && snippet.Contains("weekStart"))
                    name = Functions[(int)Fn.Group].Id;
                else if (snippet.Contains("weekStart"))
                    name = snippet.Contains("teacher") || snippet.Contains("Teacher")
                           ? Functions[(int)Fn.Teacher].Id : Functions[(int)Fn.Student].Id;
                else if (snippet.Contains("revoke") || snippet.Contains("logout")
                      || snippet.Contains("signOut") || snippet.Contains("sign-out"))
                    name = Functions[(int)Fn.Logout].Id;

                if (name != null && !found.ContainsKey(name)) found[name] = path;
            }
        }

        private async Task<TextResult> FetchText(Uri url)
        {
            Uri current = url;
            for (int hop = 0; hop < 5; hop++)
            {
                var req = MakeRequest(current.ToString(), null);
                req.Headers.Remove("Origin");
                req.Headers["Accept"] = "application/json,text/html,*/*";
                req.Limit = JsonLimit;
                KtkHttp.Response resp;
                try { resp = await Send(req, true); }
                catch { return null; }
                if (resp.Status >= 300 && resp.Status < 400 && resp.Location != null)
                {
                    Uri next;
                    if (Uri.TryCreate(current, resp.Location, out next)
                        && next.Scheme == "https" && next.Host == url.Host)
                    { current = next; continue; }
                    return null;
                }
                if (resp.Status != 200) return null;
                return new TextResult(Encoding.UTF8.GetString(resp.Body, 0, resp.Body.Length), current);
            }
            return null;
        }

        private async Task<Dictionary<string, string>> Crawl()
        {
            var found = new Dictionary<string, string>();
            var root  = new Uri(Base + "/");
            var page  = await FetchText(root);
            if (page == null || page.Text == null) return found;
            Scan(page.Text, found);

            var queue = new Queue<string>(ScriptUrls(page.Text, page.FinalUri));
            var seen  = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            while (queue.Count > 0 && seen.Count < MaxScripts)
            {
                string urlStr = queue.Dequeue();
                if (!seen.Add(urlStr)) continue;
                var script = await FetchText(new Uri(urlStr));
                if (script == null || script.Text == null) continue;
                Scan(script.Text, found);
                foreach (var next in ScriptUrls(script.Text, script.FinalUri))
                    if (!seen.Contains(next) && queue.Count < 256) queue.Enqueue(next);
            }
            return found;
        }

        // ---------------------------------------------------------------- helpers

        private static Dictionary<string, object> DictStrToObj(Dictionary<string, string> src)
        {
            var d = new Dictionary<string, object>();
            foreach (var kv in src) d[kv.Key] = kv.Value;
            return d;
        }
    }
}
