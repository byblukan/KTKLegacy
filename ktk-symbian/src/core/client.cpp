// KTK for Symbian - ktk.education-system.ru.
#include "client.h"
#include "collegecalendar.h"
#include "json.h"
#include "trustanchors.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QRegExp>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QUrl>

#include "bearssl.h"

#include <string.h>

const char *const Client::Base = "https://ktk.education-system.ru";

namespace
{
    /// Part of the sign-in fingerprint: the site lists this app as its own device.
    const char *const DeviceName = "ktk-symbian";
    const char *const UserAgent =
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36";
    const qint64 JsonLimit = 4 * 1024 * 1024;
    const qint64 FileLimit = 50 * 1024 * 1024;
    /// Lessons per week whose task/webinar is fetched (the bot's MAX_LESSON_EXTRAS / 2).
    const int MaxExtras = 20;
    const int MaxScripts = 64;

    struct FnInfo
    {
        const char *id;
        const char *hash;
    };

    /// Known ids (content hashes; a site deploy changes them, see rediscover).
    const FnInfo Functions[Client::FnCount] = {
        { "signIn", "9cd5b48fd0588913cb64c83d8ae2b42d5f3e97ed5dd47dc10f211cdc2322c1f6" },
        { "student", "85d2195b8f64b023a2c16160b65ef47139dea3a839096c4b75b3888cda49f16c" },
        { "teacher", "9fd2616a2ecddd56f79974f2111a13f2c9d89265cb6832c8c9e7ae2939e56f4a" },
        { "group", "d418f54f8d5ba132bd168b1792cc8f0efbff634c7861f6aa66890cb9f7103c05" },
        { "logout", "2e1ca1208f926608597adee62b6f7a786ebfd7e01b208963ecb9bee6e800f9f5" },
        { "studentHomework", "6856a131ea327da0ae2769d0e8b9950bce030194dda15acd331dd5ad2c273f38" },
        { "studentWebinar", "b218948fba15961755e1cf6c0742e352d8709719e99ac7bd3e06315787e13650" }
    };

    QString fnId(Client::Fn fn) { return QLatin1String(Functions[fn].id); }
    QString defaultPath(Client::Fn fn) { return QLatin1String("/_serverFn/") + QLatin1String(Functions[fn].hash); }

    /// QThread::msleep is protected in Qt 4.
    class Sleeper : public QThread
    {
    public:
        static void ms(unsigned long t) { QThread::msleep(t); }
    };

    QString fingerprint(const QString &login)
    {
        br_sha256_context sha;
        br_sha256_init(&sha);
        const QByteArray a = login.toUtf8();
        br_sha256_update(&sha, a.constData(), size_t(a.size()));
        br_sha256_update(&sha, DeviceName, strlen(DeviceName));
        unsigned char hash[32];
        br_sha256_out(&sha, hash);
        return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(hash), 16).toHex());
    }

    QVariant at(const QVariantMap &m, const char *key) { return m.value(QLatin1String(key)); }

    /// Server functions report failures in-band (HTTP 200 with {error: {appError: {code}}}). An
    /// expired or revoked session must become LoginRequired. None when there is no error.
    KtkError serverError(const QVariant &value)
    {
        const QVariantMap map = Json::map(value);
        const QVariant error = map.value(QLatin1String("error"));
        if (!error.isValid()) return KtkError();
        QVariantMap details = Json::mapAt(Json::map(error), "appError");
        if (!Json::hasMap(Json::map(error), "appError")) details = Json::map(error);
        const QString code = Json::string(at(details, "code")).toLower();
        const QString type = Json::string(at(details, "type")).toLower();
        const char *const words[] = { "auth", "session", "unauthor", "forbidden", "login" };
        for (int i = 0; i < 5; ++i) {
            if (code.contains(QLatin1String(words[i])) || type.contains(QLatin1String(words[i])))
                return KtkError(KtkError::LoginRequired);
        }
        QString message = Json::string(at(details, "message"));
        if (message.isNull()) message = Json::string(error);
        return KtkError(KtkError::Server, message);
    }

    bool shouldRediscover(const KtkError &error)
    {
        return error.code == KtkError::BadResponse
            || (error.code == KtkError::HttpStatus && (error.status == 404 || error.status == 410));
    }

    bool writeFileAtomically(const QString &path, const QByteArray &data)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        const QString partial = path + QLatin1String(".part");
        QFile f(partial);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        const bool written = f.write(data) == data.size() && f.flush();
        f.close();
        if (!written) {
            QFile::remove(partial);
            return false;
        }
        QFile::remove(path);
        if (!QFile::rename(partial, path)) {
            QFile::remove(partial);
            return false;
        }
        return true;
    }

    QByteArray readSmallFile(const QString &path, qint64 limit)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly) || f.size() > limit) return QByteArray();
        return f.readAll();
    }

    void deleteTree(const QString &path)
    {
        QFileInfo info(path);
        if (info.isDir() && !info.isSymLink()) {
            QDir dir(path);
            const QStringList entries = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
            for (int i = 0; i < entries.size(); ++i) deleteTree(dir.filePath(entries.at(i)));
            QDir().rmdir(path);
        } else if (info.exists() || info.isSymLink()) {
            QFile::remove(path);
        }
    }

    Seroval::Fields fields1(const char *k, const QVariant &v)
    {
        Seroval::Fields f;
        f.append(qMakePair(QString::fromLatin1(k), v));
        return f;
    }
}

// -- Session -------------------------------------------------------------------------------------

QVariantMap Session::toVariant() const
{
    QVariantMap c;
    for (QMap<QString, QString>::const_iterator it = cookies.constBegin(); it != cookies.constEnd(); ++it)
        c.insert(it.key(), it.value());
    QVariantMap d;
    d.insert(QLatin1String("cookies"), c);
    d.insert(QLatin1String("sessionId"), sessionId);
    d.insert(QLatin1String("login"), login);
    d.insert(QLatin1String("student"), student);
    d.insert(QLatin1String("group"), group);
    d.insert(QLatin1String("teacherHash"), teacherHash);
    d.insert(QLatin1String("branch"), branch);
    return d;
}

Session Session::fromVariant(const QVariant &v)
{
    Session s;
    const QVariantMap d = Json::map(v);
    const QVariantMap c = Json::mapAt(d, "cookies");
    for (QVariantMap::const_iterator it = c.constBegin(); it != c.constEnd(); ++it)
        if (Json::isString(it.value())) s.cookies.insert(it.key(), it.value().toString());
    if (s.cookies.isEmpty()) return Session();
    s.sessionId = Json::string(at(d, "sessionId"));
    s.login = Json::string(at(d, "login"));
    s.student = !Json::isBool(at(d, "student")) || at(d, "student").toBool();
    s.group = Json::number(at(d, "group"), 0);
    s.teacherHash = Json::string(at(d, "teacherHash"));
    s.branch = Json::string(at(d, "branch"));
    return s;
}

// -- Client --------------------------------------------------------------------------------------

Client::Client(const QString &dataDir, const QString &filesDir)
    : m_dataDir(dataDir), m_filesDir(filesDir), m_generation(1), m_crawled(false)
{
    ktk_trust_anchors_init();
    QDir().mkpath(m_dataDir);
    bool ok = false;
    const QVariant session = Json::parse(readSmallFile(m_dataDir + QLatin1String("/session.json"), 1024 * 1024), &ok);
    if (ok) m_session = Session::fromVariant(session);
    const QVariantMap paths = Json::map(Json::parse(readSmallFile(m_dataDir + QLatin1String("/paths.json"), 64 * 1024), &ok));
    for (QVariantMap::const_iterator it = paths.constBegin(); it != paths.constEnd(); ++it)
        m_paths.insert(it.key(), it.value().toString());
}

Session Client::session() const
{
    QMutexLocker locker(&m_lock);
    return m_session;
}

bool Client::hasSession() const
{
    QMutexLocker locker(&m_lock);
    return !m_session.isNull();
}

int Client::sessionGeneration() const
{
    QMutexLocker locker(&m_lock);
    return m_generation;
}

void Client::saveSessionLocked()
{
    const QString path = m_dataDir + QLatin1String("/session.json");
    if (m_session.isNull()) QFile::remove(path);
    else writeFileAtomically(path, Json::serialize(m_session.toVariant()));
}

// -- server functions ----------------------------------------------------------------------------

QString Client::pathFor(Fn fn) const
{
    QMutexLocker locker(&m_lock);
    const QString path = m_paths.value(fnId(fn));
    return path.startsWith(QLatin1String("/_serverFn/")) ? path : defaultPath(fn);
}

void Client::setPath(Fn fn, const QString &path)
{
    QMutexLocker locker(&m_lock);
    m_paths.insert(fnId(fn), path);
    QVariantMap out;
    for (QHash<QString, QString>::const_iterator it = m_paths.constBegin(); it != m_paths.constEnd(); ++it)
        out.insert(it.key(), it.value());
    writeFileAtomically(m_dataDir + QLatin1String("/paths.json"), Json::serialize(out));
}

HttpRequest Client::request(const QString &url, const QMap<QString, QString> *jar) const
{
    HttpRequest request;
    request.url = url;
    request.setHeader("User-Agent", UserAgent);
    request.setHeader("Origin", Base);
    request.setHeader("Referer", QByteArray(Base) + "/");
    request.setHeader("sec-fetch-site", "same-origin");
    request.setHeader("sec-fetch-mode", "cors");
    request.setHeader("sec-fetch-dest", "empty");
    if (jar && !jar->isEmpty()) {
        QByteArray cookie;
        for (QMap<QString, QString>::const_iterator it = jar->constBegin(); it != jar->constEnd(); ++it) {
            if (!cookie.isEmpty()) cookie += "; ";
            cookie += it.key().toUtf8() + '=' + it.value().toUtf8();
        }
        request.setHeader("Cookie", cookie);
    }
    return request;
}

/// Idempotent requests are retried on connection errors and 5xx, like the bot does.
HttpResponse Client::send(Http &http, HttpRequest request, bool idempotent)
{
    const int attempts = idempotent ? 3 : 1;
    for (int attempt = 1;; ++attempt) {
        HttpResponse response;
        KtkError failure;
        try {
            response = http.perform(request);
        } catch (const KtkError &e) {
            failure = e;
        }
        const bool retry = attempt < attempts
            && ((failure.isError() && failure.code == KtkError::Network) || (!failure.isError() && response.status >= 500));
        if (!retry) {
            if (failure.isError()) throw failure;
            return response;
        }
        Sleeper::ms(200 * attempt);
    }
}

/// Merges Set-Cookie headers into a jar; cookies rotated by the site are saved, so the session
/// survives a restart.
void Client::absorbCookies(const QList<QByteArray> &lines, QMap<QString, QString> *jar, int generation)
{
    QMutexLocker locker(&m_lock);
    const bool current = jar == 0;
    if (current) {
        if (generation != m_generation || m_session.isNull()) return;   // signed out meanwhile
        jar = &m_session.cookies;
    }
    bool changed = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = QString::fromUtf8(lines.at(i));
        const QStringList parts = line.split(QLatin1Char(';'));
        const QString pair = parts.at(0).trimmed();
        const int equals = pair.indexOf(QLatin1Char('='));
        if (equals <= 0) continue;
        const QString name = pair.left(equals).trimmed();
        const QString value = pair.mid(equals + 1).trimmed();
        bool expired = value.isEmpty();
        for (int k = 1; k < parts.size(); ++k) {
            const QString attribute = parts.at(k).trimmed().toLower();
            if (attribute.startsWith(QLatin1String("max-age="))) {
                bool ok = false;
                const qint64 age = attribute.mid(8).trimmed().toLongLong(&ok);
                if (ok && age <= 0) expired = true;
            }
        }
        if (expired) {
            if (jar->remove(name) > 0) changed = true;
        } else if (jar->value(name) != value || !jar->contains(name)) {
            jar->insert(name, value);
            changed = true;
        }
    }
    if (changed && current) {
        if (m_session.cookies.isEmpty()) {
            // Every cookie expired: keep the last known ones on disk until the site says
            // LoginRequired, which signs out properly.
            return;
        }
        saveSessionLocked();
    }
}

QVariant Client::callOnce(Http &http, Fn fn, const QString &path, const Seroval::Fields &fields,
                          QMap<QString, QString> *jar)
{
    const QByteArray payload = Seroval::request(fields);
    const bool post = fn == SignIn || fn == Logout;
    QString url = QLatin1String(Base) + path;
    if (!post) url += QLatin1String("?payload=") + Http::escape(QString::fromUtf8(payload));

    int generation = 0;
    QMap<QString, QString> cookies;
    if (jar) {
        cookies = *jar;
    } else {
        QMutexLocker locker(&m_lock);
        cookies = m_session.cookies;
        generation = m_generation;
    }
    HttpRequest req = request(url, &cookies);
    if (post) {
        req.method = "POST";
        req.body = payload;
        req.setHeader("Content-Type", "application/json");
    }
    if (fn == SignIn) req.setHeader("Referer", QByteArray(Base) + "/welcome?redirect=%2F");
    req.setHeader("x-tsr-serverfn", "true");
    req.setHeader("x-application-storage-mode", "ordinary");
    req.setHeader("Accept", "application/x-tss-framed, application/x-ndjson, application/json");
    req.limit = JsonLimit;
    req.timeoutMs = 60000;

    const HttpResponse response = send(http, req, !post);
    absorbCookies(response.setCookies, jar, generation);
    if (fn != SignIn && (response.status == 401 || response.status == 403)) throw KtkError(KtkError::LoginRequired);
    if (response.status != 200) throw KtkError::fromStatus(response.status);
    bool ok = false;
    const QVariant raw = Json::parse(response.body, &ok, true);
    if (!ok) throw KtkError(KtkError::BadResponse);
    return Seroval::decode(raw);
}

/// One server function call; after a site deploy its id changes, so look it up once again.
QVariant Client::call(Http &http, Fn fn, const Seroval::Fields &fields, QMap<QString, QString> *jar)
{
    const QString path = pathFor(fn);
    try {
        return callOnce(http, fn, path, fields, jar);
    } catch (const KtkError &first) {
        if (!shouldRediscover(first)) throw;
        const QString found = rediscover(http, fn);
        if (found.isEmpty() || found == path) throw;
        qDebug("server function %s moved, using the rediscovered address", Functions[fn].id);
        setPath(fn, found);
        return callOnce(http, fn, found, fields, jar);
    }
}

QVariant Client::quietCall(Http &http, Fn fn, const Seroval::Fields &fields)
{
    try {
        return call(http, fn, fields);
    } catch (const KtkError &) {
        return QVariant();
    }
}

// -- sign in / out -------------------------------------------------------------------------------

Session Client::signIn(const QString &rawLogin, const QString &rawPassword)
{
    const QString login = rawLogin.trimmed();
    const QString password = rawPassword;   // as typed: spaces at its ends may be part of it
    QMap<QString, QString> jar;
    Http http;
    Seroval::Fields fields;
    fields.append(qMakePair(QString::fromLatin1("collegeId"), QVariant(login)));
    fields.append(qMakePair(QString::fromLatin1("password"), QVariant(password)));
    fields.append(qMakePair(QString::fromLatin1("fingerprint"), QVariant(fingerprint(login))));
    const QVariant value = call(http, SignIn, fields, &jar);
    if (!Json::isMap(value)) throw KtkError(KtkError::BadResponse);
    const QVariantMap map = value.toMap();
    if (!Json::hasMap(map, "user")) {
        const QVariantMap failure = Json::mapAt(map, "error");
        QString message = Json::string(at(Json::mapAt(failure, "appError"), "message"));
        if (message.isNull()) message = Json::string(at(failure, "message"));
        throw KtkError(KtkError::Credentials, message);
    }
    const QVariantMap user = Json::mapAt(map, "user");
    Session session;
    session.cookies = jar;
    QString sessionId = Json::string(at(Json::mapAt(map, "session"), "id"));
    if (sessionId.isNull()) sessionId = Json::string(at(Json::mapAt(user, "session"), "id"));
    session.sessionId = sessionId;
    session.login = login;
    session.student = !Json::isBool(at(user, "isStudent")) || at(user, "isStudent").toBool();
    const QVariant group = at(user, "group");
    if (Json::isNumber(group)) {
        session.group = Json::number(group, 0);
    } else if (Json::isString(group)) {
        bool ok = false;
        session.group = group.toString().trimmed().toLongLong(&ok);
        if (!ok) session.group = 0;
    }
    const QString hash = Json::string(at(user, "hash"));
    session.teacherHash = session.student ? QString() : (hash.isNull() ? QString::fromLatin1("teacher") : hash);
    session.branch = Json::string(at(user, "branch"));
    if (session.cookies.isEmpty()) throw KtkError(KtkError::BadResponse);

    forgetCaches();
    QMutexLocker locker(&m_lock);
    m_session = session;
    ++m_generation;
    saveSessionLocked();
    return session;
}

void Client::signOutLocally()
{
    {
        QMutexLocker locker(&m_lock);
        m_session = Session();
        ++m_generation;
        saveSessionLocked();
    }
    forgetCaches();
}

void Client::logoutOnSite(const Session &old)
{
    if (old.isNull() || old.sessionId.isEmpty()) return;
    QMap<QString, QString> jar = old.cookies;
    Http http;
    try {
        call(http, Logout, fields1("id", old.sessionId), &jar);
    } catch (const KtkError &) {
        // The local session is gone anyway.
    }
}

void Client::forgetCaches()
{
    deleteTree(m_dataDir + QLatin1String("/weeks"));
    deleteTree(m_filesDir);
}

// -- weeks ---------------------------------------------------------------------------------------

Week Client::cachedWeek(const QString &weekStart) const
{
    if (!CollegeCalendar::isWeekStart(weekStart)) return Week();
    const QByteArray data = readSmallFile(m_dataDir + QLatin1String("/weeks/") + weekStart + QLatin1String(".json"), 16 * 1024 * 1024);
    if (data.isEmpty()) return Week();
    bool ok = false;
    const QVariant payload = Json::parse(data, &ok);
    if (!ok || !Json::isMap(payload)) return Week();
    return Week::fromPayload(payload.toMap(), true);
}

void Client::saveWeekPayload(const QVariantMap &payload, const QString &weekStart)
{
    writeFileAtomically(m_dataDir + QLatin1String("/weeks/") + weekStart + QLatin1String(".json"), Json::serialize(payload));
}

QVariantMap Client::fetchWeek(Http &http, const QString &weekStart, const Session &session)
{
    const bool teacher = !session.student;
    const QVariant value = call(http, teacher ? Teacher : Student, fields1("weekStart", weekStart));
    const KtkError server = serverError(value);
    if (server.isError()) throw server;
    if (!Json::isMap(value) || !Json::isList(at(value.toMap(), "days"))) throw KtkError(KtkError::BadResponse);
    QVariantMap payload;
    payload.insert(QLatin1String("weekStart"), weekStart);
    payload.insert(QLatin1String("week"), value);
    payload.insert(QLatin1String("teacher"), teacher);
    payload.insert(QLatin1String("savedAt"), CollegeCalendar::nowMillis());
    if (!teacher) payload.insert(QLatin1String("extras"), extras(http, payload, weekStart));
    return payload;
}

/// Tasks of lessons flagged hasHomework and webinars of distance lessons; best effort, one after
/// another over the kept-alive connection.
QVariantMap Client::extras(Http &http, const QVariantMap &payload, const QString &weekStart)
{
    const Week bare = Week::fromPayload(payload, false);
    QList<Lesson> wanted;
    QSet<qint64> journals;
    const QList<Lesson> all = bare.allLessons();
    for (int i = 0; i < all.size() && wanted.size() < MaxExtras; ++i) {
        const Lesson &lesson = all.at(i);
        if (lesson.journalId > 0 && (lesson.hasHomework || lesson.remote) && !journals.contains(lesson.journalId)) {
            journals.insert(lesson.journalId);
            wanted.append(lesson);
        }
    }
    QVariantMap out;
    for (int i = 0; i < wanted.size(); ++i) {
        const Lesson &lesson = wanted.at(i);
        Seroval::Fields fields;
        fields.append(qMakePair(QString::fromLatin1("weekStart"), QVariant(weekStart)));
        fields.append(qMakePair(QString::fromLatin1("journalId"), QVariant(lesson.journalId)));
        QVariantMap entry;
        if (lesson.hasHomework) {
            const QVariant value = quietCall(http, StudentHomework, fields);
            if (Json::isMap(value) && !serverError(value).isError()) entry.insert(QLatin1String("homework"), value);
        }
        if (lesson.remote) {
            const QVariant value = quietCall(http, StudentWebinar, fields);
            if (Json::isMap(value) && !serverError(value).isError()) entry.insert(QLatin1String("webinar"), value);
        }
        if (!entry.isEmpty()) out.insert(QString::number(lesson.journalId), entry);
    }
    return out;
}

Week Client::loadWeek(const QString &weekStart, KtkError *error)
{
    *error = KtkError();
    const Session current = session();
    if (current.isNull() || !CollegeCalendar::isWeekStart(weekStart)) {
        *error = KtkError(KtkError::LoginRequired);
        return Week();
    }
    const int generation = sessionGeneration();
    Week week;
    try {
        Http http;
        const QVariantMap payload = fetchWeek(http, weekStart, current);
        week = Week::fromPayload(payload, true);
        // Not after a sign-out meanwhile: the saved weeks belong to the session that asked.
        if (!week.isNull() && sessionGeneration() == generation) saveWeekPayload(payload, weekStart);
        else *error = KtkError(KtkError::BadResponse);
    } catch (const KtkError &e) {
        *error = e;
    }
    if (week.isNull() && error->isError() && !error->isLoginRequired()) {
        week = cachedWeek(weekStart);
        if (!week.isNull()) week.offline = true;
    }
    return week;
}

// -- files ---------------------------------------------------------------------------------------

QString Client::downloadFile(const TaskFile &file)
{
    const QString directory = m_filesDir + QLatin1Char('/') + QString::number(file.fileId);
    const QString target = directory + QLatin1Char('/') + file.safeName();
    if (QFileInfo(target).isFile()) return target;
    QDir().mkpath(directory);
    const QString partial = target + QLatin1String(".part");

    QMap<QString, QString> cookies;
    {
        QMutexLocker locker(&m_lock);
        cookies = m_session.cookies;
    }
    HttpRequest req = request(QLatin1String(Base) + QLatin1String("/api/timetable/students/self/lessons/")
                              + QString::number(file.journalId) + QLatin1String("/files/") + QString::number(file.fileId)
                              + QLatin1String("?weekStart=") + Http::escape(file.weekStart), &cookies);
    req.removeHeader("Origin");
    req.setHeader("Accept", "*/*");
    req.limit = FileLimit;
    req.timeoutMs = 120000;
    req.outputPath = partial;
    try {
        Http http;
        const HttpResponse response = send(http, req, false);
        if (response.status == 401 || response.status == 403) throw KtkError(KtkError::LoginRequired);
        if (response.status != 200) throw KtkError::fromStatus(response.status);
        QFile::remove(target);
        if (!QFile::rename(partial, target)) throw KtkError(KtkError::File, QString::fromUtf8("Не удалось сохранить файл."));
    } catch (...) {
        QFile::remove(partial);
        throw;
    }
    return target;
}

// -- discovery -----------------------------------------------------------------------------------

/// The page and its scripts name every server function; classify each id by the code around it,
/// exactly like the bot's Candidates::add. One crawl per launch serves every call.
QString Client::rediscover(Http &http, Fn fn)
{
    if (fn == StudentHomework || fn == StudentWebinar) return QString();   // not told apart by discovery yet
    QMutexLocker locker(&m_crawlLock);
    if (!m_crawled) {
        m_discovered = crawl(http);
        m_crawled = true;
    }
    return m_discovered.value(fnId(fn));
}

/// Same-origin redirects only.
bool Client::fetchText(Http &http, const QString &url, QString *text, QString *finalUrl)
{
    QUrl current(url);
    const QString host = current.host();
    for (int hop = 0; hop < 5; ++hop) {
        HttpRequest req = request(QString::fromLatin1(current.toEncoded()), 0);
        req.removeHeader("Origin");
        req.setHeader("Accept", "application/json,text/html,*/*");
        req.limit = JsonLimit;
        HttpResponse response;
        try {
            response = send(http, req, true);
        } catch (const KtkError &) {
            return false;
        }
        if (response.status >= 300 && response.status < 400 && !response.location.isEmpty()) {
            const QUrl next = current.resolved(QUrl(response.location));
            if (next.scheme() != QLatin1String("https") || next.host() != host) return false;
            current = next;
            continue;
        }
        if (response.status != 200) return false;
        *text = QString::fromUtf8(response.body.constData(), response.body.size());
        *finalUrl = QString::fromLatin1(current.toEncoded());
        return true;
    }
    return false;
}

namespace
{
    QStringList scriptUrls(const QString &text, const QString &baseUrl)
    {
        const QUrl base(baseUrl);
        QStringList urls;
        QSet<QString> seen;
        QRegExp patterns[2] = {
            QRegExp(QLatin1String("<script[^>]+src=[\"']([^\"']+)[\"']"), Qt::CaseInsensitive),
            QRegExp(QLatin1String("[\"']([^\"']+\\.js(?:\\?[^\"']*)?)[\"']"))
        };
        for (int p = 0; p < 2; ++p) {
            QRegExp &rx = patterns[p];
            int pos = 0;
            while (urls.size() < 128 && (pos = rx.indexIn(text, pos)) >= 0) {
                pos += qMax(1, rx.matchedLength());
                const QUrl url = base.resolved(QUrl(rx.cap(1)));
                const QString s = QString::fromLatin1(url.toEncoded());
                if (url.isValid() && url.scheme() == QLatin1String("https") && url.host() == base.host() && !seen.contains(s)) {
                    seen.insert(s);
                    urls.append(s);
                }
            }
        }
        return urls;
    }

    void scan(const QString &text, QHash<QString, QString> &found)
    {
        const QString marker = QLatin1String("/_serverFn/");
        const int length = text.size();
        QRegExp rx(QLatin1String("/_serverFn/([0-9a-fA-F]{32,64})"));
        int pos = 0;
        while ((pos = rx.indexIn(text, pos)) >= 0) {
            const QString path = rx.cap(0);
            const int index = pos;
            const int after = pos + rx.matchedLength();
            pos = after;
            const int previous = index - marker.size() >= 0 ? text.lastIndexOf(marker, index - marker.size()) : -1;
            const int previousFn = previous < 0 ? 0 : previous + marker.size();
            const int next = text.indexOf(marker, after);
            const int nextFn = next < 0 ? length : next;

            int semicolon = index > 0 ? text.lastIndexOf(QLatin1Char(';'), index - 1) : -1;
            if (semicolon < previousFn) {
                semicolon = index > 0 ? text.lastIndexOf(QLatin1Char('\n'), index - 1) : -1;
                if (semicolon < previousFn) semicolon = -1;
            }
            const int start = semicolon < 0 ? previousFn : semicolon + 1;

            int end = text.indexOf(QLatin1Char(';'), after);
            if (end < 0 || end >= nextFn) {
                end = text.indexOf(QLatin1String("createServerFn"), after);
                if (end >= 0 && end + 14 > nextFn) end = -1;
            }
            const int stop = end < 0 ? nextFn : end;
            const QString snippet = start <= stop ? text.mid(start, stop - start) : path;

            QString name;
            if (snippet.contains(QLatin1String("collegeId")) || snippet.contains(QLatin1String("fingerprint"))) {
                name = fnId(Client::SignIn);
            } else if (snippet.contains(QLatin1String("groupId")) && snippet.contains(QLatin1String("weekStart"))) {
                name = fnId(Client::Group);
            } else if (snippet.contains(QLatin1String("weekStart"))) {
                name = snippet.contains(QLatin1String("teacher")) || snippet.contains(QLatin1String("Teacher"))
                    ? fnId(Client::Teacher) : fnId(Client::Student);
            } else if (snippet.contains(QLatin1String("revoke")) || snippet.contains(QLatin1String("logout"))
                       || snippet.contains(QLatin1String("signOut")) || snippet.contains(QLatin1String("sign-out"))) {
                name = fnId(Client::Logout);
            }
            if (!name.isEmpty() && !found.contains(name)) found.insert(name, path);
        }
    }
}

QHash<QString, QString> Client::crawl(Http &http)
{
    QHash<QString, QString> found;
    QString page, pageUrl;
    if (!fetchText(http, QLatin1String(Base) + QLatin1Char('/'), &page, &pageUrl)) return found;
    scan(page, found);
    QStringList queue = scriptUrls(page, pageUrl);
    QSet<QString> seen;
    while (!queue.isEmpty() && seen.size() < MaxScripts) {
        const QString url = queue.takeFirst();
        if (seen.contains(url)) continue;
        seen.insert(url);
        QString script, scriptUrl;
        if (!fetchText(http, url, &script, &scriptUrl)) continue;
        scan(script, found);
        const QStringList next = scriptUrls(script, url);
        for (int i = 0; i < next.size(); ++i)
            if (!seen.contains(next.at(i)) && queue.size() < 256) queue.append(next.at(i));
    }
    return found;
}
