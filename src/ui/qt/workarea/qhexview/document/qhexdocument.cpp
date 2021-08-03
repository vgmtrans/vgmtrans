#include "qhexdocument.h"
#include "commands/insertcommand.h"
#include "commands/removecommand.h"
#include "commands/replacecommand.h"
#include <QApplication>
#include <QClipboard>
#include <QBuffer>
#include <QFile>

QHexDocument::QHexDocument(VGMFile* buffer, QObject* parent) : QObject(parent), m_baseaddress(0) {
  m_buffer = buffer;
  m_areaindent = DEFAULT_AREA_IDENTATION;
  m_hexlinewidth = DEFAULT_HEX_LINE_LENGTH;

  m_metadata = new QHexMetadata(this);
  m_metadata->setLineWidth(m_hexlinewidth);

  connect(m_metadata, &QHexMetadata::metadataChanged, this, &QHexDocument::lineChanged);
  connect(m_metadata, &QHexMetadata::metadataCleared, this, &QHexDocument::documentChanged);

  connect(&m_undostack, &QUndoStack::canUndoChanged, this, &QHexDocument::canUndoChanged);
  connect(&m_undostack, &QUndoStack::canRedoChanged, this, &QHexDocument::canRedoChanged);
}

bool QHexDocument::isEmpty() const {
  return m_buffer->unLength == 0;
}
bool QHexDocument::atEnd(QHexCursor* cursor) const {
  return cursor->position().offset() >= m_buffer->unLength;
}
bool QHexDocument::canUndo() const {
  return m_undostack.canUndo();
}
bool QHexDocument::canRedo() const {
  return m_undostack.canRedo();
}
qint64 QHexDocument::length() const {
  return m_buffer->unLength;
}
quint64 QHexDocument::baseAddress() const {
  return m_baseaddress;
}

int QHexDocument::areaIndent() const {
  return m_areaindent;
}
void QHexDocument::setAreaIndent(quint8 value) {
  m_areaindent = value;
}
int QHexDocument::hexLineWidth() const {
  return m_hexlinewidth;
}
void QHexDocument::setHexLineWidth(quint8 value) {
  m_hexlinewidth = value;
  m_metadata->setLineWidth(value);
}

QHexMetadata* QHexDocument::metadata() const {
  return m_metadata;
}

void QHexDocument::removeSelection(QHexCursor* cursor) {
  if (!cursor->hasSelection())
    return;

  this->remove(cursor->selectionStart().offset(), cursor->selectionLength());
  cursor->clearSelection();
}

QByteArray QHexDocument::selectedBytes(QHexCursor* cursor) const {
  if (!cursor->hasSelection())
    return QByteArray();

  return read(cursor->selectionStart().offset(), cursor->selectionLength());
}

char QHexDocument::at(int offset) const {
  return m_buffer->GetByte(offset);
}

void QHexDocument::setBaseAddress(quint64 baseaddress) {
  if (m_baseaddress == baseaddress)
    return;

  m_baseaddress = baseaddress;
  emit documentChanged();
}

void QHexDocument::sync() {
  emit documentChanged();
}

void QHexDocument::undo() {
  m_undostack.undo();
  emit documentChanged();
}

void QHexDocument::redo() {
  m_undostack.redo();
  emit documentChanged();
}

void QHexDocument::cut(QHexCursor* cursor, bool hex) {
  if (!cursor->hasSelection())
    return;

  this->copy(cursor, hex);
  this->removeSelection(cursor);
}

void QHexDocument::copy(QHexCursor* cursor, bool hex) {
  if (!cursor->hasSelection())
    return;

  QClipboard* c = qApp->clipboard();
  QByteArray bytes = this->selectedBytes(cursor);

  if (hex)
    bytes = bytes.toHex(' ').toUpper();

  c->setText(bytes);
}

void QHexDocument::paste(QHexCursor* cursor, bool hex) {
  QClipboard* c = qApp->clipboard();
  QByteArray data = c->text().toUtf8();

  if (data.isEmpty())
    return;

  this->removeSelection(cursor);

  if (hex)
    data = QByteArray::fromHex(data);

  if (cursor->insertionMode() == QHexCursor::InsertMode)
    this->insert(cursor->position().offset(), data);
  else
    this->replace(cursor->position().offset(), data);
}

void QHexDocument::insert(qint64 offset, uchar b) {
  this->insert(offset, QByteArray(1, b));
}

void QHexDocument::replace(qint64 offset, uchar b) {
  this->replace(offset, QByteArray(1, b));
}

void QHexDocument::insert(qint64 offset, const QByteArray& data) {
  // m_undostack.push(new InsertCommand(m_buffer, offset, data));
  emit documentChanged();
}

void QHexDocument::replace(qint64 offset, const QByteArray& data) {
  // m_undostack.push(new ReplaceCommand(m_buffer, offset, data));
  emit documentChanged();
}

void QHexDocument::remove(qint64 offset, int len) {
  /// m_undostack.push(new RemoveCommand(m_buffer, offset, len));
  emit documentChanged();
}

QByteArray QHexDocument::read(qint64 offset, int len) const {
  QByteArray buffer;
  if (offset >= 0 && offset < m_buffer->unLength) {
    if (offset + len > m_buffer->unLength) {
      len = m_buffer->unLength - offset;
    }
    buffer.resize(len);
    m_buffer->GetBytes(m_baseaddress + offset, len, buffer.data());
  }
  return buffer;
}

// TODO: reimplement
qint64 QHexDocument::searchForward(QHexCursor* cursor, const QByteArray& ba) {
  return 0;
  // qint64 startPos = cursor->position().offset();
  // qint64 findPos = m_buffer->indexOf(ba, startPos);
  // if (findPos > -1) {
  //   cursor->clearSelection();
  //   cursor->moveTo(findPos);
  //   cursor->select(ba.length());
  // }
  // return findPos;
}

// TODO: reimplement
qint64 QHexDocument::searchBackward(QHexCursor* cursor, const QByteArray& ba) {
  return 0;
  // qint64 startPos = cursor->position().offset() - 1;
  // if (cursor->hasSelection()) {
  //   startPos = cursor->selectionStart().offset() - 1;
  // }
  // qint64 findPos = m_buffer->lastIndexOf(ba, startPos);
  // if (findPos > -1) {
  //   cursor->clearSelection();
  //   cursor->moveTo(findPos);
  //   cursor->select(ba.length());
  // }
  // return findPos;
}
