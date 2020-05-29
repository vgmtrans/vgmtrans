#ifndef QHEXBUFFER_H
#define QHEXBUFFER_H

#include <QObject>
#include <QIODevice>

class QHexBuffer : public QObject
{
    Q_OBJECT

    public:
        explicit QHexBuffer(QObject *parent = nullptr);
        bool isEmpty() const;

    public:
        virtual uchar at(int idx);
        virtual void replace(int offset, const QByteArray& data);
        virtual void read(char* data, int size);
        virtual void read(const QByteArray& ba);

    public:
        virtual int length() const = 0;
        virtual void insert(int offset, const QByteArray& data) = 0;
        virtual void remove(int offset, int length) = 0;
        virtual QByteArray read(int offset, int length) = 0;
        virtual void read(QIODevice* iodevice) = 0;
        virtual void write(QIODevice* iodevice) = 0;
};

#endif // QHEXBUFFER_H
