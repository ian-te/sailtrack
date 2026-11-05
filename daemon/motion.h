#ifndef MOTION_H
#define MOTION_H

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

class QAccelerometer;

// What the radios see: the connected Wi-Fi access point and the cells around. Read
// from connman and ofono, which keep it current through suspend, so it costs no GPS.
struct RadioFingerprint
{
    QString bssid;         // connected Wi-Fi access point, if any
    QString servingCell;
    QSet<QString> cells;   // serving and neighbour cells

    bool isEmpty() const { return bssid.isEmpty() && servingCell.isEmpty(); }
    bool sameAs(const RadioFingerprint &other) const;
    QString describe() const;

    static RadioFingerprint capture();
};

// Tells whether the phone has stayed put since a fingerprint was taken: the radios
// still see the same place and the accelerometer is quiet for a few seconds.
class MotionCheck : public QObject
{
    Q_OBJECT
public:
    explicit MotionCheck(QObject *parent = nullptr);

    void start(const RadioFingerprint &anchor);
    RadioFingerprint current() const { return m_current; }

signals:
    void finished(bool still, const QString &reason);

private:
    void onReading();
    void finish();

    QAccelerometer *m_accel = nullptr;
    QTimer m_timer;
    QVector<qreal> m_magnitudes;
    RadioFingerprint m_current;
};

#endif
