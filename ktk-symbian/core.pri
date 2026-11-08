# KTK for Symbian - the site client shared by the phone app (KTKLegacy.pro) and the desktop harness
# (tools/ktk-cli). QtCore + QtNetwork and BearSSL only; no UI.
#
# Written for Qt 4.7 and GCCE 4.4 (C++98): no C++11, no Qt 5-only API. The harness builds the
# same files with Qt 5 on a PC, which is how the protocol is checked against the live site.

QT += core network
INCLUDEPATH += $$PWD/src/core

include(bearssl.pri)

HEADERS += \
    $$PWD/src/core/ktkerror.h \
    $$PWD/src/core/json.h \
    $$PWD/src/core/seroval.h \
    $$PWD/src/core/entropy.h \
    $$PWD/src/core/trustanchors.h \
    $$PWD/src/core/tlsconnection.h \
    $$PWD/src/core/http.h \
    $$PWD/src/core/collegecalendar.h \
    $$PWD/src/core/week.h \
    $$PWD/src/core/client.h

SOURCES += \
    $$PWD/src/core/ktkerror.cpp \
    $$PWD/src/core/json.cpp \
    $$PWD/src/core/seroval.cpp \
    $$PWD/src/core/entropy.cpp \
    $$PWD/src/core/trustanchors.c \
    $$PWD/src/core/tlsconnection.cpp \
    $$PWD/src/core/http.cpp \
    $$PWD/src/core/collegecalendar.cpp \
    $$PWD/src/core/week.cpp \
    $$PWD/src/core/client.cpp
