#include "insertcommand.h"

InsertCommand::InsertCommand(QHexBuffer *buffer, int offset, const QByteArray &data, QUndoCommand *parent): HexCommand(buffer, parent)
{
    m_offset = offset;
    m_data = data;
}

void InsertCommand::undo() { m_buffer->remove(m_offset, m_data.length()); }
void InsertCommand::redo() { m_buffer->insert(m_offset, m_data); }
