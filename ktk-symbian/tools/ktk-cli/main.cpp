// KTK desktop harness - exercises the core against the live site, so the protocol can be checked
// on a PC before a phone build.
//
//   ktk-cli get <url>              two GETs over one kept-alive TLS connection
//   ktk-cli crawl                  find the server function addresses in the site's scripts
//   ktk-cli selftest               JSON, seroval and calendar checks, no network
//   ktk-cli signin                 login and password from KTK_LOGIN / KTK_PASSWORD
//   ktk-cli week [yyyy-mm-dd]      the week as the phone would show it (needs a session)
//   ktk-cli file <lesson#> <file#> download a task file of the current week
//   ktk-cli signout
//
// KTK_DATA_DIR (default ./ktk-data) holds the session and saved weeks, like the phone's
// private folder.
#include "client.h"
#include "collegecalendar.h"
#include "http.h"
#include "json.h"
#include "seroval.h"

#include <QCoreApplication>
#include <QDir>
#include <QStringList>
#include <QTextStream>

class ClientProbe
{
public:
    static QHash<QString, QString> crawl(Client &client);
};

QHash<QString, QString> ClientProbe::crawl(Client &client)
{
    Http http;
    return client.crawl(http);
}

namespace
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    int check(bool ok, const char *what)
    {
        out << (ok ? "ok    " : "FAIL  ") << what << "\n";
        return ok ? 0 : 1;
    }

    int selftest()
    {
        int failures = 0;
        bool ok = false;
        QVariant v = Json::parse("{\"a\":[1,2.5,\"x\\u00e9\\n\",true,null],\"b\":{}}", &ok);
        failures += check(ok && v.toMap().value(QLatin1String("a")).toList().size() == 5, "json parse");
        failures += check(Json::serialize(v) == "{\"a\":[1,2.5,\"x\xc3\xa9\\n\",true,null],\"b\":{}}", "json round trip");
        Json::parse("[1,", &ok);
        failures += check(!ok, "json rejects truncated");
        Json::parse("{\"a\":1}\n{\"b\":2}", &ok, true);
        failures += check(ok, "json first value only");

        Seroval::Fields fields;
        fields.append(qMakePair(QString::fromLatin1("weekStart"), QVariant(QString::fromLatin1("2026-11-02"))));
        fields.append(qMakePair(QString::fromLatin1("journalId"), QVariant(qint64(42))));
        const QByteArray request = Seroval::request(fields);
        failures += check(request == "{\"f\":127,\"m\":[],\"t\":{\"i\":0,\"o\":0,\"p\":{\"k\":[\"data\"],\"v\":[{\"i\":1,\"o\":0,"
                                     "\"p\":{\"k\":[\"weekStart\",\"journalId\"],\"v\":[{\"s\":\"2026-11-02\",\"t\":1},{\"s\":42,\"t\":0}]},"
                                     "\"t\":10}]},\"t\":10}}", "seroval request");
        // {result: {s: "a<b", n: 5, ok: true, list: [ref to s]}}
        const QVariant answer = Json::parse(
            "{\"t\":{\"t\":10,\"i\":0,\"p\":{\"k\":[\"result\"],\"v\":[{\"t\":10,\"i\":1,\"p\":{\"k\":[\"s\",\"n\",\"ok\",\"list\"],"
            "\"v\":[{\"t\":1,\"i\":2,\"s\":\"a\\\\x3Cb\"},{\"t\":0,\"s\":5},{\"t\":2,\"s\":2},{\"t\":9,\"a\":[{\"t\":4,\"i\":2}]}]}}]}},\"f\":127,\"m\":[]}", &ok);
        const QVariantMap decoded = Json::map(Seroval::decode(answer));
        failures += check(decoded.value(QLatin1String("s")).toString() == QLatin1String("a<b")
                          && Json::number(decoded.value(QLatin1String("n")), 0) == 5
                          && Json::boolean(decoded.value(QLatin1String("ok")))
                          && decoded.value(QLatin1String("list")).toList().value(0).toString() == QLatin1String("a<b"),
                          "seroval decode with reference");

        failures += check(CollegeCalendar::weekTitle(QLatin1String("2026-11-02")) == QString::fromUtf8("2–8 ноября"), "week title");
        failures += check(CollegeCalendar::weekTitle(QLatin1String("2026-11-30")) == QString::fromUtf8("30 ноября – 6 декабря"), "week title across months");
        failures += check(CollegeCalendar::dayTitle(QLatin1String("2026-11-07")) == QString::fromUtf8("Суббота, 7 ноября"), "day title");
        failures += check(CollegeCalendar::shiftWeek(QLatin1String("2026-11-04"), 1) == QLatin1String("2026-11-09"), "shift week");
        failures += check(CollegeCalendar::deadlineText(QLatin1String("2026-11-05T18:59:00.000Z")) == QLatin1String("05.11.2026 23:59"), "deadline UTC");
        failures += check(CollegeCalendar::deadlineText(QLatin1String("2026-11-05T23:59:00+05:00")) == QLatin1String("05.11.2026 23:59"), "deadline +05:00");
        failures += check(CollegeCalendar::timeFromMinutes(485) == QLatin1String("08:05"), "time");
        failures += check(Http::escape(QString::fromUtf8("a b/ж~")) == QLatin1String("a%20b%2F%D0%B6~"), "url escape");

        TaskFile file;
        file.fileId = 7;
        file.caption = QString::fromUtf8("../Отчёт: итог");
        file.extension = QLatin1String(".PDF");
        failures += check(file.safeName() == QString::fromUtf8("_Отчёт_ итог.PDF"), "safe file name");
        out << (failures ? "selftest failed\n" : "selftest passed\n");
        return failures ? 1 : 0;
    }

    void printWeek(const Week &week)
    {
        out << "week " << week.weekStart << (week.teacher ? " (teacher)" : "") << (week.offline ? " OFFLINE" : "") << "\n";
        int index = 0;
        for (int d = 0; d < week.days.size(); ++d) {
            const Day &day = week.days.at(d);
            out << "\n== " << CollegeCalendar::dayTitle(day.date) << "\n";
            for (int i = 0; i < day.lessons.size(); ++i, ++index) {
                const Lesson &l = day.lessons.at(i);
                out << "  [" << index << "] " << l.pair << ". " << l.discipline;
                if (!l.badgeText().isEmpty()) out << "  {" << l.badgeText() << "}";
                out << "\n      " << l.timeText() << " | " << (l.remote ? QString::fromUtf8("дистанционно") : l.hallText())
                    << " | " << l.shortTeacher() << " | " << l.subgroupText() << "\n";
                if (!l.task.isEmpty()) out << "      task: " << l.task.left(80).replace(QLatin1Char('\n'), QLatin1Char(' ')) << "\n";
                if (!l.deadline.isEmpty()) out << "      deadline: " << CollegeCalendar::deadlineText(l.deadline) << "\n";
                if (!l.webinar.isEmpty()) out << "      webinar: " << l.webinar << "\n";
                for (int f = 0; f < l.files.size(); ++f) out << "      file " << f << ": " << l.files.at(f).fileName() << "\n";
            }
        }
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const QString command = args.value(1);
    QString dataDir = QString::fromLocal8Bit(qgetenv("KTK_DATA_DIR"));
    if (dataDir.isEmpty()) dataDir = QDir::currentPath() + QLatin1String("/ktk-data");

    try {
        if (command == QLatin1String("selftest")) return selftest();
        if (command == QLatin1String("get") && args.size() >= 3) {
            Http http;
            HttpRequest request;
            request.url = args.at(2);
            for (int i = 0; i < 2; ++i) {
                const HttpResponse response = http.perform(request);
                out << "status " << response.status << ", " << response.body.size() << " bytes";
                if (!response.location.isEmpty()) out << ", location " << response.location;
                out << "\n";
                out.flush();
            }
            return 0;
        }
        Client client(dataDir, dataDir + QLatin1String("/files"));
        if (command == QLatin1String("crawl")) {
            const QHash<QString, QString> found = ClientProbe::crawl(client);
            for (QHash<QString, QString>::const_iterator it = found.constBegin(); it != found.constEnd(); ++it)
                out << it.key() << " " << it.value() << "\n";
            return found.isEmpty() ? 1 : 0;
        }
        if (command == QLatin1String("signin")) {
            const QString login = QString::fromLocal8Bit(qgetenv("KTK_LOGIN"));
            const QString password = QString::fromLocal8Bit(qgetenv("KTK_PASSWORD"));
            if (login.isEmpty() || password.isEmpty()) {
                err << "set KTK_LOGIN and KTK_PASSWORD\n";
                return 2;
            }
            const Session s = client.signIn(login, password);
            out << "signed in as " << s.login << (s.student ? " (student)" : " (teacher)") << ", cookies: " << s.cookies.size() << "\n";
            return 0;
        }
        if (command == QLatin1String("week")) {
            const QString start = args.size() >= 3 ? args.at(2) : CollegeCalendar::currentWeekStart();
            KtkError error;
            const Week week = client.loadWeek(start, &error);
            if (!week.isNull()) printWeek(week);
            if (error.isError()) {
                err << "error: " << error.text() << "\n";
                return 1;
            }
            return 0;
        }
        if (command == QLatin1String("file") && args.size() >= 4) {
            const Week week = client.cachedWeek(CollegeCalendar::currentWeekStart());
            const QList<Lesson> lessons = week.allLessons();
            const Lesson lesson = lessons.value(args.at(2).toInt());
            const TaskFile file = lesson.files.value(args.at(3).toInt());
            if (file.fileId == 0) {
                err << "no such file\n";
                return 2;
            }
            out << client.downloadFile(file) << "\n";
            return 0;
        }
        if (command == QLatin1String("signout")) {
            const Session old = client.session();
            client.signOutLocally();
            client.logoutOnSite(old);
            out << "signed out\n";
            return 0;
        }
    } catch (const KtkError &e) {
        err << "error: " << e.text() << "\n";
        return 1;
    }
    err << "usage: ktk-cli selftest | get <url> | crawl | signin | week [yyyy-mm-dd] | file <lesson> <file> | signout\n";
    return 2;
}
