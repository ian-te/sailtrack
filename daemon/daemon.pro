TEMPLATE = app
TARGET = harbour-sailtrack-daemon
QT = core dbus network positioning
CONFIG += c++11 link_pkgconfig
PKGCONFIG += keepalive

HEADERS += tracker.h queue.h uploader.h ../common/paths.h
SOURCES += main.cpp tracker.cpp queue.cpp uploader.cpp

target.path = /usr/bin

unit.files = ../systemd/harbour-sailtrack.service
unit.path = /usr/lib/systemd/user

INSTALLS += target unit
