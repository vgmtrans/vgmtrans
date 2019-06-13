#include "qmemoryrefbuffer.h"

#include <QObject> 

QMemoryRefBuffer::QMemoryRefBuffer(QObject *parent): QHexBuffer(parent) { }
int QMemoryRefBuffer::length() const { return m_buffer->size(); }
void QMemoryRefBuffer::insert(int, const QByteArray &) { /* Insertion unsupported */ }
void QMemoryRefBuffer::remove(int offset, int length) { /* Deletion unsupported */ }

QByteArray QMemoryRefBuffer::read(int offset, int length)
{
    m_buffer->seek(offset);
    return m_buffer->read(length);
}

void QMemoryRefBuffer::read(QIODevice *device) { m_buffer = qobject_cast<QBuffer*>(device); }

void QMemoryRefBuffer::write(QIODevice *device)
{
    m_buffer->seek(0);
    device->write(m_buffer->readAll());
}
