// KTK for Symbian - ktk.education-system.ru: sign-in, the week with tasks and webinars, files,
// sign-out, and finding server functions again after a site deploy (the same protocol as
// ktk-ios KTKClient and ktk-android Client).
//
// The network calls block: they run in worker threads (AppController), each with its own Http.
// The session, the cookie jar and the server function addresses are shared and guarded here.
#ifndef CLIENT_H
#define CLIENT_H

#include "http.h"
#include "ktkerror.h"
#include "seroval.h"
#include "week.h"

#include <QHash>
#include <QMap>
#include <QMutex>
#include <QString>
#include <QVariant>

/// The site session (cookies); the password is never stored.
struct Session
{
    Session() : student(true), group(0) {}

    QMap<QString, QString> cookies;
    QString sessionId;
    QString login;
    bool student;
    qint64 group;
    QString teacherHash;
    QString branch;

    bool isNull() const { return cookies.isEmpty(); }
    QVariantMap toVariant() const;
    static Session fromVariant(const QVariant &v);
};

class Client
{
public:
    enum Fn { SignIn, Student, Teacher, Group, Logout, StudentHomework, StudentWebinar, FnCount };

    static const char *const Base;

    /// `dataDir` holds the session, the server function addresses and the saved weeks;
    /// `filesDir` the downloaded files (a folder other apps can open on the phone).
    Client(const QString &dataDir, const QString &filesDir);

    Session session() const;
    bool hasSession() const;
    /// Changes whenever the session is replaced or dropped, so a late answer can tell whether
    /// it still belongs to the session it was asked for.
    int sessionGeneration() const;

    /// Throws KtkError.
    Session signIn(const QString &login, const QString &password);
    /// Forgets the session, every saved week and every downloaded file.
    void signOutLocally();
    /// Ends `old` on the site (best effort; the local session should be gone already).
    void logoutOnSite(const Session &old);

    /// The week from the site, with tasks, webinars and files of the lessons that have them. When
    /// the site cannot be reached, the saved copy comes back with `offline` set, plus the error.
    /// Never throws.
    Week loadWeek(const QString &weekStart, KtkError *error);
    /// The saved copy of a week, or a null week. Reads a small file: fine on the UI thread.
    Week cachedWeek(const QString &weekStart) const;

    /// Downloads (or finds already downloaded) a task file; returns its path. Throws KtkError.
    QString downloadFile(const TaskFile &file);
    QString filesDirectory() const { return m_filesDir; }

private:
    friend class ClientProbe;   // the desktop harness (tools/ktk-cli)

    QString pathFor(Fn fn) const;
    void setPath(Fn fn, const QString &path);
    HttpRequest request(const QString &url, const QMap<QString, QString> *jar) const;
    HttpResponse send(Http &http, HttpRequest request, bool idempotent);
    /// Merges Set-Cookie lines into `jar` (0 = the current session's jar, saved to disk when it
    /// changes and the session is still `generation`).
    void absorbCookies(const QList<QByteArray> &lines, QMap<QString, QString> *jar, int generation);
    QVariant callOnce(Http &http, Fn fn, const QString &path, const Seroval::Fields &fields,
                      QMap<QString, QString> *jar);
    QVariant call(Http &http, Fn fn, const Seroval::Fields &fields, QMap<QString, QString> *jar = 0);
    QVariant quietCall(Http &http, Fn fn, const Seroval::Fields &fields);
    QVariantMap fetchWeek(Http &http, const QString &weekStart, const Session &session);
    QVariantMap extras(Http &http, const QVariantMap &payload, const QString &weekStart);
    void saveWeekPayload(const QVariantMap &payload, const QString &weekStart);
    void saveSessionLocked();
    void forgetCaches();

    // discovery
    QString rediscover(Http &http, Fn fn);
    bool fetchText(Http &http, const QString &url, QString *text, QString *finalUrl);
    QHash<QString, QString> crawl(Http &http);

    QString m_dataDir;
    QString m_filesDir;
    mutable QMutex m_lock;          // the members below
    Session m_session;
    int m_generation;
    QHash<QString, QString> m_paths;
    QMutex m_crawlLock;
    bool m_crawled;
    QHash<QString, QString> m_discovered;   // this launch's crawl

    Client(const Client &);
    Client &operator=(const Client &);
};

#endif // CLIENT_H
