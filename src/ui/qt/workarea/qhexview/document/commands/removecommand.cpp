#include "removecommand.h"

RemoveCommand::RemoveCommand(QHexBuffer *buffer, qint64 offset, int length, QUndoCommand *parent): HexCommand(buffer, parent)
{
    m_offset = offset;
    m_length = length;
}

void RemoveCommand::undo() { m_buffer->insert(m_offset, m_data); }

void RemoveCommand::redo()
{
    m_data = m_buffer->read(m_offset, m_length); // Backup data
    m_buffer->remove(m_offset, m_length);
}
