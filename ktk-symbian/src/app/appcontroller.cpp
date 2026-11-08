// KTK for Symbian - what the QML pages see as `app`.
#include "appcontroller.h"
#include "collegecalendar.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QNetworkConfigurationManager>
#include <QNetworkSession>
#include <QSettings>
#include <QStringList>
#include <QUrl>

#if QT_VERSION >= 0x050000
#include <QStandardPaths>
#endif

#define KTK_STRINGIFY2(x) #x
#define KTK_STRINGIFY(x) KTK_STRINGIFY2(x)

namespace
{
    /// Back after this long: the week may have changed.
    const qint64 StaleMs = 5 * 60 * 1000;
    /// Worker threads run BearSSL and the JSON trees; Symbian's default thread stack is tiny.
    const uint JobStackSize = 256 * 1024;

    QString dataDirectory()
    {
#if QT_VERSION >= 0x050000
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
        QString dir = QDesktopServices::storageLocation(QDesktopServices::DataLocation);
#endif
        if (dir.isEmpty()) dir = QDir::homePath() + QLatin1String("/.ktk");
        return dir;
    }

    /// Downloaded files must be where other apps can open them: the private folder is closed to
    /// them on Symbian. Mass memory when the phone has it (E: on the N8), the phone memory else.
    QString filesDirectory(const QString &dataDir)
    {
#if defined(Q_OS_SYMBIAN)
        Q_UNUSED(dataDir);
        if (QDir(QLatin1String("E:/")).exists()) return QString::fromLatin1("E:/KTKLegacy");
        return QString::fromLatin1("C:/Data/KTKLegacy");
#else
        return dataDir + QLatin1String("/files");
#endif
    }

    /// Colours of the badge, readable on the dark Symbian theme.
    QString badgeColor(const QString &badge)
    {
        if (badge.startsWith(QLatin1Char('5'))) return QString::fromLatin1("#5fd068");
        if (badge.startsWith(QLatin1Char('4'))) return QString::fromLatin1("#6aa8ff");
        if (badge.startsWith(QLatin1Char('3'))) return QString::fromLatin1("#f0a43a");
        if (badge.startsWith(QLatin1Char('2')) || badge.startsWith(QLatin1Char('1')) || badge == QString::fromUtf8("Н"))
            return QString::fromLatin1("#ff6b6b");
        return QString::fromLatin1("#bbbbbb");
    }

    QString detailText(const Lesson &lesson, bool teacher)
    {
        QStringList lines;
        QStringList first;
        if (!lesson.timeText().isEmpty()) first << lesson.timeText();
        if (lesson.remote) first << QString::fromUtf8("дистанционно");
        else if (!lesson.hallText().isEmpty()) first << QString::fromUtf8("каб. ") + lesson.hallText();
        if (!lesson.subgroupText().isEmpty()) first << lesson.subgroupText();
        if (!first.isEmpty()) lines << first.join(QString::fromUtf8(" · "));
        const QString who = teacher
            ? (lesson.groupId.isEmpty() ? QString() : QString::fromUtf8("Группа ") + lesson.groupId)
            : lesson.shortTeacher();
        if (!who.isEmpty()) lines << who;
        QStringList extras;
        if (!lesson.task.isEmpty() || lesson.hasHomework) extras << QString::fromUtf8("задание");
        if (!lesson.files.isEmpty()) extras << QString::fromUtf8("файлов: %1").arg(lesson.files.size());
        if (!lesson.webinar.isEmpty()) extras << QString::fromUtf8("вебинар");
        if (!extras.isEmpty()) lines << QString::fromUtf8("● ") + extras.join(QString::fromUtf8(" · "));
        return lines.join(QLatin1String("\n"));
    }

    QVariantMap item(const char *kind)
    {
        QVariantMap m;
        m.insert(QLatin1String("kind"), QString::fromLatin1(kind));
        return m;
    }

    QVariantMap rowItem(const QString &label, const QString &value, const QString &color = QString(), bool bold = false)
    {
        QVariantMap m = item("row");
        m.insert(QLatin1String("label"), label);
        m.insert(QLatin1String("value"), value);
        m.insert(QLatin1String("color"), color);
        m.insert(QLatin1String("bold"), bold);
        return m;
    }

    QVariantMap textItem(const char *kind, const QString &text)
    {
        QVariantMap m = item(kind);
        m.insert(QLatin1String("text"), text);
        return m;
    }
}

// -- Job -----------------------------------------------------------------------------------------

void Job::run()
{
    try {
        switch (kind) {
        case SignInJob:
            session = client->signIn(login, password);
            break;
        case WeekJob:
            week = client->loadWeek(weekStart, &error);
            break;
        case FileJob:
            path = client->downloadFile(file);
            break;
        case LogoutJob:
            client->logoutOnSite(oldSession);
            break;
        }
    } catch (const KtkError &e) {
        error = e;
    } catch (...) {
        error = KtkError(KtkError::BadResponse);
    }
    password.clear();
}

// -- AppController -------------------------------------------------------------------------------

AppController::AppController(QObject *parent)
    : QObject(parent), m_loading(false), m_gradesMode(false), m_scrolledToToday(false), m_signingIn(false),
      m_weekToken(0), m_rowsDay(0), m_lessonTeacher(false), m_downloadingFile(-1), m_netManager(0), m_netSession(0),
      m_networkReady(false), m_networkOpening(false)
{
    const QString data = dataDirectory();
    m_client = new Client(data, filesDirectory(data));
    m_weekStart = CollegeCalendar::currentWeekStart();
    qApp->installEventFilter(this);
}

AppController::~AppController()
{
    // Jobs still running keep using the client; it is left alive for them as the process ends.
}

QString AppController::version() const
{
#ifdef APP_VERSION
    return QString::fromLatin1(KTK_STRINGIFY(APP_VERSION));
#else
    return QString::fromLatin1("dev");
#endif
}

bool AppController::signedIn() const { return m_client->hasSession(); }
QString AppController::login() const { return m_client->session().login; }

QString AppController::lastLogin() const
{
    QSettings settings;
    return settings.value(QLatin1String("lastLogin")).toString();
}

QString AppController::weekTitle() const { return CollegeCalendar::weekTitle(m_weekStart); }

void AppController::start()
{
    if (signedIn()) showWeek(m_weekStart);
}

// -- network -------------------------------------------------------------------------------------

void AppController::startJob(Job *job)
{
    job->setStackSize(JobStackSize);
    job->generation = m_client->sessionGeneration();
    connect(job, SIGNAL(finished()), this, SLOT(onJobFinished()));
    m_pending.append(job);

    // On the phone a socket without an open QNetworkSession either fails or asks for an access
    // point every time. Open the default configuration once, before the first job.
    if (!m_networkReady && !m_networkOpening) {
        if (!m_netManager) m_netManager = new QNetworkConfigurationManager(this);
        const QNetworkConfiguration config = m_netManager->defaultConfiguration();
        if (!config.isValid() || !(m_netManager->capabilities() & QNetworkConfigurationManager::NetworkSessionRequired)) {
            m_networkReady = true;
        } else {
            delete m_netSession;
            m_netSession = new QNetworkSession(config, this);
            connect(m_netSession, SIGNAL(opened()), this, SLOT(onNetworkOpened()));
            connect(m_netSession, SIGNAL(error(QNetworkSession::SessionError)), this, SLOT(onNetworkError()));
            m_networkOpening = true;
            m_netSession->open();
        }
    }
    if (m_networkReady) runPendingJobs();
}

void AppController::runPendingJobs()
{
    const QList<Job *> jobs = m_pending;
    m_pending.clear();
    for (int i = 0; i < jobs.size(); ++i) jobs.at(i)->start();
}

void AppController::onNetworkOpened()
{
    m_networkOpening = false;
    m_networkReady = true;
    runPendingJobs();
}

void AppController::onNetworkError()
{
    m_networkOpening = false;
    const QString reason = m_netSession ? m_netSession->errorString() : QString();
    qWarning("network session error: %s", qPrintable(reason));
    if (m_netSession && m_netSession->isOpen()) {
        m_networkReady = true;
        runPendingJobs();
        return;
    }
    // Not connected: the next job asks again (the user may pick another access point).
    m_networkReady = false;
    const QList<Job *> jobs = m_pending;
    m_pending.clear();
    for (int i = 0; i < jobs.size(); ++i) {
        jobs.at(i)->failWith(KtkError(KtkError::Network, QString::fromUtf8("нет подключения к интернету")));
        QMetaObject::invokeMethod(this, "deliverJob", Qt::QueuedConnection, Q_ARG(QObject *, jobs.at(i)));
    }
}

void AppController::onJobFinished()
{
    handleJob(static_cast<Job *>(sender()));
}

void AppController::deliverJob(QObject *job)
{
    handleJob(static_cast<Job *>(job));
}

void AppController::handleJob(Job *job)
{
    if (!job) return;
    switch (job->kind) {
    case Job::SignInJob: finishSignIn(job); break;
    case Job::WeekJob: finishWeek(job); break;
    case Job::FileJob: finishFile(job); break;
    case Job::LogoutJob: break;
    }
    job->deleteLater();
}

bool AppController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationActivate && signedIn()) {
        // Back after a while: the week may have changed, and "сегодня" may have moved.
        if (!m_loading && !m_week.isNull()
            && (m_week.updatedAt == 0 || CollegeCalendar::nowMillis() - m_week.updatedAt > StaleMs)) {
            reload();
        } else if (CollegeCalendar::today() != m_rowsDay) {
            rebuild();
        }
    }
    return QObject::eventFilter(watched, event);
}

// -- sign in / out -------------------------------------------------------------------------------

void AppController::signIn(const QString &login, const QString &password)
{
    if (m_signingIn) return;
    if (login.trimmed().isEmpty() || password.trimmed().isEmpty()) {
        emit signInFailed(QString::fromUtf8("Введите логин и пароль."));
        return;
    }
    m_signingIn = true;
    emit signingInChanged();
    Job *job = new Job(Job::SignInJob, m_client);
    job->login = login.trimmed();
    job->password = password;
    startJob(job);
}

void AppController::finishSignIn(Job *job)
{
    m_signingIn = false;
    emit signingInChanged();
    if (job->error.isError()) {
        emit signInFailed(job->error.text());
        return;
    }
    QSettings settings;
    settings.setValue(QLatin1String("lastLogin"), job->session.login);
    m_week = Week();
    m_error = KtkError();
    m_gradesMode = false;
    emit sessionChanged();
    showWeek(CollegeCalendar::currentWeekStart());
}

void AppController::signOut()
{
    const Session old = m_client->session();
    m_client->signOutLocally();
    m_week = Week();
    m_error = KtkError();
    ++m_weekToken;
    setLoading(false);
    rebuild();
    emit sessionChanged();
    if (!old.isNull() && !old.sessionId.isEmpty()) {
        Job *job = new Job(Job::LogoutJob, m_client);
        job->oldSession = old;
        startJob(job);
    }
}

// -- weeks ---------------------------------------------------------------------------------------

void AppController::setLoading(bool loading)
{
    m_loading = loading;
}

void AppController::showWeek(const QString &start)
{
    m_weekStart = CollegeCalendar::isWeekStart(start) ? start : CollegeCalendar::currentWeekStart();
    m_scrolledToToday = false;
    m_week = m_client->cachedWeek(m_weekStart);
    m_week.offline = false;
    m_error = KtkError();
    rebuild();
    reload();
}

void AppController::showCurrentWeek() { showWeek(CollegeCalendar::currentWeekStart()); }
void AppController::previousWeek() { showWeek(CollegeCalendar::shiftWeek(m_weekStart, -1)); }
void AppController::nextWeek() { showWeek(CollegeCalendar::shiftWeek(m_weekStart, 1)); }

void AppController::reload()
{
    if (!signedIn()) return;
    setLoading(true);
    rebuild();
    Job *job = new Job(Job::WeekJob, m_client);
    job->weekStart = m_weekStart;
    job->token = ++m_weekToken;
    startJob(job);
}

void AppController::finishWeek(Job *job)
{
    if (job->token != m_weekToken) return;   // the user went to another week meanwhile
    setLoading(false);
    if (job->error.isLoginRequired()) {
        if (m_client->sessionGeneration() == job->generation) {
            m_client->signOutLocally();
            m_week = Week();
            m_error = KtkError();
            rebuild();
            emit sessionChanged();
            emit loginRequired(job->error.text());
        }
        return;
    }
    if (!job->week.isNull()) m_week = job->week;
    m_error = job->error;
    rebuild();
    if (!m_scrolledToToday && !m_gradesMode && !m_week.isNull()) {
        m_scrolledToToday = true;
        for (int i = 1; i < m_rows.size(); ++i) {
            const QVariantMap row = m_rows.at(i).toMap();
            if (row.value(QLatin1String("kind")).toString() == QLatin1String("header") && row.value(QLatin1String("today")).toBool()) {
                emit scrollToRow(i);
                break;
            }
        }
    }
}

void AppController::setGradesMode(bool grades)
{
    if (grades == m_gradesMode) return;
    m_gradesMode = grades;
    rebuild();
}

void AppController::rebuild()
{
    QVariantList rows;
    bool anyDay = false;
    int lessonIndex = 0;
    if (!m_week.isNull()) {
        for (int d = 0; d < m_week.days.size(); ++d) {
            const Day &day = m_week.days.at(d);
            QVariantList lessons;
            for (int i = 0; i < day.lessons.size(); ++i, ++lessonIndex) {
                const Lesson &lesson = day.lessons.at(i);
                if (m_gradesMode && !lesson.hasGrade()) continue;
                QVariantMap row = item("lesson");
                row.insert(QLatin1String("title"), QString::number(lesson.pair) + QLatin1String(". ") + lesson.discipline);
                row.insert(QLatin1String("details"), detailText(lesson, m_week.teacher));
                const QString badge = m_week.teacher ? QString() : lesson.badgeText();
                row.insert(QLatin1String("badge"), badge.isEmpty() ? QString::fromUtf8("›") : badge);
                row.insert(QLatin1String("badgeColor"), badge.isEmpty() ? QString::fromLatin1("#888888") : badgeColor(badge));
                row.insert(QLatin1String("badgeSmall"), badge.size() > 2);
                row.insert(QLatin1String("lessonIndex"), lessonIndex);
                lessons.append(row);
            }
            if (m_gradesMode && lessons.isEmpty()) continue;
            if (!m_gradesMode && lessons.isEmpty() && CollegeCalendar::weekdayOfDate(day.date) == 6) continue;   // an empty Sunday
            anyDay = true;
            QVariantMap header = item("header");
            const bool today = CollegeCalendar::isToday(day.date);
            const QString title = CollegeCalendar::dayTitle(day.date);
            header.insert(QLatin1String("title"), today ? title + QString::fromUtf8(" · сегодня") : title);
            header.insert(QLatin1String("today"), today);
            rows.append(header);
            if (lessons.isEmpty()) rows.append(textItem("message", QString::fromUtf8("Пар нет")));
            rows += lessons;
        }
    }
    if (!anyDay) {
        QString message;
        if (m_week.isNull()) {
            message = m_loading ? QString::fromUtf8("Загрузка…")
                    : (m_error.isError() ? m_error.text() : QString::fromUtf8("Нет данных."));
        } else {
            message = m_gradesMode ? QString::fromUtf8("На этой неделе оценок нет.") : QString::fromUtf8("На этой неделе пар нет.");
        }
        rows.append(textItem("message", message));
    }
    m_rows = rows;
    m_rowsDay = CollegeCalendar::today();

    m_banner.clear();
    if (!m_week.isNull() && m_week.offline && m_error.isError()) {
        m_banner = m_week.updatedAt != 0
            ? m_error.text() + QString::fromUtf8("\nПоказано сохранённое ") + CollegeCalendar::shortDateTime(m_week.updatedAt) + QLatin1Char('.')
            : m_error.text();
    }
    emit weekChanged();
}

// -- lesson --------------------------------------------------------------------------------------

QString AppController::openLesson(int lessonIndex)
{
    const QList<Lesson> lessons = m_week.allLessons();
    if (lessonIndex < 0 || lessonIndex >= lessons.size()) return QString();
    m_lesson = lessons.at(lessonIndex);
    m_lessonTeacher = m_week.teacher;
    m_downloadingFile = -1;
    emit downloadChanged();
    return QString::fromUtf8("%1 пара").arg(m_lesson.pair);
}

QVariantList AppController::lessonItems() const
{
    const Lesson &l = m_lesson;
    QVariantList items;
    items.append(textItem("title", l.disciplineFull.isEmpty() ? l.discipline : l.disciplineFull));

    QString when = CollegeCalendar::dayTitle(l.date);
    if (!l.timeText().isEmpty()) when = l.timeText() + QLatin1Char('\n') + when;
    items.append(rowItem(QString::fromUtf8("Когда"), when));
    if (l.remote) items.append(rowItem(QString::fromUtf8("Кабинет"), QString::fromUtf8("Дистанционно")));
    else if (!l.hallText().isEmpty()) items.append(rowItem(QString::fromUtf8("Кабинет"), l.hallText()));
    if (m_lessonTeacher) {
        if (!l.groupId.isEmpty()) items.append(rowItem(QString::fromUtf8("Группа"), l.groupId));
    } else if (!l.teacher.isEmpty()) {
        items.append(rowItem(QString::fromUtf8("Препод."), l.teacher));
    }
    if (!l.pairTypeName.isEmpty()) items.append(rowItem(QString::fromUtf8("Тип"), l.pairTypeName));
    if (!l.subgroupText().isEmpty()) items.append(rowItem(QString::fromUtf8("Подгруппа"), l.subgroupText()));

    if (!m_lessonTeacher && l.hasGrade()) {
        items.append(textItem("header", QString::fromUtf8("Оценка")));
        if (!l.gradeText().isEmpty()) items.append(rowItem(QString::fromUtf8("Оценка"), l.gradeText(), badgeColor(l.gradeText()), true));
        if (!l.markText().isEmpty()) items.append(rowItem(QString::fromUtf8("Отметка"), l.markText()));
    }

    if (!l.task.isEmpty() || !l.deadline.isEmpty() || l.hasHomework) {
        const bool any = !l.task.isEmpty() || !l.deadline.isEmpty();
        if (any || l.files.isEmpty()) items.append(textItem("header", QString::fromUtf8("Задание")));
        if (!l.task.isEmpty()) items.append(textItem("text", l.task));
        if (!l.deadline.isEmpty()) items.append(rowItem(QString::fromUtf8("Срок"), CollegeCalendar::deadlineText(l.deadline)));
        if (!any && l.files.isEmpty())
            items.append(textItem("text", QString::fromUtf8("Задание есть, но не загрузилось. Обновите неделю.")));
    }

    if (!l.webinar.isEmpty()) {
        items.append(textItem("header", QString::fromUtf8("Вебинар")));
        items.append(textItem("link", l.webinar));
        items.append(textItem("note", QString::fromUtf8("Браузер телефона может не открыть современный сайт: ссылку можно скопировать.")));
    }

    if (!l.files.isEmpty()) {
        items.append(textItem("header", QString::fromUtf8("Файлы")));
        for (int i = 0; i < l.files.size(); ++i) {
            QVariantMap f = textItem("file", l.files.at(i).fileName());
            f.insert(QLatin1String("fileIndex"), i);
            items.append(f);
        }
    }
    return items;
}

void AppController::downloadFile(int fileIndex)
{
    if (m_downloadingFile >= 0 || fileIndex < 0 || fileIndex >= m_lesson.files.size()) return;
    m_downloadingFile = fileIndex;
    emit downloadChanged();
    Job *job = new Job(Job::FileJob, m_client);
    job->file = m_lesson.files.at(fileIndex);
    job->fileIndex = fileIndex;
    startJob(job);
}

void AppController::finishFile(Job *job)
{
    if (m_downloadingFile == job->fileIndex) {
        m_downloadingFile = -1;
        emit downloadChanged();
    }
    if (job->error.isError()) {
        emit notice(job->error.text());
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(job->path)))
        emit notice(QString::fromUtf8("Файл сохранён (%1), но на телефоне нет приложения, которое его откроет.")
                    .arg(QDir::toNativeSeparators(job->path)));
}

void AppController::openUrl(const QString &url)
{
    if (!QDesktopServices::openUrl(QUrl(url))) emit notice(QString::fromUtf8("Не удалось открыть ссылку."));
}

void AppController::copyText(const QString &text)
{
    QApplication::clipboard()->setText(text);
    emit notice(QString::fromUtf8("Скопировано"));
}
