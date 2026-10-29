#ifndef UPLOADER_H
#define UPLOADER_H

#include <QList>
#include <QObject>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class Queue;

// Drains the queue into Dawarich's OwnTracks endpoint, one point per POST, in
// batches: each batch is cut from the queue once it's through. Stops at the first
// failure and keeps that point for the next try.
class Uploader : public QObject
{
    Q_OBJECT
public:
    explicit Uploader(Queue *queue, QObject *parent = nullptr);

    void drain(const QUrl &endpoint);
    bool isBusy() const { return m_busy; }

signals:
    void finished(bool ok, int sent, const QString &message);

private:
    void nextBatch();
    void sendNext();
    void onReply();
    void finish(bool ok, const QString &message);

    Queue *m_queue;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer m_timeout;
    QUrl m_endpoint;
    QList<QByteArray> m_batch;
    int m_index = 0;
    int m_sent = 0;
    int m_dropped = 0;
    bool m_busy = false;
    bool m_timedOut = false;
};

#endif
