#include "hexcommand.h"

HexCommand::HexCommand(QHexBuffer *buffer, QUndoCommand *parent): QUndoCommand(parent), m_buffer(buffer), m_offset(0), m_length(0) { }
