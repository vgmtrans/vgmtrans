#ifndef QHEXCURSOR_H
#define QHEXCURSOR_H

#include <QObject>

#define DEFAULT_HEX_LINE_LENGTH      0x10
#define DEFAULT_AREA_IDENTATION      0x01

struct QHexPosition {
    quint64 line;
    int column;
    quint8 lineWidth;
    int nibbleindex;

    QHexPosition() = default;
    inline qint64 offset() const { return static_cast<qint64>(line * lineWidth) + column; }
    inline int operator-(const QHexPosition& rhs) const { return this->offset() - rhs.offset(); }
    inline bool operator==(const QHexPosition& rhs) const { return (line == rhs.line) && (column == rhs.column) && (nibbleindex == rhs.nibbleindex); }
    inline bool operator!=(const QHexPosition& rhs) const { return (line != rhs.line) || (column != rhs.column) || (nibbleindex != rhs.nibbleindex); }
};

class QHexCursor : public QObject
{
    Q_OBJECT

    public:
        enum InsertionMode { OverwriteMode, InsertMode };

    public:
        explicit QHexCursor(QObject *parent = nullptr);

    public:
        const QHexPosition& selectionStart() const;
        const QHexPosition& selectionEnd() const;
        const QHexPosition& position() const;
        InsertionMode insertionMode() const;
        int selectionLength() const;
        quint64 currentLine() const;
        int currentColumn() const;
        int currentNibble() const;
        quint64 selectionLine() const;
        int selectionColumn() const;
        int selectionNibble() const;
        bool atEnd() const;
        bool isLineSelected(quint64 line) const;
        bool hasSelection() const;
        void clearSelection();

    public:
        void moveTo(const QHexPosition& pos);
        void moveTo(quint64 line, int column, int nibbleindex = 1);
        void moveTo(qint64 offset);
        void select(const QHexPosition& pos);
        void select(quint64 line, int column, int nibbleindex = 1);
        void select(int length);
        void selectOffset(qint64 offset, int length);
        void setInsertionMode(InsertionMode mode);
        void setLineWidth(quint8 width);
        void switchInsertionMode();

    signals:
        void positionChanged();
        void insertionModeChanged();

    private:
        InsertionMode m_insertionmode;
        quint8 m_lineWidth;
        QHexPosition m_position, m_selection;
};

#endif // QHEXCURSOR_H
