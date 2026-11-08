// Headless AppController run: a saved session the site rejects must end in loginRequired, with
// the week rows rebuilt on the way (loading, then signed out).
#include "appcontroller.h"
#include "probe.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QLatin1String("KTKLegacy"));
    app.setApplicationName(QLatin1String("KTKLegacy"));
    AppController controller;
    Probe probe(&controller);
    QTextStream(stdout) << "signedIn at start: " << controller.signedIn() << "\n";
    controller.start();
    QTimer::singleShot(30000, &app, SLOT(quit()));
    app.exec();
    QTextStream(stdout) << (probe.loginRequiredSeen && !controller.signedIn() ? "PASS\n" : "FAIL\n");
    return probe.loginRequiredSeen ? 0 : 1;
}
