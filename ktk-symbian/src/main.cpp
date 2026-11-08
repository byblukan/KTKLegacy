// KTK for Symbian - the college timetable (ktk.education-system.ru) on Symbian Anna / Belle.
#include "appcontroller.h"

#include <QApplication>
#include <QDeclarativeContext>
#include <QDeclarativeEngine>
#include <QDeclarativeView>
#include <QTextCodec>
#include <QUrl>

int main(int argc, char *argv[])
{
    // Every source file is UTF-8; Qt 4 assumes Latin-1 for C strings unless told otherwise.
    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));

    QApplication app(argc, argv);
    app.setOrganizationName(QLatin1String("KTKLegacy"));
    app.setApplicationName(QLatin1String("KTKLegacy"));

    AppController controller;

    QDeclarativeView view;
    view.setResizeMode(QDeclarativeView::SizeRootObjectToView);
    view.rootContext()->setContextProperty(QLatin1String("app"), &controller);
    view.setSource(QUrl(QLatin1String("qrc:/qml/main.qml")));

#if defined(Q_OS_SYMBIAN) || defined(Q_WS_SIMULATOR)
    view.showFullScreen();
#else
    view.resize(360, 640);
    view.show();
#endif

    controller.start();
    return app.exec();
}
