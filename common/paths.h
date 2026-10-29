// Shared by the daemon and the app: where the config, queue and status live, the
// config keys with their defaults, and the D-Bus names.
#ifndef SAILTRACK_PATHS_H
#define SAILTRACK_PATHS_H

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

namespace Sailtrack {

const char *const AppName = "harbour-sailtrack";
const char *const Unit = "harbour-sailtrack.service";

const char *const DBusService = "dev.iante.sailtrack";
const char *const DBusPath = "/";
const char *const DBusInterface = "dev.iante.sailtrack.Daemon";

// Config keys and defaults
const char *const KeyUrl = "url";
const char *const KeyApiKey = "apiKey";
const char *const KeyInterval = "interval";        // seconds between fixes
const char *const KeyMinAccuracy = "minAccuracy";  // metres; worse fixes are dropped
const char *const KeyFixTimeout = "fixTimeout";    // seconds to wait for a fix
const char *const KeyTrackerId = "trackerId";      // OwnTracks tid
const char *const KeySkipWhenStill = "skipWhenStill";  // reuse the last fix while not moving
const int DefaultInterval = 300;
const int DefaultMinAccuracy = 100;
const int DefaultFixTimeout = 60;
const char *const DefaultTrackerId = "SF";
const bool DefaultSkipWhenStill = true;

inline QString configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1Char('/') + QLatin1String(AppName);
}

inline QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1Char('/') + QLatin1String(AppName);
}

inline QString configPath() { return configDir() + QLatin1String("/sailtrack.conf"); }
inline QString queuePath() { return dataDir() + QLatin1String("/queue.jsonl"); }
inline QString statusPath() { return dataDir() + QLatin1String("/status.json"); }

inline QSettings *openConfig(QObject *parent = nullptr)
{
    QDir().mkpath(configDir());
    return new QSettings(configPath(), QSettings::IniFormat, parent);
}

// The fix timeout never runs into the next wakeup.
inline int effectiveFixTimeout(int fixTimeout, int interval)
{
    return qBound(15, fixTimeout, qMax(15, interval - 15));
}

} // namespace Sailtrack

#endif
