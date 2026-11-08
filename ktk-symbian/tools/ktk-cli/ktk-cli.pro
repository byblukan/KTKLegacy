# Desktop harness for the KTK core: builds with Qt 4.7+ or Qt 5 on a PC.
#   qmake tools/ktk-cli/ktk-cli.pro && make
TEMPLATE = app
TARGET = ktk-cli
CONFIG += console
CONFIG -= app_bundle
QT -= gui
include(../../core.pri)
SOURCES += main.cpp
# Catch C++11 creeping into code that GCCE 4.4 (C++98) must compile.
*-clang*: QMAKE_CXXFLAGS += -Wc++98-compat -Wno-c++98-compat-pedantic
