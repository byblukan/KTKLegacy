# KTKLegacy for Symbian - the college timetable (ktk.education-system.ru) for Symbian^3 / Anna / Belle.
#
# Phone: build-symbian.cmd with the Qt SDK 1.2.1 "Qt 4.7.4 for Symbian Anna/Belle" target.
# Desktop: the core alone builds in tools/ktk-cli (Qt 4.7+ or Qt 5); the QML needs the Symbian
# components, so the full app runs on the phone (or the Qt Simulator of the SDK).

TEMPLATE = app
TARGET = KTKLegacy
VERSION = 1.0.0

QT += core gui network declarative

include(core.pri)

INCLUDEPATH += src/app

HEADERS += \
    src/app/appcontroller.h

SOURCES += \
    src/main.cpp \
    src/app/appcontroller.cpp

RESOURCES += qml.qrc

# Reaches AppController::version() as an unquoted macro (stringified in code), which survives
# every generator's quoting rules.
DEFINES += APP_VERSION=$$VERSION

OTHER_FILES += qml/*.qml README.md

symbian {
    # Unprotected range: installs self-signed, no Symbian Signed needed.
    TARGET.UID3 = 0xE8A5C3B1
    # User-grantable only, so the self-signed package installs on any phone.
    TARGET.CAPABILITY += NetworkServices
    # The site's week as a JSON tree, plus 33 KB of TLS buffers per connection: a roomy heap.
    TARGET.EPOCHEAPSIZE = 0x020000 0x4000000
    TARGET.EPOCSTACKSIZE = 0x14000
    ICON = icon.svg
    # The name all KTK clients share (iOS, Android, Symbian).
    DEPLOYMENT.display_name = KTKLegacy

    # Qt Quick Components for Symbian: built into Belle, and installed on Anna by the Smart
    # Installer package (KTKLegacy_installer.sis).
    CONFIG += qt-components
    DEPLOYMENT.installer_header = 0x2002CCCF

    vendorinfo = \
        "%{\"KTKLegacy\"}" \
        ":\"KTKLegacy\""
    my_deployment.pkg_prerules = vendorinfo
    DEPLOYMENT += my_deployment
}
