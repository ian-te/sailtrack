#ifndef TRACKER_H
#define TRACKER_H

#include <QDateTime>
#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include "queue.h"

class BackgroundActivity;
class Uploader;

// One cycle per wakeup: BackgroundActivity wakes the phone, the tracker takes one
// fix (or gives up after the timeout), queues it, drains the queue and goes back to
// waiting. Exported on D-Bus for the app.
class Tracker : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "dev.iante.sailtrack.Daemon")
public:
    explicit Tracker(QObject *parent = nullptr);

    void start();

public slots:
    Q_SCRIPTABLE void Reload();     // config changed
    Q_SCRIPTABLE void SendNow();    // drain the queue now
    Q_SCRIPTABLE void FixNow();     // run a cycle now

private:
    void loadConfig();
    bool isConfigured() const;
    void onRunning();
    void startFix();
    void onPosition(const QGeoPositionInfo &info);
    void onSourceError(QGeoPositionInfoSource::Error error);
    void finishFix(const QString &error = QString());
    void upload();
    void onUploadFinished(bool ok, int sent, const QString &message);
    void endCycle();
    void scheduleNext();
    QByteArray makePoint(const QGeoPositionInfo &info) const;
    void setState(const QString &state);
    void writeStatus();

    BackgroundActivity *m_activity;
    Uploader *m_uploader;
    Queue m_queue;
    QGeoPositionInfoSource *m_source = nullptr;
    QTimer m_fixTimer;

    // config
    QUrl m_baseUrl;
    QString m_apiKey;
    int m_interval = 0;
    int m_minAccuracy = 0;
    int m_fixTimeout = 0;
    QString m_trackerId;

    // state
    QString m_state;
    bool m_inCycle = false;
    bool m_manual = false;
    QDateTime m_startedAt;
    QDateTime m_fixStart;
    QDateTime m_lastWake;
    QDateTime m_nextWake;
    QGeoPositionInfo m_best;
    QGeoPositionInfo m_lastFix;
    QDateTime m_lastAttempt;
    bool m_lastAttemptOk = false;
    QString m_lastAttemptMessage;
    QDateTime m_lastUpload;
    bool m_lastUploadOk = false;
    int m_lastUploadSent = 0;
    QString m_lastUploadMessage;
};

#endif
