// Prints what AppController tells the pages.
#ifndef PROBE_H
#define PROBE_H

#include "appcontroller.h"

#include <QCoreApplication>
#include <QObject>
#include <QStringList>
#include <QTextStream>

class Probe : public QObject
{
    Q_OBJECT
public:
    explicit Probe(AppController *app) : loginRequiredSeen(false), m_app(app)
    {
        connect(app, SIGNAL(weekChanged()), this, SLOT(onWeek()));
        connect(app, SIGNAL(loginRequired(QString)), this, SLOT(onLoginRequired(QString)));
        connect(app, SIGNAL(sessionChanged()), this, SLOT(onSession()));
    }
    bool loginRequiredSeen;

public slots:
    void onWeek()
    {
        QTextStream out(stdout);
        QStringList kinds;
        const QVariantList rows = m_app->rows();
        for (int i = 0; i < rows.size(); ++i) {
            const QVariantMap r = rows.at(i).toMap();
            kinds << r.value(QLatin1String("kind")).toString() + QLatin1Char(':')
                     + r.value(QLatin1String("title"), r.value(QLatin1String("text"))).toString();
        }
        out << "week " << m_app->weekTitle() << (m_app->loading() ? " (loading)" : "") << " rows=" << kinds.join(QLatin1String(" | ")) << "\n";
    }
    void onLoginRequired(const QString &message)
    {
        QTextStream(stdout) << "loginRequired: " << message << "\n";
        loginRequiredSeen = true;
        QCoreApplication::quit();
    }
    void onSession() { QTextStream(stdout) << "sessionChanged, signedIn=" << m_app->signedIn() << "\n"; }

private:
    AppController *m_app;
};

#endif
