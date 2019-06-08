#include "qmemorybuffer.h"

QMemoryBuffer::QMemoryBuffer(QObject *parent) : QHexBuffer(parent) { }
uchar QMemoryBuffer::at(int idx) { return static_cast<uchar>(m_buffer.at(idx)); }
int QMemoryBuffer::length() const { return m_buffer.length(); }
void QMemoryBuffer::insert(int offset, const QByteArray &data) { m_buffer.insert(offset, data); }
void QMemoryBuffer::remove(int offset, int length) { m_buffer.remove(offset, length); }
QByteArray QMemoryBuffer::read(int offset, int length) { return m_buffer.mid(offset, length); }
void QMemoryBuffer::read(QIODevice *device) { m_buffer = device->readAll(); }
void QMemoryBuffer::write(QIODevice *device) { device->write(m_buffer); }
