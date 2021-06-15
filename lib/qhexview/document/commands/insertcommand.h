#ifndef INSERTCOMMAND_H
#define INSERTCOMMAND_H

#include "hexcommand.h"

class InsertCommand: public HexCommand
{
    public:
        InsertCommand(QHexBuffer* buffer, qint64 offset, const QByteArray& data, QUndoCommand* parent = nullptr);
        void undo() override;
        void redo() override;
};

#endif // INSERTCOMMAND_H
