#ifndef QHEXDOCUMENT_H
#define QHEXDOCUMENT_H

#include <QUndoStack>
#include <QFile>
#include "buffer/qhexbuffer.h"
#include "buffer/qfilebuffer.h"
#include "qhexmetadata.h"
#include "qhexcursor.h"
#include "VGMFile.h"

class QHexDocument: public QObject
{
    Q_OBJECT

    public:
        explicit QHexDocument(VGMFile* buffer, QObject *parent = nullptr);

    public:
        bool isEmpty() const;
        bool atEnd(QHexCursor* cursor) const;
        bool canUndo() const;
        bool canRedo() const;
        qint64 length() const;
        quint64 baseAddress() const;
        QHexMetadata* metadata() const;
        int areaIndent() const;
        void setAreaIndent(quint8 value);
        int hexLineWidth() const;
        void setHexLineWidth(quint8 value);

    public:
        void removeSelection(QHexCursor* cursor);
        QByteArray selectedBytes(QHexCursor* cursor) const;
        char at(int offset) const;
        void setBaseAddress(quint64 baseaddress);
        void sync();

    public slots:
        void undo();
        void redo();
        void cut(QHexCursor* cursor, bool hex = false);
        void copy(QHexCursor* cursor, bool hex = false);
        void paste(QHexCursor* cursor, bool hex = false);
        void insert(qint64 offset, uchar b);
        void replace(qint64 offset, uchar b);
        void insert(qint64 offset, const QByteArray& data);
        void replace(qint64 offset, const QByteArray& data);
        void remove(qint64 offset, int len);
      
        qint64 searchForward(QHexCursor* cursor, const QByteArray &ba);
        qint64 searchBackward(QHexCursor* cursor, const QByteArray &ba);
        
        QByteArray read(qint64 offset, int len) const;

    signals:
        void canUndoChanged(bool canUndo);
        void canRedoChanged(bool canRedo);
        void documentChanged();
        void lineChanged(quint64 line);

    private:
        VGMFile* m_buffer;
        QHexMetadata* m_metadata;
        QUndoStack m_undostack;
        quint64 m_baseaddress;
        quint8 m_areaindent;
        // TODO: move this to the qhexview
        quint8 m_hexlinewidth;
};

#endif // QHEXEDITDATA_H
