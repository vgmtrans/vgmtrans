#include "qmemoryrefbuffer.h"

#include <QObject>

QMemoryRefBuffer::QMemoryRefBuffer(QObject *parent): QHexBuffer(parent) { }
qint64 QMemoryRefBuffer::length() const { return m_buffer->size(); }
void QMemoryRefBuffer::insert(qint64 offset, const QByteArray &data)
{
    Q_UNUSED(offset)
    Q_UNUSED(data)
    /* Insertion unsupported */
}
void QMemoryRefBuffer::remove(qint64 offset, int length)
{
    Q_UNUSED(offset)
    Q_UNUSED(length)
    /* Deletion unsupported */
}

QByteArray QMemoryRefBuffer::read(qint64 offset, int length)
{
    m_buffer->seek(offset);
    return m_buffer->read(length);
}

bool QMemoryRefBuffer::read(QIODevice *device)
{
    m_buffer = qobject_cast<QBuffer*>(device);
    if (m_buffer) {
        m_buffer->setParent(this);
        return true;
    }
    return false;
}

void QMemoryRefBuffer::write(QIODevice *device)
{
    m_buffer->seek(0);
    if (m_buffer->size() < INT_MAX) {
        device->write(m_buffer->readAll());
    } else {
        while (m_buffer->pos() < m_buffer->size()) {
            char tmpBuf[4096];
            qint64 chunkLen = m_buffer->read(tmpBuf, 4096);
            if (chunkLen == -1)
                break;
            if (chunkLen > 0) {
                device->write(tmpBuf, chunkLen);
                m_buffer->seek(m_buffer->pos() + chunkLen);
            }
        }
    }
}

qint64 QMemoryRefBuffer::indexOf(const QByteArray& ba, qint64 from)
{
    qint64 findPos = -1;
    if (from < m_buffer->size()) {
        findPos = from;
        m_buffer->seek(from);

        while (findPos < m_buffer->size()) {
            QByteArray data = m_buffer->read(INT_MAX);
            int idx = data.indexOf(ba);
            if (idx >= 0) {
                findPos += idx;
                break;
            }
            if (findPos + data.size() >= m_buffer->size())
                return -1;
            m_buffer->seek(m_buffer->pos() + data.size() - ba.size());
        }
    }
    return findPos;
}

qint64 QMemoryRefBuffer::lastIndexOf(const QByteArray& ba, qint64 from)
{
    qint64 findPos = -1;
    if (from >= 0 && ba.size() < INT_MAX) {
        qint64 currPos = from;
        while (currPos >= 0) {
            qint64 readPos = (currPos < INT_MAX) ? 0 : currPos - INT_MAX;
            m_buffer->seek(readPos);
            QByteArray data = m_buffer->read(currPos - readPos);
            int idx = data.lastIndexOf(ba, from);
            if (idx >= 0) {
                findPos = readPos + idx;
                break;
            }
            if (readPos <= 0)
                break;
            currPos = readPos + ba.size();
        }

    }
    return findPos;
}
