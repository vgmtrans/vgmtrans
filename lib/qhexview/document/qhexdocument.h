#ifndef QHEXDOCUMENT_H
#define QHEXDOCUMENT_H

#include <QUndoStack>
#include <QFile>
#include "buffer/qhexbuffer.h"
#include "buffer/qfilebuffer.h"
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
        qint64 length() const;
        quint64 baseAddress() const;
        QHexCursor* cursor() const;
        QHexMetadata* metadata() const;
        int areaIndent() const;
        void setAreaIndent(quint8 value);
        int hexLineWidth() const;
        void setHexLineWidth(quint8 value);

    public:
        void removeSelection();
        QByteArray read(qint64 offset, int len = 0);
        QByteArray selectedBytes() const;
        char at(int offset) const;
        void setBaseAddress(quint64 baseaddress);
        void sync();

    public slots:
        void undo();
        void redo();
        void cut(bool hex = false);
        void copy(bool hex = false);
        void paste(bool hex = false);
        void insert(qint64 offset, uchar b);
        void replace(qint64 offset, uchar b);
        void insert(qint64 offset, const QByteArray& data);
        void replace(qint64 offset, const QByteArray& data);
        void remove(qint64 offset, int len);
        QByteArray read(qint64 offset, int len) const;
        bool saveTo(QIODevice* device);
        
        qint64 searchForward(const QByteArray &ba);
        qint64 searchBackward(const QByteArray &ba);

    public:
        template<typename T> static QHexDocument* fromDevice(QIODevice* iodevice, QObject* parent = nullptr);
        template<typename T> static QHexDocument* fromFile(QString filename, QObject* parent = nullptr);
        template<typename T> static QHexDocument* fromMemory(char *data, int size, QObject* parent = nullptr);
        template<typename T> static QHexDocument* fromMemory(const QByteArray& ba, QObject* parent = nullptr);
        static QHexDocument* fromLargeFile(QString filename, QObject *parent = nullptr);

    signals:
        void canUndoChanged(bool canUndo);
        void canRedoChanged(bool canRedo);
        void documentChanged();
        void lineChanged(quint64 line);

    private:
        QHexBuffer* m_buffer;
        QHexMetadata* m_metadata;
        QUndoStack m_undostack;
        QHexCursor* m_cursor;
        quint64 m_baseaddress;
        quint8 m_areaindent;
        quint8 m_hexlinewidth;
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
    if (hexbuffer->read(iodevice))
    {
        if(needsclose)
            iodevice->close();

        return new QHexDocument(hexbuffer, parent);
    } else {
        delete hexbuffer;
    }

    return nullptr;
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
