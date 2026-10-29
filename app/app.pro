TARGET = harbour-sailtrack
CONFIG += sailfishapp c++11
QT += dbus

isEmpty(VERSION): VERSION = 0.1.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

HEADERS += src/controller.h ../common/paths.h
SOURCES += src/main.cpp src/controller.cpp

DISTFILES += \
    qml/harbour-sailtrack.qml \
    qml/pages/*.qml \
    qml/cover/*.qml \
    qml/js/*.js \
    harbour-sailtrack.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172
