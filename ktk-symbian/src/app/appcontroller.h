// KTK for Symbian - what the QML pages see as `app`: the session, the week on screen as list
// rows, the lesson details, downloads, and the jobs that talk to the site in worker threads.
#ifndef APPCONTROLLER_H
#define APPCONTROLLER_H

#include "client.h"
#include "ktkerror.h"
#include "week.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QThread>
#include <QVariant>

class QNetworkConfigurationManager;
class QNetworkSession;

/// One call to the site in a worker thread. The result is read on the UI thread after the
/// thread has finished.
class Job : public QThread
{
public:
    enum Kind { SignInJob, WeekJob, FileJob, LogoutJob };

    Job(Kind kind, Client *client) : kind(kind), client(client), token(0), generation(0), fileIndex(-1) {}

    const Kind kind;
    Client *client;
    int token;            // which request of its kind this answers (stale answers are dropped)
    int generation;       // the session it was asked for

    // inputs
    QString login, password, weekStart;
    TaskFile file;
    Session oldSession;
    int fileIndex;

    // outputs
    KtkError error;
    Session session;
    Week week;
    QString path;

    /// Fails the job without running it (no network).
    void failWith(const KtkError &e) { error = e; }

protected:
    void run();
};

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY sessionChanged)
    Q_PROPERTY(QString login READ login NOTIFY sessionChanged)
    Q_PROPERTY(QString lastLogin READ lastLogin NOTIFY sessionChanged)
    Q_PROPERTY(bool signingIn READ signingIn NOTIFY signingInChanged)
    Q_PROPERTY(QString weekStart READ weekStart NOTIFY weekChanged)
    Q_PROPERTY(QString weekTitle READ weekTitle NOTIFY weekChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY weekChanged)
    Q_PROPERTY(bool gradesMode READ gradesMode WRITE setGradesMode NOTIFY weekChanged)
    Q_PROPERTY(bool teacher READ teacher NOTIFY weekChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY weekChanged)
    Q_PROPERTY(QString banner READ banner NOTIFY weekChanged)
    Q_PROPERTY(int downloadingFile READ downloadingFile NOTIFY downloadChanged)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit AppController(QObject *parent = 0);
    ~AppController();

    bool signedIn() const;
    QString login() const;
    QString lastLogin() const;
    bool signingIn() const { return m_signingIn; }
    QString weekStart() const { return m_weekStart; }
    QString weekTitle() const;
    bool loading() const { return m_loading; }
    bool gradesMode() const { return m_gradesMode; }
    void setGradesMode(bool grades);
    bool teacher() const { return !m_week.isNull() && m_week.teacher; }
    QVariantList rows() const { return m_rows; }
    QString banner() const { return m_banner; }
    int downloadingFile() const { return m_downloadingFile; }
    QString version() const;

    Q_INVOKABLE void signIn(const QString &login, const QString &password);
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void showWeek(const QString &start);
    Q_INVOKABLE void showCurrentWeek();
    Q_INVOKABLE void previousWeek();
    Q_INVOKABLE void nextWeek();
    Q_INVOKABLE void reload();

    /// Freezes the lesson for the lesson page (the week may reload under it) and returns the
    /// page's title.
    Q_INVOKABLE QString openLesson(int lessonIndex);
    /// The open lesson as display items: {kind: title|row|header|text|note|link|file, ...}.
    Q_INVOKABLE QVariantList lessonItems() const;
    Q_INVOKABLE void downloadFile(int fileIndex);
    Q_INVOKABLE void openUrl(const QString &url);
    Q_INVOKABLE void copyText(const QString &text);

    /// Starts: shows the saved week at once and refreshes it.
    void start();

signals:
    void sessionChanged();
    void signingInChanged();
    void weekChanged();
    void downloadChanged();
    void signInFailed(const QString &message);
    /// The site ended the session; the app has signed out locally.
    void loginRequired(const QString &message);
    void notice(const QString &message);
    /// Scroll the week list to this row (today), once per shown week.
    void scrollToRow(int row);

protected:
    bool eventFilter(QObject *watched, QEvent *event);

private slots:
    void onJobFinished();
    /// A job that failed before it could run (no network), delivered through the event loop.
    void deliverJob(QObject *job);
    void onNetworkOpened();
    void onNetworkError();

private:
    void startJob(Job *job);
    void handleJob(Job *job);
    void runPendingJobs();
    void finishSignIn(Job *job);
    void finishWeek(Job *job);
    void finishFile(Job *job);
    void rebuild();
    void setLoading(bool loading);

    Client *m_client;
    QString m_weekStart;
    Week m_week;
    KtkError m_error;
    bool m_loading;
    bool m_gradesMode;
    bool m_scrolledToToday;
    bool m_signingIn;
    int m_weekToken;
    QVariantList m_rows;
    QString m_banner;
    qint64 m_rowsDay;         // the date "сегодня" in m_rows refers to

    Lesson m_lesson;          // the lesson page's lesson
    bool m_lessonTeacher;
    int m_downloadingFile;

    QNetworkConfigurationManager *m_netManager;
    QNetworkSession *m_netSession;
    bool m_networkReady;
    bool m_networkOpening;
    QList<Job *> m_pending;
};

#endif // APPCONTROLLER_H
