#include "replacecommand.h"

ReplaceCommand::ReplaceCommand(QHexBuffer *buffer, int offset, const QByteArray &data, QUndoCommand *parent): HexCommand(buffer, parent)
{
    m_offset = offset;
    m_data = data;
}

void ReplaceCommand::undo() { m_buffer->replace(m_offset, m_olddata); }

void ReplaceCommand::redo()
{
    m_olddata = m_buffer->read(m_offset, m_data.length());
    m_buffer->replace(m_offset, m_data);
}
