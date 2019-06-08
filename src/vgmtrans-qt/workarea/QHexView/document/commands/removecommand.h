#ifndef REMOVECOMMAND_H
#define REMOVECOMMAND_H

#include "hexcommand.h"

class RemoveCommand: public HexCommand
{
    public:
        RemoveCommand(QHexBuffer* buffer, int offset, int length, QUndoCommand* parent = 0);
        void undo() override;
        void redo() override;
};

#endif // REMOVECOMMAND_H
