#ifndef REPLACECOMMAND_H
#define REPLACECOMMAND_H

#include "hexcommand.h"

class ReplaceCommand: public HexCommand
{
    public:
        ReplaceCommand(QHexBuffer* buffer, qint64 offset, const QByteArray& data, QUndoCommand* parent = nullptr);
        void undo() override;
        void redo() override;

    private:
        QByteArray m_olddata;
};

#endif // REPLACECOMMAND_H
