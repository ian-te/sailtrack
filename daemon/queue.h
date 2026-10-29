#ifndef QUEUE_H
#define QUEUE_H

#include <QByteArray>
#include <QList>
#include <QString>

// Points waiting for upload, one OwnTracks JSON object per line. Only the daemon
// writes it; the app counts its lines.
class Queue
{
public:
    explicit Queue(const QString &path);

    bool append(const QByteArray &point);
    QList<QByteArray> head(int n) const;
    bool dropHead(int n);
    int count() const { return m_count; }

private:
    QList<QByteArray> readAll() const;

    QString m_path;
    int m_count;
};

#endif
