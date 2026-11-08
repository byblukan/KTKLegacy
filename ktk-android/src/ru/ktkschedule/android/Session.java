package ru.ktkschedule.android;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONObject;

import java.util.LinkedHashMap;
import java.util.Map;

/** The site session (cookies) in private preferences; the password is never stored. */
final class Session {
    private static final String PREFS = "session";
    private static final String KEY = "json";

    Map<String, String> cookies = new LinkedHashMap<String, String>();
    String sessionId = "";
    String login = "";
    boolean student = true;
    long group;
    String teacherHash = "";
    String branch = "";

    static Session load(Context context) {
        String json = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, null);
        if (json == null) {
            return null;
        }
        try {
            Map<String, Object> d = Json.map(Json.fromJson(new JSONObject(json)));
            Map<String, Object> cookies = Json.mapAt(d, "cookies");
            if (cookies == null || cookies.isEmpty()) {
                return null;
            }
            Session session = new Session();
            for (Map.Entry<String, Object> cookie : cookies.entrySet()) {
                if (cookie.getValue() instanceof String) {
                    session.cookies.put(cookie.getKey(), (String) cookie.getValue());
                }
            }
            session.sessionId = orEmpty(Json.string(d.get("sessionId")));
            session.login = orEmpty(Json.string(d.get("login")));
            session.student = !(d.get("student") instanceof Boolean) || (Boolean) d.get("student");
            session.group = Json.number(d.get("group"), 0);
            session.teacherHash = orEmpty(Json.string(d.get("teacherHash")));
            session.branch = orEmpty(Json.string(d.get("branch")));
            return session;
        } catch (Exception e) {
            return null;
        }
    }

    synchronized void save(Context context) {
        Map<String, Object> d = new LinkedHashMap<String, Object>();
        d.put("cookies", new LinkedHashMap<String, String>(cookies));
        d.put("sessionId", sessionId);
        d.put("login", login);
        d.put("student", student);
        d.put("group", group);
        d.put("teacherHash", teacherHash);
        d.put("branch", branch);
        try {
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString(KEY, Json.toJson(d).toString()).commit();
        } catch (Exception ignored) {
            // The session then lasts until the app is closed.
        }
    }

    static void clear(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        prefs.edit().clear().commit();
    }

    private static String orEmpty(String s) {
        return s != null ? s : "";
    }
}
