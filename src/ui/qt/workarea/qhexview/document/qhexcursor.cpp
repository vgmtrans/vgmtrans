#include "qhexcursor.h"
#include <QWidget>

QHexCursor::QHexCursor(QObject *parent)
    : QObject(parent), m_insertionmode(QHexCursor::OverwriteMode) {
  position2.line = position2.column = 0;
  position2.line = position2.column = 0;

  position1.line = position1.column = 0;
  position1.line = position1.column = 0;

  position2.nibbleindex = position1.nibbleindex = 1;
  setLineWidth(DEFAULT_HEX_LINE_LENGTH);
}

const QHexPosition &QHexCursor::selectionStart() const {
  if (position2.line < position1.line)
    return position2;

  if (position2.line == position1.line) {
    if (position2.column < position1.column)
      return position2;
  }

  return position1;
}

const QHexPosition &QHexCursor::selectionEnd() const {
  if (position2.line > position1.line)
    return position2;

  if (position2.line == position1.line) {
    if (position2.column > position1.column)
      return position2;
  }

  return position1;
}

const QHexPosition &QHexCursor::position() const {
  return position2;
}
QHexCursor::InsertionMode QHexCursor::insertionMode() const {
  return m_insertionmode;
}
int QHexCursor::selectionLength() const {
  return this->selectionEnd() - this->selectionStart() + 1;
}
quint64 QHexCursor::currentLine() const {
  return position2.line;
}
int QHexCursor::currentColumn() const {
  return position2.column;
}
int QHexCursor::currentNibble() const {
  return position2.nibbleindex;
}
quint64 QHexCursor::selectionLine() const {
  return position1.line;
}
int QHexCursor::selectionColumn() const {
  return position1.column;
}
int QHexCursor::selectionNibble() const {
  return position1.nibbleindex;
}

bool QHexCursor::isLineSelected(quint64 line) const {
  if (!this->hasSelection())
    return false;

  quint64 first = std::min(position2.line, position1.line);
  quint64 last = std::max(position2.line, position1.line);

  if ((line < first) || (line > last))
    return false;

  return true;
}

bool QHexCursor::hasSelection() const {
  return position2 != position1;
}

void QHexCursor::clearSelection() {
  position1 = position2;
  emit positionChanged();
}

void QHexCursor::moveTo(const QHexPosition &pos) {
  this->moveTo(pos.line, pos.column, pos.nibbleindex);
}
void QHexCursor::select(const QHexPosition &pos) {
  this->select(pos.line, pos.column, pos.nibbleindex);
}

void QHexCursor::moveTo(quint64 line, int column, int nibbleindex) {
  position1.line = line;
  position1.column = column;
  position1.nibbleindex = nibbleindex;

  this->select(line, column, nibbleindex);
}

void QHexCursor::select(quint64 line, int column, int nibbleindex) {
  position2.line = line;
  position2.column = column;
  position2.nibbleindex = nibbleindex;

  emit positionChanged();
}

void QHexCursor::moveTo(qint64 offset) {
  quint64 line = offset / m_lineWidth;
  this->moveTo(line, offset - (line * m_lineWidth));
}

void QHexCursor::select(int length) {
  // TODO: make this work for multi-line selection (also open PR on the original QHexView)
  this->select(position2.line, std::min(m_lineWidth - 1, position2.column + length - 1));
}

void QHexCursor::selectOffset(qint64 offset, int length) {
  this->moveTo(offset);
  this->select(length);
}

void QHexCursor::setInsertionMode(QHexCursor::InsertionMode mode) {
  bool differentmode = (m_insertionmode != mode);
  m_insertionmode = mode;

  if (differentmode)
    emit insertionModeChanged();
}

void QHexCursor::setLineWidth(quint8 width) {
  m_lineWidth = width;
  position2.lineWidth = width;
  position1.lineWidth = width;
}

void QHexCursor::switchInsertionMode() {
  if (m_insertionmode == QHexCursor::OverwriteMode)
    m_insertionmode = QHexCursor::InsertMode;
  else
    m_insertionmode = QHexCursor::OverwriteMode;

  emit insertionModeChanged();
}
