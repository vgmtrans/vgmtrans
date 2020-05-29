#ifndef QMEMORYREFBUFFER_H
#define QMEMORYREFBUFFER_H

#include "qhexbuffer.h"
#include "qbuffer.h"

class QMemoryRefBuffer : public QHexBuffer
{
    Q_OBJECT

    public:
        explicit QMemoryRefBuffer(QObject *parent = nullptr);
        int length() const override;
        void insert(int, const QByteArray&) override;
        void remove(int offset, int length) override;
        QByteArray read(int offset, int length) override;
        void read(QIODevice* device) override;
        void write(QIODevice* device) override;

    private:
        QBuffer* m_buffer;
};

#endif // QMEMORYREFBUFFER_H
