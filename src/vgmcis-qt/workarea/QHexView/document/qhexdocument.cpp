#include "qhexdocument.h"
#include "commands/insertcommand.h"
#include "commands/removecommand.h"
#include "commands/replacecommand.h"
#include <QApplication>
#include <QClipboard>
#include <QBuffer>
#include <QFile>

QHexDocument::QHexDocument(QHexBuffer *buffer, QObject *parent): QObject(parent), m_baseaddress(0)
{
    m_buffer = buffer;
    m_buffer->setParent(this); // Take Ownership

    m_cursor = new QHexCursor(this);
    m_metadata = new QHexMetadata(this);

    connect(m_metadata, &QHexMetadata::metadataChanged, this, &QHexDocument::lineChanged);
    connect(m_metadata, &QHexMetadata::metadataCleared, this, &QHexDocument::documentChanged);

    connect(&m_undostack, &QUndoStack::canUndoChanged, this, &QHexDocument::canUndoChanged);
    connect(&m_undostack, &QUndoStack::canRedoChanged, this, &QHexDocument::canRedoChanged);
}

bool QHexDocument::isEmpty() const { return m_buffer->isEmpty(); }
bool QHexDocument::atEnd() const { return m_cursor->position().offset() >= m_buffer->length(); }
bool QHexDocument::canUndo() const { return m_undostack.canUndo(); }
bool QHexDocument::canRedo() const { return m_undostack.canRedo(); }
int QHexDocument::length() const { return m_buffer->length(); }
int QHexDocument::baseAddress() const { return m_baseaddress; }
QHexCursor *QHexDocument::cursor() const { return m_cursor; }

QHexMetadata *QHexDocument::metadata() const { return m_metadata; }
QByteArray QHexDocument::read(int offset, int len) { return m_buffer->read(offset, len); }

void QHexDocument::removeSelection()
{
    if(!m_cursor->hasSelection())
        return;

    this->remove(m_cursor->selectionStart().offset(), m_cursor->selectionLength());
    m_cursor->clearSelection();
}

QByteArray QHexDocument::selectedBytes() const
{
    if(!m_cursor->hasSelection())
        return QByteArray();

    return m_buffer->read(m_cursor->selectionStart().offset(), m_cursor->selectionLength());
}

char QHexDocument::at(int offset) const { return m_buffer->at(offset); }

void QHexDocument::setBaseAddress(int baseaddress)
{
    if(m_baseaddress == baseaddress)
        return;

    m_baseaddress = baseaddress;
    emit documentChanged();
}

void QHexDocument::sync() { emit documentChanged(); }

void QHexDocument::undo()
{
    m_undostack.undo();
    emit documentChanged();
}

void QHexDocument::redo()
{
    m_undostack.redo();
    emit documentChanged();
}

void QHexDocument::cut(bool hex)
{
    if(!m_cursor->hasSelection())
        return;

    this->copy(hex);
    this->removeSelection();
}

void QHexDocument::copy(bool hex)
{
    if(!m_cursor->hasSelection())
        return;

    QClipboard* c = qApp->clipboard();
    QByteArray bytes = this->selectedBytes();

    if(hex)
        bytes = bytes.toHex(' ').toUpper();

    c->setText(bytes);
}

void QHexDocument::paste(bool hex)
{
    QClipboard* c = qApp->clipboard();
    QByteArray data = c->text().toUtf8();

    if(data.isEmpty())
        return;

    this->removeSelection();

    if(hex)
        data = QByteArray::fromHex(data);

    if(m_cursor->insertionMode() == QHexCursor::InsertMode)
        this->insert(m_cursor->position().offset(), data);
    else
        this->replace(m_cursor->position().offset(), data);
}

void QHexDocument::insert(int offset, uchar b)
{
    this->insert(offset, QByteArray(1, b));
}

void QHexDocument::replace(int offset, uchar b)
{
    this->replace(offset, QByteArray(1, b));
}

void QHexDocument::insert(int offset, const QByteArray &data)
{
    m_undostack.push(new InsertCommand(m_buffer, offset, data));
    emit documentChanged();
}

void QHexDocument::replace(int offset, const QByteArray &data)
{
    m_undostack.push(new ReplaceCommand(m_buffer, offset, data));
    emit documentChanged();
}

void QHexDocument::remove(int offset, int len)
{
    m_undostack.push(new RemoveCommand(m_buffer, offset, len));
    emit documentChanged();
}

QByteArray QHexDocument::read(int offset, int len) const { return m_buffer->read(offset, len); }

bool QHexDocument::saveTo(QIODevice *device)
{
    if(!device->isWritable())
        return false;

    m_buffer->write(device);
    return true;
}
