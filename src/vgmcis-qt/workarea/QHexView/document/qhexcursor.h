#ifndef QHEXCURSOR_H
#define QHEXCURSOR_H

#include <QObject>

#define HEX_LINE_LENGTH      0x10
#define HEX_LINE_LAST_COLUMN (HEX_LINE_LENGTH - 1)

struct QHexPosition {
    int line, column, nibbleindex;

    QHexPosition() = default;
    QHexPosition(const QHexPosition&) = default;

    QHexPosition& operator=(const QHexPosition& rhs) {
        line = rhs.line;
        column = rhs.column;
        nibbleindex = rhs.nibbleindex;
        return *this;
    }

    int offset() const { return (line * HEX_LINE_LENGTH) + column; }
    int operator-(const QHexPosition& rhs) const { return this->offset() - rhs.offset(); }
    bool operator==(const QHexPosition& rhs) const { return (line == rhs.line) && (column == rhs.column) && (nibbleindex == rhs.nibbleindex); }
    bool operator!=(const QHexPosition& rhs) const { return (line != rhs.line) || (column != rhs.column) || (nibbleindex != rhs.nibbleindex); }
};

class QHexCursor : public QObject
{
    Q_OBJECT

    public:
        enum InsertionMode { OverwriteMode, InsertMode };

    public:
        explicit QHexCursor(QObject *parent = 0);

    public:
        const QHexPosition& selectionStart() const;
        const QHexPosition& selectionEnd() const;
        const QHexPosition& position() const;
        InsertionMode insertionMode() const;
        int selectionLength() const;
        int currentLine() const;
        int currentColumn() const;
        int currentNibble() const;
        int selectionLine() const;
        int selectionColumn() const;
        int selectionNibble() const;
        bool atEnd() const;
        bool isLineSelected(int line) const;
        bool hasSelection() const;
        void clearSelection();

    public:
        void moveTo(const QHexPosition& pos);
        void moveTo(int line, int column, int nibbleindex = 1);
        void moveTo(int offset);
        void select(const QHexPosition& pos);
        void select(int line, int column, int nibbleindex = 1);
        void select(int length);
        void selectOffset(int offset, int length);
        void setInsertionMode(InsertionMode mode);
        void switchInsertionMode();

    signals:
        void positionChanged();
        void insertionModeChanged();

    private:
        InsertionMode m_insertionmode;
        QHexPosition m_position, m_selection;
};

#endif // QHEXCURSOR_H
