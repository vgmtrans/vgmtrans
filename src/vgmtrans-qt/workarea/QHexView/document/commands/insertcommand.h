#ifndef INSERTCOMMAND_H
#define INSERTCOMMAND_H

#include "hexcommand.h"

class InsertCommand: public HexCommand
{
    public:
        InsertCommand(QHexBuffer* buffer, int offset, const QByteArray& data, QUndoCommand* parent = 0);
        void undo() override;
        void redo() override;
};

#endif // INSERTCOMMAND_H
