# Desktop check of AppController without the QML (the Symbian components exist only on the
# phone): runs the job, network and session flow headless. Qt 4.7+ or Qt 5.
TEMPLATE = app
TARGET = app-check
CONFIG += console
CONFIG -= app_bundle
QT += network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
include(../../core.pri)
INCLUDEPATH += ../../src/app
HEADERS += ../../src/app/appcontroller.h probe.h
SOURCES += ../../src/app/appcontroller.cpp main.cpp
