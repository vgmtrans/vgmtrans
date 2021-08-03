#ifndef QMEMORYREFBUFFER_H
#define QMEMORYREFBUFFER_H

#include "qhexbuffer.h"
#include "qbuffer.h"

class QMemoryRefBuffer : public QHexBuffer
{
    Q_OBJECT

    public:
        explicit QMemoryRefBuffer(QObject *parent = nullptr);
        qint64 length() const override;
        void insert(qint64 offset, const QByteArray& data) override;
        void remove(qint64 offset, int length) override;
        QByteArray read(qint64 offset, int length) override;
        bool read(QIODevice* device) override;
        void write(QIODevice* device) override;

        qint64 indexOf(const QByteArray& ba, qint64 from) override;
        qint64 lastIndexOf(const QByteArray& ba, qint64 from) override;
    private:
        QBuffer* m_buffer;
};

#endif // QMEMORYREFBUFFER_H
