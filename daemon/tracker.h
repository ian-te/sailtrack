#ifndef TRACKER_H
#define TRACKER_H

#include <QDateTime>
#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include "motion.h"
#include "queue.h"

class BackgroundActivity;
class Uploader;

// One cycle per wakeup: BackgroundActivity wakes the phone, the tracker takes one
// fix (or gives up after the timeout), queues it, drains the queue and goes back to
// waiting. While the phone hasn't moved since the last fix, that fix is sent again
// and the GPS stays off. Exported on D-Bus for the app.
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
    bool canReuseFix() const;
    void onMotionChecked(bool still, const QString &reason);
    void reuseFix(const QString &reason);
    void startFix(const RadioFingerprint &radios);
    void onPosition(const QGeoPositionInfo &info);
    void onSourceError(QGeoPositionInfoSource::Error error);
    void finishFix(const QString &error = QString());
    void upload();
    void onUploadFinished(bool ok, int sent, const QString &message);
    void endCycle();
    void scheduleNext();
    QByteArray makePoint(const QGeoPositionInfo &info) const;
    void logCycle(const QString &kind, int gpsSeconds, int currentMa);
    void setState(const QString &state);
    void writeStatus();

    BackgroundActivity *m_activity;
    Uploader *m_uploader;
    Queue m_queue;
    MotionCheck *m_motion;
    QGeoPositionInfoSource *m_source = nullptr;
    QTimer m_fixTimer;

    // config
    QUrl m_baseUrl;
    QString m_apiKey;
    int m_interval = 0;
    int m_minAccuracy = 0;
    int m_fixTimeout = 0;
    QString m_trackerId;
    bool m_skipWhenStill = true;

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
    // The fix a stationary wakeup sends again, and what the radios saw when it was taken.
    QGeoPositionInfo m_anchorFix;
    RadioFingerprint m_anchorRadios;
    QDateTime m_anchorConfirmed;   // last real fix at the anchor's place
    RadioFingerprint m_fixRadios;
    QDateTime m_lastAttempt;
    bool m_lastAttemptOk = false;
    QString m_lastAttemptMessage;
    QDateTime m_lastUpload;
    bool m_lastUploadOk = false;
    int m_lastUploadSent = 0;
    QString m_lastUploadMessage;
};

#endif
