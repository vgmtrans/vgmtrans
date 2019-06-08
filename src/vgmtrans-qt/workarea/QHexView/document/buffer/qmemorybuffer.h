#ifndef QMEMORYBUFFER_H
#define QMEMORYBUFFER_H

#include "qhexbuffer.h"

class QMemoryBuffer : public QHexBuffer
{
    Q_OBJECT

    public:
        explicit QMemoryBuffer(QObject *parent = nullptr);
        uchar at(int idx) override;
        int length() const override;
        void insert(int offset, const QByteArray& data) override;
        void remove(int offset, int length) override;
        QByteArray read(int offset, int length) override;
        void read(QIODevice* device) override;
        void write(QIODevice* device) override;

    private:
        QByteArray m_buffer;
};

#endif // QMEMORYBUFFER_H
