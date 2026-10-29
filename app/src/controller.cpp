#include "controller.h"
#include "../../common/paths.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

using namespace Sailtrack;

Controller::Controller(QObject *parent)
    : QObject(parent)
    , m_config(openConfig(this))
{
    // The daemon replaces status.json by renaming, so watch the folder.
    QDir().mkpath(dataDir());
    m_watcher.addPath(dataDir());
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Controller::readStatus);
    refresh();
}

QString Controller::url() const { return m_config->value(KeyUrl).toString(); }
QString Controller::apiKey() const { return m_config->value(KeyApiKey).toString(); }
int Controller::interval() const { return m_config->value(KeyInterval, DefaultInterval).toInt(); }
int Controller::minAccuracy() const { return m_config->value(KeyMinAccuracy, DefaultMinAccuracy).toInt(); }
int Controller::fixTimeout() const { return m_config->value(KeyFixTimeout, DefaultFixTimeout).toInt(); }
bool Controller::skipWhenStill() const { return m_config->value(KeySkipWhenStill, DefaultSkipWhenStill).toBool(); }

bool Controller::configured() const
{
    return !url().trimmed().isEmpty() && !apiKey().trimmed().isEmpty();
}

QString Controller::version() const
{
    return QStringLiteral(APP_VERSION);
}

void Controller::save(const QString &url, const QString &apiKey, int interval,
                      int minAccuracy, int fixTimeout, bool skipWhenStill)
{
    m_config->setValue(KeyUrl, url.trimmed());
    m_config->setValue(KeyApiKey, apiKey.trimmed());
    m_config->setValue(KeyInterval, interval);
    m_config->setValue(KeyMinAccuracy, minAccuracy);
    m_config->setValue(KeyFixTimeout, fixTimeout);
    m_config->setValue(KeySkipWhenStill, skipWhenStill);
    m_config->sync();
    // It holds the API key.
    QFile::setPermissions(configPath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    emit settingsChanged();
    if (m_active)
        callDaemon(QStringLiteral("Reload"));
}

void Controller::start()
{
    systemctl({QStringLiteral("start"), QLatin1String(Unit)});
}

void Controller::stop()
{
    systemctl({QStringLiteral("stop"), QLatin1String(Unit)});
}

void Controller::setEnabled(bool enabled)
{
    systemctl({enabled ? QStringLiteral("enable") : QStringLiteral("disable"),
               QStringLiteral("--now"), QLatin1String(Unit)});
}

void Controller::sendNow()
{
    callDaemon(QStringLiteral("SendNow"));
}

void Controller::fixNow()
{
    callDaemon(QStringLiteral("FixNow"));
}

void Controller::refresh()
{
    readStatus();
    queryUnit();
}

void Controller::systemctl(const QStringList &args)
{
    ++m_pending;
    emit unitChanged();
    QProcess *process = new QProcess(this);
    connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this, process](int code, QProcess::ExitStatus) {
        if (code != 0)
            emit error(QString::fromLocal8Bit(process->readAllStandardError()).trimmed());
        process->deleteLater();
        --m_pending;
        queryUnit();
    });
    process->start(QStringLiteral("systemctl"), QStringList(QStringLiteral("--user")) + args);
}

void Controller::queryUnit()
{
    QProcess *process = new QProcess(this);
    connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this, process](int, QProcess::ExitStatus) {
        bool active = false;
        bool enabled = false;
        for (const QByteArray &line : process->readAllStandardOutput().split('\n')) {
            if (line == "ActiveState=active" || line == "ActiveState=activating")
                active = true;
            else if (line == "UnitFileState=enabled")
                enabled = true;
        }
        process->deleteLater();
        m_active = active;
        m_enabled = enabled;
        emit unitChanged();
    });
    process->start(QStringLiteral("systemctl"),
                   {QStringLiteral("--user"), QStringLiteral("show"), QStringLiteral("-p"),
                    QStringLiteral("ActiveState"), QStringLiteral("-p"), QStringLiteral("UnitFileState"),
                    QLatin1String(Unit)});
}

void Controller::readStatus()
{
    QFile file(statusPath());
    QVariantMap status;
    if (file.open(QIODevice::ReadOnly))
        status = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    if (status != m_status) {
        m_status = status;
        emit statusChanged();
    }
}

void Controller::callDaemon(const QString &method)
{
    if (!m_active) {
        emit error(tr("The daemon isn't running"));
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(
                QLatin1String(DBusService), QLatin1String(DBusPath), QLatin1String(DBusInterface), method);
    QDBusPendingCallWatcher *watcher =
            new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<> reply = *w;
        if (reply.isError())
            emit error(reply.error().message());
        w->deleteLater();
    });
}
