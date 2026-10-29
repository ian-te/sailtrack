#include "queue.h"

#include <QFile>
#include <QSaveFile>
#include <QDebug>

Queue::Queue(const QString &path)
    : m_path(path)
{
    m_count = readAll().size();
}

bool Queue::append(const QByteArray &point)
{
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        qWarning() << "Can't open the queue" << m_path << file.errorString();
        return false;
    }
    if (file.write(point + '\n') < 0 || !file.flush()) {
        qWarning() << "Can't write to the queue" << file.errorString();
        return false;
    }
    ++m_count;
    return true;
}

QList<QByteArray> Queue::head(int n) const
{
    return readAll().mid(0, n);
}

bool Queue::dropHead(int n)
{
    if (n <= 0)
        return true;
    const QList<QByteArray> rest = readAll().mid(n);
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Can't rewrite the queue" << file.errorString();
        return false;
    }
    for (const QByteArray &line : rest)
        file.write(line + '\n');
    if (!file.commit()) {
        qWarning() << "Can't rewrite the queue" << file.errorString();
        return false;
    }
    m_count = rest.size();
    return true;
}

QList<QByteArray> Queue::readAll() const
{
    QList<QByteArray> lines;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return lines;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (!line.isEmpty())
            lines.append(line);
    }
    return lines;
}
