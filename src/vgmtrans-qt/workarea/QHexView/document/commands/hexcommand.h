#ifndef HEXCOMMAND_H
#define HEXCOMMAND_H

#include <QUndoCommand>
#include "../buffer/qhexbuffer.h"

class HexCommand: public QUndoCommand
{
    public:
        HexCommand(QHexBuffer* buffer, QUndoCommand* parent = 0);

    protected:
        QHexBuffer* m_buffer;
        int m_offset, m_length;
        QByteArray m_data;
};

#endif // HEXCOMMAND_H
