#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <QFileSystemWatcher>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

// The app's view of the daemon: settings in the shared config file, status from
// the daemon's status.json, control through systemctl --user and D-Bus.
class Controller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString url READ url NOTIFY settingsChanged)
    Q_PROPERTY(QString apiKey READ apiKey NOTIFY settingsChanged)
    Q_PROPERTY(int interval READ interval NOTIFY settingsChanged)
    Q_PROPERTY(int minAccuracy READ minAccuracy NOTIFY settingsChanged)
    Q_PROPERTY(int fixTimeout READ fixTimeout NOTIFY settingsChanged)
    Q_PROPERTY(bool skipWhenStill READ skipWhenStill NOTIFY settingsChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY settingsChanged)
    Q_PROPERTY(bool active READ active NOTIFY unitChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY unitChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY unitChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit Controller(QObject *parent = nullptr);

    QString url() const;
    QString apiKey() const;
    int interval() const;
    int minAccuracy() const;
    int fixTimeout() const;
    bool skipWhenStill() const;
    bool configured() const;
    bool active() const { return m_active; }
    bool enabled() const { return m_enabled; }
    bool busy() const { return m_pending > 0; }
    QVariantMap status() const { return m_status; }
    QString version() const;

    Q_INVOKABLE void save(const QString &url, const QString &apiKey, int interval,
                          int minAccuracy, int fixTimeout, bool skipWhenStill);
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setEnabled(bool enabled);   // start at boot (and now), or not
    Q_INVOKABLE void sendNow();
    Q_INVOKABLE void fixNow();
    Q_INVOKABLE void refresh();

signals:
    void settingsChanged();
    void unitChanged();
    void statusChanged();
    void error(const QString &message);

private:
    void systemctl(const QStringList &args);
    void queryUnit();
    void readStatus();
    void callDaemon(const QString &method);

    QSettings *m_config;
    QFileSystemWatcher m_watcher;
    QTimer m_poll;
    QVariantMap m_status;
    bool m_active = false;
    bool m_enabled = false;
    int m_pending = 0;
};

#endif
