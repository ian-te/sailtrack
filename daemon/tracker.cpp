#include "tracker.h"
#include "uploader.h"
#include "../common/paths.h"

#include <keepalive/backgroundactivity.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrlQuery>
#include <QDebug>
#include <cmath>

using namespace Sailtrack;

namespace {

// A fix this good is the GPS's and ends the search. A rougher one (a network
// position, ±50–100 m) is only kept in case the GPS doesn't lock in time.
const int GpsAccuracy = 25;

// However still the phone seems, a real fix is taken at least this often.
const int MaxReuseSecs = 1800;

// Battery level and OwnTracks' bs (1 unplugged, 2 charging, 3 full), from sysfs.
int readBattery(int *status)
{
    QFile capacity(QStringLiteral("/sys/class/power_supply/battery/capacity"));
    QFile state(QStringLiteral("/sys/class/power_supply/battery/status"));
    int level = -1;
    if (capacity.open(QIODevice::ReadOnly))
        level = capacity.readAll().trimmed().toInt();
    *status = 0;
    if (state.open(QIODevice::ReadOnly)) {
        const QByteArray s = state.readAll().trimmed();
        if (s == "Charging")
            *status = 2;
        else if (s == "Full")
            *status = 3;
        else if (!s.isEmpty())
            *status = 1;
    }
    return level;
}

QString queueFile()
{
    QDir().mkpath(dataDir());
    return queuePath();
}

qreal accuracyOf(const QGeoPositionInfo &info)
{
    return info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy)
            ? info.attribute(QGeoPositionInfo::HorizontalAccuracy) : qreal(1e9);
}

QString isoOrNull(const QDateTime &time)
{
    return time.isValid() ? time.toUTC().toString(Qt::ISODate) : QString();
}

} // namespace

Tracker::Tracker(QObject *parent)
    : QObject(parent)
    , m_activity(new BackgroundActivity(this))
    , m_motion(new MotionCheck(this))
    , m_queue(queueFile())
{
    m_uploader = new Uploader(&m_queue, this);
    connect(m_uploader, &Uploader::finished, this, &Tracker::onUploadFinished);
    connect(m_activity, &BackgroundActivity::running, this, &Tracker::onRunning);
    m_fixTimer.setSingleShot(true);
    connect(&m_fixTimer, &QTimer::timeout, this, [this] { finishFix(); });
    connect(m_motion, &MotionCheck::finished, this, &Tracker::onMotionChecked);
}

void Tracker::start()
{
    m_startedAt = QDateTime::currentDateTimeUtc();
    loadConfig();
    qInfo() << "Started; interval" << m_interval << "s, queue" << m_queue.count();
    m_activity->run();   // first fix right away
}

void Tracker::loadConfig()
{
    QScopedPointer<QSettings> config(openConfig());
    QString url = config->value(KeyUrl).toString().trimmed();
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    m_baseUrl = QUrl(url);
    m_apiKey = config->value(KeyApiKey).toString().trimmed();
    m_interval = qMax(30, config->value(KeyInterval, DefaultInterval).toInt());
    m_minAccuracy = qMax(1, config->value(KeyMinAccuracy, DefaultMinAccuracy).toInt());
    m_fixTimeout = effectiveFixTimeout(config->value(KeyFixTimeout, DefaultFixTimeout).toInt(), m_interval);
    m_trackerId = config->value(KeyTrackerId, QLatin1String(DefaultTrackerId)).toString().left(2);
    m_skipWhenStill = config->value(KeySkipWhenStill, DefaultSkipWhenStill).toBool();
}

bool Tracker::isConfigured() const
{
    return m_baseUrl.isValid() && !m_baseUrl.host().isEmpty() && !m_apiKey.isEmpty();
}

void Tracker::Reload()
{
    loadConfig();
    qInfo() << "Config reloaded; interval" << m_interval << "s";
    if (!m_inCycle)
        scheduleNext();
    writeStatus();
}

void Tracker::SendNow()
{
    if (!m_uploader->isBusy())
        upload();
}

void Tracker::FixNow()
{
    if (m_inCycle)
        return;
    m_manual = true;
    m_activity->run();
}

void Tracker::onRunning()
{
    if (m_inCycle)
        return;
    m_inCycle = true;
    m_lastWake = QDateTime::currentDateTimeUtc();
    loadConfig();   // picks up changes even if a Reload was missed
    if (!isConfigured()) {
        m_lastAttempt = m_lastWake;
        m_lastAttemptOk = false;
        m_lastAttemptMessage = QStringLiteral("Not configured: set the Dawarich URL and API key");
        endCycle();
        return;
    }
    if (canReuseFix()) {
        setState(QStringLiteral("checking"));
        m_motion->start(m_anchorRadios);
        return;
    }
    startFix(RadioFingerprint::capture());
}

bool Tracker::canReuseFix() const
{
    return m_skipWhenStill && !m_manual && m_anchorFix.isValid() && !m_anchorRadios.isEmpty()
            && m_anchorConfirmed.secsTo(QDateTime::currentDateTimeUtc()) < qMax(MaxReuseSecs, 2 * m_interval);
}

void Tracker::onMotionChecked(bool still, const QString &reason)
{
    qInfo() << "Motion check:" << reason;
    if (still)
        reuseFix(reason);
    else
        startFix(m_motion->current());
}

// Sends the anchor fix again with the current time, so Dawarich sees the stay go on.
void Tracker::reuseFix(const QString &reason)
{
    QGeoPositionInfo point(m_anchorFix.coordinate(), QDateTime::currentDateTimeUtc());
    if (m_anchorFix.hasAttribute(QGeoPositionInfo::HorizontalAccuracy))
        point.setAttribute(QGeoPositionInfo::HorizontalAccuracy,
                           m_anchorFix.attribute(QGeoPositionInfo::HorizontalAccuracy));
    if (m_anchorFix.hasAttribute(QGeoPositionInfo::VerticalAccuracy))
        point.setAttribute(QGeoPositionInfo::VerticalAccuracy,
                           m_anchorFix.attribute(QGeoPositionInfo::VerticalAccuracy));
    point.setAttribute(QGeoPositionInfo::GroundSpeed, 0);
    m_lastAttempt = point.timestamp();
    m_lastAttemptOk = m_queue.append(makePoint(point));
    m_lastAttemptMessage = m_lastAttemptOk
            ? QStringLiteral("Not moving (%1), sent the fix from %2 again")
              .arg(reason, m_anchorFix.timestamp().toLocalTime().toString(QStringLiteral("HH:mm")))
            : QStringLiteral("Couldn't write the queue");
    qInfo() << m_lastAttemptMessage << "queue" << m_queue.count();
    upload();
}

void Tracker::startFix(const RadioFingerprint &radios)
{
    m_fixRadios = radios;
    m_best = QGeoPositionInfo();
    m_fixStart = QDateTime::currentDateTimeUtc();
    m_source = QGeoPositionInfoSource::createDefaultSource(this);
    if (!m_source) {
        finishFix(QStringLiteral("No positioning source"));
        return;
    }
    setState(QStringLiteral("fixing"));
    m_source->setPreferredPositioningMethods(QGeoPositionInfoSource::AllPositioningMethods);
    m_source->setUpdateInterval(1000);
    connect(m_source, &QGeoPositionInfoSource::positionUpdated, this, &Tracker::onPosition);
    connect(m_source, static_cast<void (QGeoPositionInfoSource::*)(QGeoPositionInfoSource::Error)>(
                &QGeoPositionInfoSource::error), this, &Tracker::onSourceError);
    m_fixTimer.start(m_fixTimeout * 1000);
    m_source->startUpdates();
}

void Tracker::onPosition(const QGeoPositionInfo &info)
{
    qDebug() << "Position" << info.timestamp() << "±" << accuracyOf(info) << "m";
    if (!info.isValid() || !info.coordinate().isValid())
        return;
    // Skip a position cached from before this wakeup.
    if (info.timestamp().isValid() && info.timestamp() < m_fixStart.addSecs(-10))
        return;
    if (!m_best.isValid() || accuracyOf(info) <= accuracyOf(m_best))
        m_best = info;
    if (accuracyOf(m_best) <= qMin(m_minAccuracy, GpsAccuracy))
        finishFix();
}

void Tracker::onSourceError(QGeoPositionInfoSource::Error error)
{
    switch (error) {
    case QGeoPositionInfoSource::AccessError:
        finishFix(QStringLiteral("Location access denied"));
        break;
    case QGeoPositionInfoSource::ClosedError:
        finishFix(QStringLiteral("Location is turned off"));
        break;
    default:
        qWarning() << "Positioning error" << error;
        break;
    }
}

void Tracker::finishFix(const QString &error)
{
    m_fixTimer.stop();
    if (m_source) {
        m_source->stopUpdates();   // releases the GPS
        m_source->disconnect(this);
        m_source->deleteLater();
        m_source = nullptr;
    }
    m_lastAttempt = QDateTime::currentDateTimeUtc();
    const int seconds = m_fixStart.secsTo(m_lastAttempt);
    if (!error.isEmpty()) {
        m_lastAttemptOk = false;
        m_lastAttemptMessage = error;
    } else if (m_best.isValid() && accuracyOf(m_best) <= m_minAccuracy) {
        m_lastAttemptOk = m_queue.append(makePoint(m_best));
        const int accuracy = qRound(accuracyOf(m_best));
        m_lastAttemptMessage = !m_lastAttemptOk ? QStringLiteral("Couldn't write the queue")
                : accuracy <= GpsAccuracy ? QStringLiteral("GPS fix in %1 s").arg(seconds)
                : QStringLiteral("No GPS lock in %1 s, sent a rough fix (±%2 m)").arg(seconds).arg(accuracy);
        if (m_lastAttemptOk) {
            m_lastFix = m_best;
            // Back at the anchor's place with a worse fix, keep the better one to reuse.
            if (!m_fixRadios.sameAs(m_anchorRadios) || !m_anchorFix.isValid()
                    || accuracyOf(m_best) <= accuracyOf(m_anchorFix) || accuracy <= GpsAccuracy) {
                m_anchorFix = m_best;
                m_anchorRadios = m_fixRadios;
            }
            m_anchorConfirmed = m_lastAttempt;
        }
    } else if (m_best.isValid()) {
        m_lastAttemptOk = false;
        m_lastAttemptMessage = QStringLiteral("Best fix in %1 s was ±%2 m, over the %3 m limit")
                .arg(seconds).arg(qRound(accuracyOf(m_best))).arg(m_minAccuracy);
    } else {
        m_lastAttemptOk = false;
        m_lastAttemptMessage = QStringLiteral("No fix in %1 s").arg(seconds);
    }
    qInfo() << m_lastAttemptMessage << "queue" << m_queue.count();
    m_manual = false;
    upload();
}

void Tracker::upload()
{
    if (m_uploader->isBusy())
        return;   // the running drain ends the cycle
    if (m_queue.count() == 0 || !isConfigured()) {
        if (m_inCycle)
            endCycle();
        else
            writeStatus();
        return;
    }
    QUrl endpoint(m_baseUrl.toString() + QLatin1String("/api/v1/owntracks/points"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("api_key"), m_apiKey);
    endpoint.setQuery(query);
    setState(QStringLiteral("uploading"));
    m_uploader->drain(endpoint);
}

void Tracker::onUploadFinished(bool ok, int sent, const QString &message)
{
    m_lastUpload = QDateTime::currentDateTimeUtc();
    m_lastUploadOk = ok;
    m_lastUploadSent = sent;
    m_lastUploadMessage = message;
    qInfo() << "Upload" << (ok ? "ok" : "failed") << "sent" << sent << message << "left" << m_queue.count();
    if (m_inCycle)
        endCycle();
    else
        setState(QStringLiteral("waiting"));
}

void Tracker::endCycle()
{
    m_inCycle = false;
    scheduleNext();
    setState(QStringLiteral("waiting"));
}

void Tracker::scheduleNext()
{
    // A standard slot lets the system wake for us together with others.
    static const int standard[] = {
        BackgroundActivity::ThirtySeconds, BackgroundActivity::TwoAndHalfMinutes,
        BackgroundActivity::FiveMinutes, BackgroundActivity::TenMinutes,
        BackgroundActivity::FifteenMinutes, BackgroundActivity::ThirtyMinutes,
        BackgroundActivity::OneHour, BackgroundActivity::TwoHours,
    };
    for (int slot : standard) {
        if (slot == m_interval) {
            m_activity->wait(BackgroundActivity::Frequency(slot));
            m_nextWake = QDateTime::fromMSecsSinceEpoch(
                        (QDateTime::currentMSecsSinceEpoch() / (slot * 1000) + 1) * slot * 1000, Qt::UTC);
            return;
        }
    }
    m_activity->wait(m_interval, m_interval + 30);
    m_nextWake = QDateTime::currentDateTimeUtc().addSecs(m_interval);
}

QByteArray Tracker::makePoint(const QGeoPositionInfo &info) const
{
    const QGeoCoordinate c = info.coordinate();
    QJsonObject point;
    point.insert(QStringLiteral("_type"), QStringLiteral("location"));
    point.insert(QStringLiteral("lat"), c.latitude());
    point.insert(QStringLiteral("lon"), c.longitude());
    const QDateTime time = info.timestamp().isValid() ? info.timestamp() : QDateTime::currentDateTimeUtc();
    point.insert(QStringLiteral("tst"), double(time.toMSecsSinceEpoch() / 1000));
    if (info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy))
        point.insert(QStringLiteral("acc"), qRound(info.attribute(QGeoPositionInfo::HorizontalAccuracy)));
    if (c.type() == QGeoCoordinate::Coordinate3D && !std::isnan(c.altitude()))
        point.insert(QStringLiteral("alt"), qRound(c.altitude()));
    if (info.hasAttribute(QGeoPositionInfo::VerticalAccuracy))
        point.insert(QStringLiteral("vac"), qRound(info.attribute(QGeoPositionInfo::VerticalAccuracy)));
    if (info.hasAttribute(QGeoPositionInfo::GroundSpeed))   // m/s; OwnTracks wants km/h
        point.insert(QStringLiteral("vel"), qRound(info.attribute(QGeoPositionInfo::GroundSpeed) * 3.6));
    if (info.hasAttribute(QGeoPositionInfo::Direction))
        point.insert(QStringLiteral("cog"), qRound(info.attribute(QGeoPositionInfo::Direction)));
    int batteryStatus = 0;
    const int battery = readBattery(&batteryStatus);
    if (battery >= 0)
        point.insert(QStringLiteral("batt"), battery);
    if (batteryStatus)
        point.insert(QStringLiteral("bs"), batteryStatus);
    point.insert(QStringLiteral("tid"), m_trackerId);
    // With a topic Dawarich treats vel as OwnTracks km/h.
    point.insert(QStringLiteral("topic"), QStringLiteral("owntracks/sailtrack/") + m_trackerId);
    point.insert(QStringLiteral("t"), m_manual ? QStringLiteral("u") : QStringLiteral("t"));
    return QJsonDocument(point).toJson(QJsonDocument::Compact);
}

void Tracker::setState(const QString &state)
{
    m_state = state;
    writeStatus();
}

void Tracker::writeStatus()
{
    QJsonObject status;
    status.insert(QStringLiteral("state"), m_state);
    status.insert(QStringLiteral("startedAt"), isoOrNull(m_startedAt));
    status.insert(QStringLiteral("configured"), isConfigured());
    status.insert(QStringLiteral("interval"), m_interval);
    status.insert(QStringLiteral("lastWake"), isoOrNull(m_lastWake));
    status.insert(QStringLiteral("nextWake"), m_inCycle ? QString() : isoOrNull(m_nextWake));
    status.insert(QStringLiteral("queue"), m_queue.count());
    if (m_lastFix.isValid()) {
        QJsonObject fix;
        fix.insert(QStringLiteral("time"), isoOrNull(m_lastFix.timestamp()));
        fix.insert(QStringLiteral("lat"), m_lastFix.coordinate().latitude());
        fix.insert(QStringLiteral("lon"), m_lastFix.coordinate().longitude());
        fix.insert(QStringLiteral("acc"), qRound(accuracyOf(m_lastFix)));
        status.insert(QStringLiteral("lastFix"), fix);
    }
    if (m_lastAttempt.isValid()) {
        QJsonObject attempt;
        attempt.insert(QStringLiteral("time"), isoOrNull(m_lastAttempt));
        attempt.insert(QStringLiteral("ok"), m_lastAttemptOk);
        attempt.insert(QStringLiteral("message"), m_lastAttemptMessage);
        status.insert(QStringLiteral("lastAttempt"), attempt);
    }
    if (m_lastUpload.isValid()) {
        QJsonObject upload;
        upload.insert(QStringLiteral("time"), isoOrNull(m_lastUpload));
        upload.insert(QStringLiteral("ok"), m_lastUploadOk);
        upload.insert(QStringLiteral("sent"), m_lastUploadSent);
        upload.insert(QStringLiteral("message"), m_lastUploadMessage);
        status.insert(QStringLiteral("lastUpload"), upload);
    }
    QSaveFile file(statusPath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(status).toJson());
        file.commit();
    }
}
