#include "motion.h"

#include <QAccelerometer>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDebug>
#include <cmath>

namespace {

// How long the accelerometer is watched, and the spread of |a| (m/s²) above which
// the phone counts as moving. Lying on a table it's ~0.05; walking or a car is well over.
const int AccelSampleMs = 3000;
const qreal AccelMovingStdDev = 0.25;

// Share of cells two fingerprints must have in common to be the same place.
const qreal CellOverlap = 0.5;

QDBusMessage systemCall(const QString &service, const QString &path, const QString &interface,
                        const QString &method)
{
    return QDBusConnection::systemBus().call(
                QDBusMessage::createMethodCall(service, path, interface, method), QDBus::Block, 2000);
}

// Reads an a{sv} out of a reply argument.
QVariantMap toMap(const QVariant &value)
{
    if (value.canConvert<QDBusArgument>()) {
        QVariantMap map;
        value.value<QDBusArgument>() >> map;
        return map;
    }
    return value.toMap();
}

QVariant unwrap(const QVariant &value)
{
    return value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant() : value;
}

QString connectedBssid()
{
    const QDBusMessage reply = systemCall(QStringLiteral("net.connman"), QStringLiteral("/"),
                                          QStringLiteral("net.connman.Manager"), QStringLiteral("GetServices"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return QString();
    const QDBusArgument services = reply.arguments().first().value<QDBusArgument>();
    QString bssid;
    services.beginArray();
    while (!services.atEnd()) {
        QDBusObjectPath path;
        QVariantMap properties;
        services.beginStructure();
        services >> path >> properties;
        services.endStructure();
        const QString state = unwrap(properties.value(QStringLiteral("State"))).toString();
        if (bssid.isEmpty() && unwrap(properties.value(QStringLiteral("Type"))).toString() == QLatin1String("wifi")
                && (state == QLatin1String("ready") || state == QLatin1String("online")))
            bssid = unwrap(properties.value(QStringLiteral("BSSID"))).toString().toLower();
    }
    services.endArray();
    return bssid;
}

QStringList modems()
{
    QStringList paths;
    const QDBusMessage reply = systemCall(QStringLiteral("org.ofono"), QStringLiteral("/"),
                                          QStringLiteral("org.ofono.Manager"), QStringLiteral("GetModems"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return paths;
    const QDBusArgument list = reply.arguments().first().value<QDBusArgument>();
    list.beginArray();
    while (!list.atEnd()) {
        QDBusObjectPath path;
        QVariantMap properties;
        list.beginStructure();
        list >> path >> properties;
        list.endStructure();
        paths << path.path();
    }
    list.endArray();
    return paths;
}

// A cell's identity: its global id when the modem reports one (the serving cell),
// otherwise its physical id on its channel (neighbours).
QString cellKey(const QString &type, const QVariantMap &p)
{
    const int ci = p.value(QStringLiteral("ci"), p.value(QStringLiteral("nci"), -1)).toInt();
    if (ci > 0 && ci != 0x7fffffff)
        return type + QLatin1Char(':') + QString::number(ci);
    const int pci = p.value(QStringLiteral("pci"), p.value(QStringLiteral("psc"), p.value(QStringLiteral("bsic"), -1))).toInt();
    const int channel = p.value(QStringLiteral("earfcn"), p.value(QStringLiteral("nrarfcn"),
                         p.value(QStringLiteral("uarfcn"), p.value(QStringLiteral("arfcn"), -1)))).toInt();
    if (pci < 0 || pci == 0x7fffffff)
        return QString();
    return type + QLatin1Char(':') + QString::number(pci) + QLatin1Char('/') + QString::number(channel);
}

void readCells(const QString &modem, RadioFingerprint *fp)
{
    QDBusMessage reply = systemCall(QStringLiteral("org.ofono"), modem,
                                    QStringLiteral("org.ofono.NetworkRegistration"), QStringLiteral("GetProperties"));
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        const QVariantMap reg = toMap(reply.arguments().first());
        const QString status = unwrap(reg.value(QStringLiteral("Status"))).toString();
        const uint ci = unwrap(reg.value(QStringLiteral("CellId"))).toUInt();
        if ((status == QLatin1String("registered") || status == QLatin1String("roaming")) && ci > 0
                && fp->servingCell.isEmpty())
            fp->servingCell = unwrap(reg.value(QStringLiteral("Technology"))).toString()
                    + QLatin1Char(':') + QString::number(ci);
    }

    reply = systemCall(QStringLiteral("org.ofono"), modem,
                       QStringLiteral("org.nemomobile.ofono.CellInfo"), QStringLiteral("GetCells"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return;
    const QList<QDBusObjectPath> cells = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().first());
    for (const QDBusObjectPath &cell : cells) {
        // GetAll returns (version, type, registered, properties).
        const QDBusMessage all = systemCall(QStringLiteral("org.ofono"), cell.path(),
                                            QStringLiteral("org.nemomobile.ofono.Cell"), QStringLiteral("GetAll"));
        if (all.type() != QDBusMessage::ReplyMessage || all.arguments().size() < 4)
            continue;
        const QString key = cellKey(all.arguments().at(1).toString(), toMap(all.arguments().at(3)));
        if (!key.isEmpty())
            fp->cells.insert(key);
    }
}

} // namespace

RadioFingerprint RadioFingerprint::capture()
{
    RadioFingerprint fp;
    fp.bssid = connectedBssid();
    for (const QString &modem : modems())
        readCells(modem, &fp);
    if (!fp.servingCell.isEmpty())
        fp.cells.insert(fp.servingCell);
    return fp;
}

bool RadioFingerprint::sameAs(const RadioFingerprint &other) const
{
    // A Wi-Fi access point reaches some tens of metres, so it decides when there is one.
    if (!bssid.isEmpty() || !other.bssid.isEmpty())
        return bssid == other.bssid;
    if (servingCell.isEmpty() || servingCell != other.servingCell)
        return false;
    const int common = (cells & other.cells).size();
    const int all = (cells | other.cells).size();
    return all > 0 && qreal(common) / all >= CellOverlap;
}

QString RadioFingerprint::describe() const
{
    return bssid.isEmpty() ? QStringLiteral("cell %1, %2 cells").arg(servingCell).arg(cells.size())
                           : QStringLiteral("Wi-Fi %1").arg(bssid);
}

MotionCheck::MotionCheck(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &MotionCheck::finish);
}

void MotionCheck::start(const RadioFingerprint &anchor)
{
    m_current = RadioFingerprint::capture();
    qDebug() << "Radios now" << m_current.describe() << "at the last fix" << anchor.describe();
    if (m_current.isEmpty() || !m_current.sameAs(anchor)) {
        // Report asynchronously, like the accelerometer path.
        QTimer::singleShot(0, this, [this] {
            emit finished(false, m_current.isEmpty() ? QStringLiteral("no radio to compare")
                                                     : QStringLiteral("radios changed"));
        });
        return;
    }
    m_magnitudes.clear();
    if (!m_accel) {
        m_accel = new QAccelerometer(this);
        m_accel->setAlwaysOn(true);   // keep it running with the display off
        connect(m_accel, &QAccelerometer::readingChanged, this, &MotionCheck::onReading);
    }
    m_accel->start();
    m_timer.start(AccelSampleMs);
}

void MotionCheck::onReading()
{
    const QAccelerometerReading *r = m_accel->reading();
    m_magnitudes.append(std::sqrt(r->x() * r->x() + r->y() * r->y() + r->z() * r->z()));
}

void MotionCheck::finish()
{
    m_accel->stop();
    // No accelerometer readings leaves the radios to decide.
    if (m_magnitudes.size() < 5) {
        qWarning() << "Only" << m_magnitudes.size() << "accelerometer readings";
        emit finished(true, QStringLiteral("same radios"));
        return;
    }
    qreal mean = 0;
    for (qreal m : m_magnitudes)
        mean += m;
    mean /= m_magnitudes.size();
    qreal variance = 0;
    for (qreal m : m_magnitudes)
        variance += (m - mean) * (m - mean);
    const qreal spread = std::sqrt(variance / m_magnitudes.size());
    qDebug() << m_magnitudes.size() << "accelerometer readings, spread" << spread;
    if (spread > AccelMovingStdDev)
        emit finished(false, QStringLiteral("moving"));
    else
        emit finished(true, QStringLiteral("same radios, not moving"));
}
