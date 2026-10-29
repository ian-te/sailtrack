#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>

#include "tracker.h"
#include "../common/paths.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("harbour-sailtrack-daemon"));

    Tracker tracker;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QLatin1String(Sailtrack::DBusService))) {
        qCritical() << "Can't take" << Sailtrack::DBusService << "on the session bus; already running?"
                    << bus.lastError().message();
        return 1;
    }
    bus.registerObject(QLatin1String(Sailtrack::DBusPath), &tracker, QDBusConnection::ExportScriptableSlots);

    tracker.start();
    return app.exec();
}
