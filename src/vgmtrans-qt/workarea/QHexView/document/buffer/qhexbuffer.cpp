#include "qhexbuffer.h"
#include <QBuffer>

QHexBuffer::QHexBuffer(QObject *parent) : QObject(parent) { }

uchar QHexBuffer::at(int idx) { return this->read(idx, 1)[0]; }
bool QHexBuffer::isEmpty() const { return this->length() <= 0; }

void QHexBuffer::replace(int offset, const QByteArray &data)
{
    this->remove(offset, data.length());
    this->insert(offset, data);
}

void QHexBuffer::read(char *data, int size)
{
    QBuffer* buffer = new QBuffer(this);
    buffer->setData(data, size);

    if(!buffer->isOpen())
        buffer->open(QBuffer::ReadWrite);

    this->read(buffer);
}

void QHexBuffer::read(const QByteArray &ba)
{
    QBuffer* buffer = new QBuffer(this);

    if(!buffer->isOpen())
        buffer->open(QBuffer::ReadWrite);

    buffer->setData(ba);
    this->read(buffer);
}

