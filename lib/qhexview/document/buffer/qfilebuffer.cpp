#include "qfilebuffer.h"

QFileBuffer::QFileBuffer(QObject *parent) : QHexBuffer(parent) { }

QFileBuffer::~QFileBuffer()
{
    if (m_buffer)
    {
        if (m_buffer->isOpen())
            m_buffer->close();

        delete m_buffer;
        m_buffer = nullptr;
    }
}

uchar QFileBuffer::at(qint64 idx) {
    m_buffer->seek(idx);
    char c = '\0';
    m_buffer->getChar(&c);
    return static_cast<uchar>(c);
}

qint64 QFileBuffer::length() const {
    return m_buffer->size();
}

void QFileBuffer::insert(qint64 offset, const QByteArray &data) {
    Q_UNUSED(offset)
    Q_UNUSED(data)
    // Not implemented
}

void QFileBuffer::remove(qint64 offset, int length) {
    Q_UNUSED(offset)
    Q_UNUSED(length)
    // Not implemented
}

QByteArray QFileBuffer::read(qint64 offset, int length) {
    m_buffer->seek(offset);
    return m_buffer->read(length);
}

bool QFileBuffer::read(QIODevice *device) {
    m_buffer = qobject_cast<QFile*>(device);
    if (m_buffer) {
        m_buffer->setParent(this);
        if (!m_buffer->isOpen())
        {
            m_buffer->open(QIODevice::ReadWrite);
        }
        return true;
    }
    return false;
}

void QFileBuffer::write(QIODevice *device) {
    Q_UNUSED(device)
    // Not implemented
}

qint64 QFileBuffer::indexOf(const QByteArray& ba, qint64 from)
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

qint64 QFileBuffer::lastIndexOf(const QByteArray& ba, qint64 from)
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
