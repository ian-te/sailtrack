#include "uploader.h"
#include "queue.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDebug>

namespace {
const int BatchSize = 25;
const int MaxPerDrain = 1000;   // a long backlog goes over several wakeups
const int RequestTimeoutMs = 30000;
}

Uploader::Uploader(Queue *queue, QObject *parent)
    : QObject(parent)
    , m_queue(queue)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(RequestTimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_reply) {
            m_timedOut = true;
            m_reply->abort();
        }
    });
}

void Uploader::drain(const QUrl &endpoint)
{
    if (m_busy)
        return;
    m_busy = true;
    m_endpoint = endpoint;
    m_sent = 0;
    m_dropped = 0;
    // A fresh manager per drain: after a suspend or a network change Qt 5.6's one
    // can stay convinced the network is gone.
    if (m_nam)
        m_nam->deleteLater();
    m_nam = new QNetworkAccessManager(this);
    m_nam->setNetworkAccessible(QNetworkAccessManager::Accessible);
    nextBatch();
}

void Uploader::nextBatch()
{
    if (m_queue->count() == 0 || m_sent + m_dropped >= MaxPerDrain) {
        finish(true, m_dropped ? QStringLiteral("%1 rejected points dropped").arg(m_dropped) : QString());
        return;
    }
    m_batch = m_queue->head(BatchSize);
    m_index = 0;
    sendNext();
}

void Uploader::sendNext()
{
    if (m_index >= m_batch.size()) {
        m_queue->dropHead(m_index);
        nextBatch();
        return;
    }
    QNetworkRequest request(m_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("harbour-sailtrack"));
    m_timedOut = false;
    m_reply = m_nam->post(request, m_batch.at(m_index));
    connect(m_reply, &QNetworkReply::finished, this, &Uploader::onReply);
    m_timeout.start();
}

void Uploader::onReply()
{
    m_timeout.stop();
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
        ++m_sent;
        ++m_index;
        sendNext();
        return;
    }
    if (status == 400 || status == 422) {
        // The point itself is bad; retrying it would block the queue forever.
        qWarning() << "Dawarich rejected a point, dropping it:" << status << m_batch.at(m_index);
        ++m_dropped;
        ++m_index;
        sendNext();
        return;
    }

    QString message;
    if (m_timedOut)
        message = QStringLiteral("Timed out");
    else if (status)
        message = QStringLiteral("HTTP %1 %2").arg(status)
                .arg(reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString());
    else
        message = reply->errorString();
    m_queue->dropHead(m_index);
    finish(false, message.trimmed());
}

void Uploader::finish(bool ok, const QString &message)
{
    m_busy = false;
    m_batch.clear();
    emit finished(ok, m_sent, message);
}
