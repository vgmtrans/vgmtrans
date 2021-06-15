#include "qhexcursor.h"
#include <QWidget>

QHexCursor::QHexCursor(QObject *parent) : QObject(parent), m_insertionmode(QHexCursor::OverwriteMode)
{
    m_position.line = m_position.column = 0;
    m_position.line = m_position.column = 0;

    m_selection.line = m_selection.column = 0;
    m_selection.line = m_selection.column = 0;

    m_position.nibbleindex = m_selection.nibbleindex = 1;
    setLineWidth(DEFAULT_HEX_LINE_LENGTH);
}

const QHexPosition &QHexCursor::selectionStart() const
{
    if(m_position.line < m_selection.line)
        return m_position;

    if(m_position.line == m_selection.line)
    {
        if(m_position.column < m_selection.column)
            return m_position;
    }

    return m_selection;
}

const QHexPosition &QHexCursor::selectionEnd() const
{
    if(m_position.line > m_selection.line)
        return m_position;

    if(m_position.line == m_selection.line)
    {
        if(m_position.column > m_selection.column)
            return m_position;
    }

    return m_selection;
}

const QHexPosition &QHexCursor::position() const { return m_position; }
QHexCursor::InsertionMode QHexCursor::insertionMode() const { return m_insertionmode; }
int QHexCursor::selectionLength() const { return this->selectionEnd() - this->selectionStart() + 1;  }
quint64 QHexCursor::currentLine() const { return m_position.line; }
int QHexCursor::currentColumn() const { return m_position.column; }
int QHexCursor::currentNibble() const { return m_position.nibbleindex; }
quint64 QHexCursor::selectionLine() const { return m_selection.line; }
int QHexCursor::selectionColumn() const { return m_selection.column; }
int QHexCursor::selectionNibble() const { return m_selection.nibbleindex;  }

bool QHexCursor::isLineSelected(quint64 line) const
{
    if(!this->hasSelection())
        return false;

    quint64 first = std::min(m_position.line, m_selection.line);
    quint64 last = std::max(m_position.line, m_selection.line);

    if((line < first) || (line > last))
        return false;

    return true;
}

bool QHexCursor::hasSelection() const { return m_position != m_selection; }

void QHexCursor::clearSelection()
{
    m_selection = m_position;
    emit positionChanged();
}

void QHexCursor::moveTo(const QHexPosition &pos) { this->moveTo(pos.line, pos.column, pos.nibbleindex); }
void QHexCursor::select(const QHexPosition &pos) { this->select(pos.line, pos.column, pos.nibbleindex); }

void QHexCursor::moveTo(quint64 line, int column, int nibbleindex)
{
    m_selection.line = line;
    m_selection.column = column;
    m_selection.nibbleindex = nibbleindex;

    this->select(line, column, nibbleindex);
}

void QHexCursor::select(quint64 line, int column, int nibbleindex)
{
    m_position.line = line;
    m_position.column = column;
    m_position.nibbleindex = nibbleindex;

    emit positionChanged();
}

void QHexCursor::moveTo(qint64 offset)
{
    quint64 line = offset / m_lineWidth;
    this->moveTo(line, offset - (line * m_lineWidth));
}

void QHexCursor::select(int length) { this->select(m_position.line, std::min(m_lineWidth - 1, m_position.column + length - 1)); }

void QHexCursor::selectOffset(qint64 offset, int length)
{
    this->moveTo(offset);
    this->select(length);
}

void QHexCursor::setInsertionMode(QHexCursor::InsertionMode mode)
{
    bool differentmode = (m_insertionmode != mode);
    m_insertionmode = mode;

	if (differentmode)
		emit insertionModeChanged();
}

void QHexCursor::setLineWidth(quint8 width)
{
    m_lineWidth = width;
    m_position.lineWidth = width;
    m_selection.lineWidth = width;
}

void QHexCursor::switchInsertionMode()
{
    if(m_insertionmode == QHexCursor::OverwriteMode)
        m_insertionmode = QHexCursor::InsertMode;
    else
        m_insertionmode = QHexCursor::OverwriteMode;

    emit insertionModeChanged();
}
