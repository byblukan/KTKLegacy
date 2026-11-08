package ru.ktkschedule.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONObject;
import org.json.JSONTokener;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URL;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Semaphore;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** ktk.education-system.ru. Callbacks run on the main thread. */
final class Client {
    private static final String TAG = "KTK";
    static final String BASE = "https://ktk.education-system.ru";
    /** Part of the sign-in fingerprint: the site lists this app as its own device. */
    private static final String DEVICE_NAME = "ktk-android";
    private static final String USER_AGENT =
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36";
    private static final int JSON_LIMIT = 4 * 1024 * 1024;
    private static final int FILE_LIMIT = 50 * 1024 * 1024;
    /** Lessons per week whose task/webinar is fetched (the bot's MAX_LESSON_EXTRAS / 2). */
    private static final int MAX_EXTRAS = 20;
    private static final int MAX_SCRIPTS = 64;
    private static final String PATHS_PREFS = "server_functions";
    private static final Pattern WEEK_START = Pattern.compile("\\d{4}-\\d{2}-\\d{2}");

    interface Callback<T> {
        /** Both may be set: a cached week together with the error that kept the fresh one away. */
        void done(T value, KtkError error);
    }

    enum Fn {
        SIGN_IN("signIn", "9cd5b48fd0588913cb64c83d8ae2b42d5f3e97ed5dd47dc10f211cdc2322c1f6"),
        STUDENT("student", "85d2195b8f64b023a2c16160b65ef47139dea3a839096c4b75b3888cda49f16c"),
        TEACHER("teacher", "9fd2616a2ecddd56f79974f2111a13f2c9d89265cb6832c8c9e7ae2939e56f4a"),
        GROUP("group", "d418f54f8d5ba132bd168b1792cc8f0efbff634c7861f6aa66890cb9f7103c05"),
        LOGOUT("logout", "2e1ca1208f926608597adee62b6f7a786ebfd7e01b208963ecb9bee6e800f9f5"),
        STUDENT_HOMEWORK("studentHomework", "6856a131ea327da0ae2769d0e8b9950bce030194dda15acd331dd5ad2c273f38"),
        STUDENT_WEBINAR("studentWebinar", "b218948fba15961755e1cf6c0742e352d8709719e99ac7bd3e06315787e13650");

        final String id;
        /** Known ids (content hashes; a site deploy changes them, see rediscover). */
        final String defaultPath;

        Fn(String id, String hash) {
            this.id = id;
            this.defaultPath = "/_serverFn/" + hash;
        }
    }

    private static Client shared;

    static synchronized Client get(Context context) {
        if (shared == null) {
            shared = new Client(context.getApplicationContext());
        }
        return shared;
    }

    private final Context context;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService pool = Executors.newCachedThreadPool();
    private final SharedPreferences paths;
    private final Object crawlLock = new Object();
    private Session session;
    private Map<String, String> jar; // cookies of the current session
    private Map<String, String> discovered; // result of this launch's crawl

    private Client(Context context) {
        this.context = context;
        this.session = Session.load(context);
        this.jar = session != null ? new LinkedHashMap<String, String>(session.cookies) : new LinkedHashMap<String, String>();
        this.paths = context.getSharedPreferences(PATHS_PREFS, Context.MODE_PRIVATE);
    }

    synchronized Session session() {
        return session;
    }

    private Week shown; // the week on screen, for the lesson screen

    void setShownWeek(Week week) {
        shown = week;
    }

    /** The week on screen, or its saved copy after the process was restarted. */
    Week shownWeek(String weekStart) {
        Week week = shown;
        return week != null && week.weekStart.equals(weekStart) ? week : cachedWeek(weekStart);
    }

    private synchronized Map<String, String> currentJar() {
        return jar;
    }

    private void onMain(Runnable runnable) {
        main.post(runnable);
    }

    // ---------------------------------------------------------------- server functions

    private String pathFor(Fn fn) {
        String path = paths.getString(fn.id, null);
        return path != null && path.startsWith("/_serverFn/") ? path : fn.defaultPath;
    }

    private void setPath(String path, Fn fn) {
        paths.edit().putString(fn.id, path).commit();
    }

    private static String fingerprint(String login) {
        try {
            MessageDigest sha = MessageDigest.getInstance("SHA-256");
            sha.update(login.getBytes("UTF-8"));
            sha.update(DEVICE_NAME.getBytes("UTF-8"));
            byte[] hash = sha.digest();
            StringBuilder hex = new StringBuilder(32);
            for (int i = 0; i < 16; i++) {
                hex.append(String.format("%02x", hash[i] & 0xff));
            }
            return hex.toString();
        } catch (Exception e) {
            throw new AssertionError(e);
        }
    }

    /**
     * Server functions report failures in-band (HTTP 200 with {error: {appError: {code}}}).
     * An expired or revoked session must become LOGIN_REQUIRED.
     */
    private static KtkError serverError(Object value) {
        Map<String, Object> map = Json.map(value);
        Object error = map != null ? map.get("error") : null;
        if (error == null) {
            return null;
        }
        Map<String, Object> details = Json.mapAt(Json.map(error), "appError");
        if (details == null) {
            details = Json.map(error);
        }
        String code = details != null && Json.string(details.get("code")) != null ? Json.string(details.get("code")).toLowerCase() : "";
        String type = details != null && Json.string(details.get("type")) != null ? Json.string(details.get("type")).toLowerCase() : "";
        for (String word : new String[]{"auth", "session", "unauthor", "forbidden", "login"}) {
            if (code.contains(word) || type.contains(word)) {
                return new KtkError(KtkError.Code.LOGIN_REQUIRED, null);
            }
        }
        String message = details != null ? Json.string(details.get("message")) : null;
        return new KtkError(KtkError.Code.SERVER, message != null ? message : Json.string(error));
    }

    private static boolean shouldRediscover(KtkError error) {
        return error.code == KtkError.Code.BAD_RESPONSE
            || (error.code == KtkError.Code.HTTP_STATUS && (error.status == 404 || error.status == 410));
    }

    private Http.Request request(String url, Map<String, String> jar) {
        Http.Request request = new Http.Request();
        request.url = url;
        request.headers.put("User-Agent", USER_AGENT);
        request.headers.put("Origin", BASE);
        request.headers.put("Referer", BASE + "/");
        request.headers.put("sec-fetch-site", "same-origin");
        request.headers.put("sec-fetch-mode", "cors");
        request.headers.put("sec-fetch-dest", "empty");
        if (jar != null) {
            synchronized (jar) {
                if (!jar.isEmpty()) {
                    StringBuilder cookie = new StringBuilder();
                    for (Map.Entry<String, String> entry : jar.entrySet()) {
                        if (cookie.length() > 0) {
                            cookie.append("; ");
                        }
                        cookie.append(entry.getKey()).append('=').append(entry.getValue());
                    }
                    request.headers.put("Cookie", cookie.toString());
                }
            }
        }
        return request;
    }

    /** Idempotent requests are retried on connection errors and 5xx, like the bot does. */
    private Http.Response send(Http.Request request, boolean idempotent) throws KtkError {
        int attempts = idempotent ? 3 : 1;
        for (int attempt = 1; ; attempt++) {
            Http.Response response = null;
            KtkError failure = null;
            try {
                response = Http.perform(context, request);
            } catch (KtkError e) {
                failure = e;
            }
            boolean retry = attempt < attempts
                && ((failure != null && failure.code == KtkError.Code.NETWORK) || (response != null && response.status >= 500));
            if (!retry) {
                if (failure != null) {
                    throw failure;
                }
                return response;
            }
            try {
                Thread.sleep(200L * attempt);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                if (failure != null) {
                    throw failure;
                }
                return response;
            }
        }
    }

    /** Merges Set-Cookie headers into a jar; cookies rotated by the site are saved, so the session survives a restart. */
    private void absorbCookies(List<String> lines, Map<String, String> jar) {
        boolean changed = false;
        synchronized (jar) {
            for (String line : lines) {
                String[] parts = line.split(";");
                String pair = parts[0].trim();
                int equals = pair.indexOf('=');
                if (equals <= 0) {
                    continue;
                }
                String name = pair.substring(0, equals).trim();
                String value = pair.substring(equals + 1).trim();
                boolean expired = value.isEmpty();
                for (int i = 1; i < parts.length; i++) {
                    String attribute = parts[i].trim().toLowerCase();
                    if (attribute.startsWith("max-age=")) {
                        try {
                            expired |= Long.parseLong(attribute.substring(8).trim()) <= 0;
                        } catch (NumberFormatException ignored) {
                            // keep the cookie
                        }
                    }
                }
                if (expired) {
                    changed |= jar.remove(name) != null;
                } else if (!value.equals(jar.get(name))) {
                    jar.put(name, value);
                    changed = true;
                }
            }
        }
        if (!changed) {
            return;
        }
        synchronized (this) {
            if (jar == this.jar && session != null) {
                synchronized (jar) {
                    session.cookies = new LinkedHashMap<String, String>(jar);
                }
                session.save(context);
            }
        }
    }

    private Object callOnce(Fn fn, String path, Object[][] fields, Map<String, String> jar) throws KtkError {
        String payload = Seroval.request(fields).toString();
        boolean post = fn == Fn.SIGN_IN || fn == Fn.LOGOUT;
        String url = BASE + path;
        if (!post) {
            url += "?payload=" + Http.escape(payload);
        }
        Http.Request request = request(url, jar);
        if (post) {
            request.method = "POST";
            try {
                request.body = payload.getBytes("UTF-8");
            } catch (IOException e) {
                throw new AssertionError(e);
            }
            request.headers.put("Content-Type", "application/json");
        }
        if (fn == Fn.SIGN_IN) {
            request.headers.put("Referer", BASE + "/welcome?redirect=%2F");
        }
        request.headers.put("x-tsr-serverfn", "true");
        request.headers.put("x-application-storage-mode", "ordinary");
        request.headers.put("Accept", "application/x-tss-framed, application/x-ndjson, application/json");
        request.limit = JSON_LIMIT;
        request.timeoutMs = 60000;

        Http.Response response = send(request, !post);
        absorbCookies(response.setCookies, jar);
        if (fn != Fn.SIGN_IN && (response.status == 401 || response.status == 403)) {
            throw new KtkError(KtkError.Code.LOGIN_REQUIRED, null);
        }
        if (response.status != 200) {
            throw KtkError.status(response.status);
        }
        Object raw;
        try {
            raw = new JSONTokener(new String(response.body, "UTF-8")).nextValue();
        } catch (Exception e) {
            throw new KtkError(KtkError.Code.BAD_RESPONSE, null);
        }
        return Seroval.decode(raw);
    }

    /** One server function call; after a site deploy its id changes, so look it up once again. */
    private Object call(Fn fn, Object[][] fields, Map<String, String> jar) throws KtkError {
        if (jar == null) {
            jar = currentJar();
        }
        String path = pathFor(fn);
        try {
            return callOnce(fn, path, fields, jar);
        } catch (KtkError first) {
            if (!shouldRediscover(first)) {
                throw first;
            }
            String found = rediscover(fn);
            if (found == null || found.equals(path)) {
                throw first;
            }
            Log.i(TAG, "server function " + fn.id + " moved, using the rediscovered address");
            setPath(found, fn);
            return callOnce(fn, found, fields, jar);
        }
    }

    // ---------------------------------------------------------------- sign in / out

    void signIn(final String rawLogin, final String rawPassword, final Callback<Session> callback) {
        final String login = rawLogin.trim();
        final String password = rawPassword; // as typed: spaces at its ends may be part of it
        pool.execute(new Runnable() {
            @Override
            public void run() {
                Map<String, String> jar = new LinkedHashMap<String, String>();
                KtkError error = null;
                Session result = null;
                try {
                    Object value = call(Fn.SIGN_IN, new Object[][]{
                        {"collegeId", login}, {"password", password}, {"fingerprint", fingerprint(login)},
                    }, jar);
                    Map<String, Object> map = Json.map(value);
                    if (map == null) {
                        throw new KtkError(KtkError.Code.BAD_RESPONSE, null);
                    }
                    Map<String, Object> user = Json.mapAt(map, "user");
                    if (user == null) {
                        Map<String, Object> failure = Json.mapAt(map, "error");
                        String message = Json.string(Json.mapAt(failure, "appError") != null
                            ? Json.mapAt(failure, "appError").get("message") : null);
                        if (message == null && failure != null) {
                            message = Json.string(failure.get("message"));
                        }
                        throw new KtkError(KtkError.Code.CREDENTIALS, message);
                    }
                    Session session = new Session();
                    synchronized (jar) {
                        session.cookies = new LinkedHashMap<String, String>(jar);
                    }
                    String sessionId = Json.string(Json.mapAt(map, "session") != null ? Json.mapAt(map, "session").get("id") : null);
                    if (sessionId == null) {
                        sessionId = Json.string(Json.mapAt(user, "session") != null ? Json.mapAt(user, "session").get("id") : null);
                    }
                    session.sessionId = sessionId != null ? sessionId : "";
                    session.login = login;
                    session.student = !(user.get("isStudent") instanceof Boolean) || (Boolean) user.get("isStudent");
                    Object group = user.get("group");
                    if (group instanceof Number) {
                        session.group = ((Number) group).longValue();
                    } else if (group instanceof String) {
                        try {
                            session.group = Long.parseLong(((String) group).trim());
                        } catch (NumberFormatException ignored) {
                            session.group = 0;
                        }
                    }
                    String hash = Json.string(user.get("hash"));
                    session.teacherHash = session.student ? "" : (hash != null ? hash : "teacher");
                    String branch = Json.string(user.get("branch"));
                    session.branch = branch != null ? branch : "";
                    if (session.cookies.isEmpty()) {
                        throw new KtkError(KtkError.Code.BAD_RESPONSE, null);
                    }
                    forgetCaches();
                    session.save(context);
                    synchronized (Client.this) {
                        Client.this.session = session;
                        Client.this.jar = new LinkedHashMap<String, String>(session.cookies);
                    }
                    result = session;
                } catch (KtkError e) {
                    error = e;
                }
                final Session done = result;
                final KtkError failure = error;
                onMain(new Runnable() {
                    @Override
                    public void run() {
                        callback.done(done, failure);
                    }
                });
            }
        });
    }

    void signOutLocally() {
        synchronized (this) {
            session = null;
            jar = new LinkedHashMap<String, String>();
        }
        Session.clear(context);
        pool.execute(new Runnable() {
            @Override
            public void run() {
                forgetCaches();
            }
        });
    }

    /**
     * Ends the session on the site (best effort) and forgets it, every cached week and file, and the
     * login remembered for the sign-in screen (kept when only the site ended the session).
     */
    void signOut() {
        final Session old = session();
        signOutLocally();
        context.getSharedPreferences(LoginActivity.PREFS, Context.MODE_PRIVATE).edit()
            .remove(LoginActivity.LAST_LOGIN).commit();
        if (old != null && !old.sessionId.isEmpty()) {
            final Map<String, String> oldJar = new LinkedHashMap<String, String>(old.cookies);
            pool.execute(new Runnable() {
                @Override
                public void run() {
                    try {
                        call(Fn.LOGOUT, new Object[][]{{"id", old.sessionId}}, oldJar);
                    } catch (KtkError ignored) {
                        // The local session is gone anyway.
                    }
                }
            });
        }
    }

    private void forgetCaches() {
        deleteTree(new File(context.getCacheDir(), "ktk-weeks"));
        deleteTree(new File(context.getCacheDir(), "ktk-files"));
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }

    // ---------------------------------------------------------------- weeks

    private File weekCacheFile(String weekStart) {
        return new File(new File(context.getCacheDir(), "ktk-weeks"), weekStart + ".json");
    }

    /** The saved copy of a week, or null. Reads a file: fine on the main thread for these sizes. */
    Week cachedWeek(String weekStart) {
        if (weekStart == null || !WEEK_START.matcher(weekStart).matches()) {
            return null;
        }
        File file = weekCacheFile(weekStart);
        if (!file.isFile() || file.length() > 16L * 1024 * 1024) {
            return null;
        }
        try {
            byte[] data = new byte[(int) file.length()];
            FileInputStream in = new FileInputStream(file);
            try {
                int offset = 0;
                while (offset < data.length) {
                    int n = in.read(data, offset, data.length - offset);
                    if (n < 0) {
                        break;
                    }
                    offset += n;
                }
            } finally {
                in.close();
            }
            Map<String, Object> payload = Json.map(Json.fromJson(new JSONObject(new String(data, "UTF-8"))));
            return payload != null ? Week.fromPayload(payload, true) : null;
        } catch (Exception e) {
            return null;
        }
    }

    private void saveWeekPayload(Map<String, Object> payload, String weekStart) {
        File file = weekCacheFile(weekStart);
        File partial = new File(file.getPath() + ".part");
        try {
            //noinspection ResultOfMethodCallIgnored
            file.getParentFile().mkdirs();
            FileOutputStream out = new FileOutputStream(partial);
            try {
                out.write(Json.toJson(payload).toString().getBytes("UTF-8"));
                out.getFD().sync();
            } finally {
                out.close();
            }
            if (!partial.renameTo(file)) {
                //noinspection ResultOfMethodCallIgnored
                partial.delete();
            }
        } catch (Exception e) {
            //noinspection ResultOfMethodCallIgnored
            partial.delete();
        }
    }

    private Map<String, Object> fetchWeek(String weekStart, Session session) throws KtkError {
        boolean teacher = !session.student;
        Object value = call(teacher ? Fn.TEACHER : Fn.STUDENT, new Object[][]{{"weekStart", weekStart}}, null);
        KtkError server = serverError(value);
        if (server != null) {
            throw server;
        }
        Map<String, Object> week = Json.map(value);
        if (week == null || Json.list(week.get("days")) == null) {
            throw new KtkError(KtkError.Code.BAD_RESPONSE, null);
        }
        Map<String, Object> payload = new LinkedHashMap<String, Object>();
        payload.put("weekStart", weekStart);
        payload.put("week", week);
        payload.put("teacher", teacher);
        payload.put("savedAt", System.currentTimeMillis());
        if (!teacher) {
            payload.put("extras", extras(payload, weekStart));
        }
        return payload;
    }

    /** Tasks of lessons flagged hasHomework and webinars of distance lessons; best effort, 3 at a time. */
    private Map<String, Object> extras(Map<String, Object> payload, final String weekStart) {
        Week bare = Week.fromPayload(payload, false);
        final List<Lesson> wanted = new ArrayList<Lesson>();
        Set<Long> journals = new HashSet<Long>();
        if (bare != null) {
            for (Lesson lesson : bare.allLessons()) {
                if (wanted.size() >= MAX_EXTRAS) {
                    break;
                }
                if (lesson.journalId > 0 && (lesson.hasHomework || lesson.remote) && journals.add(lesson.journalId)) {
                    wanted.add(lesson);
                }
            }
        }
        final Map<String, Object> extras = new LinkedHashMap<String, Object>();
        final Semaphore slots = new Semaphore(3);
        final CountDownLatch done = new CountDownLatch(wanted.size());
        for (final Lesson lesson : wanted) {
            slots.acquireUninterruptibly();
            pool.execute(new Runnable() {
                @Override
                public void run() {
                    try {
                        Object[][] fields = {{"weekStart", weekStart}, {"journalId", lesson.journalId}};
                        Map<String, Object> entry = new LinkedHashMap<String, Object>();
                        if (lesson.hasHomework) {
                            Object value = quietCall(Fn.STUDENT_HOMEWORK, fields);
                            if (Json.map(value) != null && serverError(value) == null) {
                                entry.put("homework", value);
                            }
                        }
                        if (lesson.remote) {
                            Object value = quietCall(Fn.STUDENT_WEBINAR, fields);
                            if (Json.map(value) != null && serverError(value) == null) {
                                entry.put("webinar", value);
                            }
                        }
                        if (!entry.isEmpty()) {
                            synchronized (extras) {
                                extras.put(String.valueOf(lesson.journalId), entry);
                            }
                        }
                    } finally {
                        slots.release();
                        done.countDown();
                    }
                }
            });
        }
        try {
            done.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        synchronized (extras) {
            return new LinkedHashMap<String, Object>(extras);
        }
    }

    private Object quietCall(Fn fn, Object[][] fields) {
        try {
            return call(fn, fields, null);
        } catch (KtkError e) {
            return null;
        }
    }

    /**
     * The week from the site, with tasks, webinars and files of the lessons that have them. When the
     * site cannot be reached, the cached copy comes back with {@code offline} set, plus the error.
     * LOGIN_REQUIRED signs out locally.
     */
    void loadWeek(final String weekStart, final Callback<Week> callback) {
        final Session session = session();
        if (session == null || !WEEK_START.matcher(weekStart).matches()) {
            callback.done(null, new KtkError(KtkError.Code.LOGIN_REQUIRED, null));
            return;
        }
        pool.execute(new Runnable() {
            @Override
            public void run() {
                KtkError error = null;
                Week week = null;
                try {
                    Map<String, Object> payload = fetchWeek(weekStart, session);
                    week = Week.fromPayload(payload, true);
                    if (week != null) {
                        saveWeekPayload(payload, weekStart);
                    } else {
                        error = new KtkError(KtkError.Code.BAD_RESPONSE, null);
                    }
                } catch (KtkError e) {
                    error = e;
                } catch (RuntimeException e) {
                    Log.e(TAG, "week " + weekStart, e);
                    error = new KtkError(KtkError.Code.BAD_RESPONSE, null);
                }
                if (week == null && error != null && error.code != KtkError.Code.LOGIN_REQUIRED) {
                    week = cachedWeek(weekStart);
                    if (week != null) {
                        week.offline = true;
                    }
                }
                final Week done = week;
                final KtkError failure = error;
                onMain(new Runnable() {
                    @Override
                    public void run() {
                        if (failure != null && failure.code == KtkError.Code.LOGIN_REQUIRED && session() == session) {
                            signOutLocally();
                        }
                        callback.done(done, failure);
                    }
                });
            }
        });
    }

    // ---------------------------------------------------------------- files

    File filesDirectory() {
        return new File(context.getCacheDir(), "ktk-files");
    }

    void downloadFile(final TaskFile file, final Callback<File> callback) {
        pool.execute(new Runnable() {
            @Override
            public void run() {
                File directory = new File(filesDirectory(), String.valueOf(file.fileId));
                File target = new File(directory, file.safeName());
                KtkError error = null;
                if (!target.isFile()) {
                    //noinspection ResultOfMethodCallIgnored
                    directory.mkdirs();
                    File partial = new File(directory, target.getName() + ".part");
                    Http.Request request = request(BASE + "/api/timetable/students/self/lessons/" + file.journalId
                        + "/files/" + file.fileId + "?weekStart=" + Http.escape(file.weekStart), currentJar());
                    request.headers.remove("Origin");
                    request.headers.put("Accept", "*/*");
                    request.limit = FILE_LIMIT;
                    request.timeoutMs = 120000;
                    request.output = partial;
                    try {
                        Http.Response response = send(request, false);
                        if (response.status == 401 || response.status == 403) {
                            throw new KtkError(KtkError.Code.LOGIN_REQUIRED, null);
                        }
                        if (response.status != 200) {
                            throw KtkError.status(response.status);
                        }
                        //noinspection ResultOfMethodCallIgnored
                        target.delete();
                        if (!partial.renameTo(target)) {
                            throw new KtkError(KtkError.Code.FILE, "Не удалось сохранить файл.");
                        }
                    } catch (KtkError e) {
                        error = e;
                    } finally {
                        //noinspection ResultOfMethodCallIgnored
                        partial.delete();
                    }
                }
                final File done = error == null ? target : null;
                final KtkError failure = error;
                onMain(new Runnable() {
                    @Override
                    public void run() {
                        callback.done(done, failure);
                    }
                });
            }
        });
    }

    // ---------------------------------------------------------------- discovery

    /**
     * The page and its scripts name every server function; classify each id by the code around it,
     * exactly like the bot's Candidates::add. One crawl per launch serves every call.
     */
    private String rediscover(Fn fn) {
        if (fn == Fn.STUDENT_HOMEWORK || fn == Fn.STUDENT_WEBINAR) {
            return null; // not told apart by discovery yet: the known ids are used
        }
        synchronized (crawlLock) {
            if (discovered == null) {
                discovered = crawl();
            }
            return discovered.get(fn.id);
        }
    }

    /** {text, final URL} or null; same-origin redirects only. */
    private Object[] fetchText(URL url) {
        URL current = url;
        for (int hop = 0; hop < 5; hop++) {
            Http.Request request = request(current.toString(), null);
            request.headers.remove("Origin");
            request.headers.put("Accept", "application/json,text/html,*/*");
            request.limit = JSON_LIMIT;
            Http.Response response;
            try {
                response = send(request, true);
            } catch (KtkError e) {
                return null;
            }
            if (response.status >= 300 && response.status < 400 && response.location != null) {
                try {
                    URL next = new URL(current, response.location);
                    if (!"https".equals(next.getProtocol()) || !url.getHost().equals(next.getHost())) {
                        return null;
                    }
                    current = next;
                    continue;
                } catch (MalformedURLException e) {
                    return null;
                }
            }
            if (response.status != 200) {
                return null;
            }
            try {
                return new Object[]{new String(response.body, "UTF-8"), current};
            } catch (IOException e) {
                return null;
            }
        }
        return null;
    }

    private static final Pattern SCRIPT_TAG = Pattern.compile(
        "<script[^>]+src=[\"']([^\"']+)[\"']", Pattern.CASE_INSENSITIVE | Pattern.DOTALL);
    private static final Pattern SCRIPT_PATH = Pattern.compile("[\"']([^\"']+\\.js(?:\\?[^\"']*)?)[\"']");
    private static final Pattern SERVER_FN = Pattern.compile("/_serverFn/([0-9a-fA-F]{32,64})");

    private static List<URL> scriptURLs(String text, URL base) {
        List<URL> urls = new ArrayList<URL>();
        Set<String> seen = new HashSet<String>();
        for (Pattern pattern : new Pattern[]{SCRIPT_TAG, SCRIPT_PATH}) {
            Matcher match = pattern.matcher(text);
            while (match.find() && urls.size() < 128) {
                try {
                    URL url = new URL(base, match.group(1));
                    if ("https".equals(url.getProtocol()) && base.getHost().equals(url.getHost()) && seen.add(url.toString())) {
                        urls.add(url);
                    }
                } catch (MalformedURLException ignored) {
                    // not a URL
                }
            }
        }
        return urls;
    }

    private static void scan(String text, Map<String, String> found) {
        String marker = "/_serverFn/";
        int length = text.length();
        Matcher match = SERVER_FN.matcher(text);
        while (match.find()) {
            String path = match.group();
            int index = match.start();
            int after = match.end();
            int previous = index - marker.length() >= 0 ? text.lastIndexOf(marker, index - marker.length()) : -1;
            int previousFn = previous < 0 ? 0 : previous + marker.length();
            int next = text.indexOf(marker, after);
            int nextFn = next < 0 ? length : next;

            int semicolon = text.lastIndexOf(';', index - 1);
            if (semicolon < previousFn) {
                semicolon = text.lastIndexOf('\n', index - 1);
                if (semicolon < previousFn) {
                    semicolon = -1;
                }
            }
            int start = semicolon < 0 ? previousFn : semicolon + 1;

            int end = text.indexOf(';', after);
            if (end < 0 || end >= nextFn) {
                end = text.indexOf("createServerFn", after);
                if (end >= 0 && end + "createServerFn".length() > nextFn) {
                    end = -1;
                }
            }
            int stop = end < 0 ? nextFn : end;
            String snippet = start <= stop ? text.substring(start, stop) : path;

            String name = null;
            if (snippet.contains("collegeId") || snippet.contains("fingerprint")) {
                name = Fn.SIGN_IN.id;
            } else if (snippet.contains("groupId") && snippet.contains("weekStart")) {
                name = Fn.GROUP.id;
            } else if (snippet.contains("weekStart")) {
                name = snippet.contains("teacher") || snippet.contains("Teacher") ? Fn.TEACHER.id : Fn.STUDENT.id;
            } else if (snippet.contains("revoke") || snippet.contains("logout") || snippet.contains("signOut")
                || snippet.contains("sign-out")) {
                name = Fn.LOGOUT.id;
            }
            if (name != null && !found.containsKey(name)) {
                found.put(name, path);
            }
        }
    }

    private Map<String, String> crawl() {
        Map<String, String> found = new HashMap<String, String>();
        URL root;
        try {
            root = new URL(BASE + "/");
        } catch (MalformedURLException e) {
            return found;
        }
        Object[] page = fetchText(root);
        if (page == null) {
            return found;
        }
        scan((String) page[0], found);
        LinkedList<URL> queue = new LinkedList<URL>(scriptURLs((String) page[0], (URL) page[1]));
        Set<String> seen = new HashSet<String>();
        while (!queue.isEmpty() && seen.size() < MAX_SCRIPTS) {
            URL url = queue.removeFirst();
            if (!seen.add(url.toString())) {
                continue;
            }
            Object[] script = fetchText(url);
            if (script == null) {
                continue;
            }
            scan((String) script[0], found);
            for (URL next : scriptURLs((String) script[0], url)) {
                if (!seen.contains(next.toString()) && queue.size() < 256) {
                    queue.add(next);
                }
            }
        }
        return found;
    }
}
