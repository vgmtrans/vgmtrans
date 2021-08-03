#ifndef QFILEBUFFER_H
#define QFILEBUFFER_H

#include "qhexbuffer.h"
#include <QFile>

class QFileBuffer : public QHexBuffer
{
    Q_OBJECT

    public:
        explicit QFileBuffer(QObject *parent = nullptr);
        ~QFileBuffer();
        uchar at(qint64 idx) override;
        qint64 length() const override;
        void insert(qint64 offset, const QByteArray& data) override;
        void remove(qint64 offset, int length) override;
        QByteArray read(qint64 offset, int length) override;
        bool read(QIODevice* device) override;
        void write(QIODevice* device) override;

        qint64 indexOf(const QByteArray& ba, qint64 from) override;
        qint64 lastIndexOf(const QByteArray& ba, qint64 from) override;
    private:
        QFile* m_buffer;
        uchar *m_memory;
};

#endif // QFILEBUFFER_H
