#ifndef QHEXDOCUMENT_H
#define QHEXDOCUMENT_H

#include <QUndoStack>
#include <QFile>
#include "buffer/qhexbuffer.h"
#include "qhexmetadata.h"
#include "qhexcursor.h"

class QHexDocument: public QObject
{
    Q_OBJECT

    private:
        explicit QHexDocument(QHexBuffer* buffer, QObject *parent = nullptr);

    public:
        bool isEmpty() const;
        bool atEnd() const;
        bool canUndo() const;
        bool canRedo() const;
        int length() const;
        int baseAddress() const;
        QHexCursor* cursor() const;
        QHexMetadata* metadata() const;

    public:
        void removeSelection();
        QByteArray read(int offset, int len = 0);
        QByteArray selectedBytes() const;
        char at(int offset) const;
        void setBaseAddress(int baseaddress);
        void sync();

    public slots:
        void undo();
        void redo();
        void cut(bool hex = false);
        void copy(bool hex = false);
        void paste(bool hex = false);
        void insert(int offset, uchar b);
        void replace(int offset, uchar b);
        void insert(int offset, const QByteArray& data);
        void replace(int offset, const QByteArray& data);
        void remove(int offset, int len);
        QByteArray read(int offset, int len) const;
        bool saveTo(QIODevice* device);

    public:
        template<typename T> static QHexDocument* fromDevice(QIODevice* iodevice, QObject* parent = nullptr);
        template<typename T> static QHexDocument* fromFile(QString filename, QObject* parent = nullptr);
        template<typename T> static QHexDocument* fromMemory(char *data, int size, QObject* parent = nullptr);
        template<typename T> static QHexDocument* fromMemory(const QByteArray& ba, QObject* parent = nullptr);

    signals:
        void canUndoChanged();
        void canRedoChanged();
        void documentChanged();
        void lineChanged(int line);

    private:
        QHexBuffer* m_buffer;
        QHexMetadata* m_metadata;
        QUndoStack m_undostack;
        QHexCursor* m_cursor;
        int m_baseaddress;
};

template<typename T> QHexDocument* QHexDocument::fromDevice(QIODevice* iodevice, QObject *parent)
{
    bool needsclose = false;

    if(!iodevice->isOpen())
    {
        needsclose = true;
        iodevice->open(QIODevice::ReadWrite);
    }

    QHexBuffer* hexbuffer = new T();
    hexbuffer->read(iodevice);

    if(needsclose)
        iodevice->close();

    return new QHexDocument(hexbuffer, parent);
}

template<typename T> QHexDocument* QHexDocument::fromFile(QString filename, QObject *parent)
{
    QFile f(filename);
    f.open(QFile::ReadOnly);

    QHexDocument* doc = QHexDocument::fromDevice<T>(&f, parent);
    f.close();
    return doc;
}

template<typename T> QHexDocument* QHexDocument::fromMemory(char *data, int size, QObject *parent)
{
    QHexBuffer* hexbuffer = new T();
    hexbuffer->read(data, size);
    return new QHexDocument(hexbuffer, parent);
}

template<typename T> QHexDocument* QHexDocument::fromMemory(const QByteArray& ba, QObject *parent)
{
    QHexBuffer* hexbuffer = new T();
    hexbuffer->read(ba);
    return new QHexDocument(hexbuffer, parent);
}

#endif // QHEXEDITDATA_H
