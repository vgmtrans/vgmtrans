#ifndef QHEXRENDERER_H
#define QHEXRENDERER_H

/*
 * Nibble encoding:
 *           AB -> [A][B]
 * Nibble Index:    1  0
 */

#include <QTextDocument>
#include <QPainter>
#include "qhexdocument.h"

class QHexRenderer : public QObject
{
    Q_OBJECT

    public:
        enum { AddressArea, HexArea, AsciiArea, ExtraArea };

    public:
        explicit QHexRenderer(QHexDocument* document, const QFontMetrics& fontmetrics, QObject *parent = nullptr);
        void renderFrame(QPainter* painter);
        void render(QPainter* painter, int start, int count, int firstline);
        void enableCursor(bool b = true);
        void selectArea(const QPoint& pt);

    public:
        void blinkCursor();
        bool hitTest(const QPoint& pt, QHexPosition* position, int firstline) const;
        int hitTestArea(const QPoint& pt) const;
        int selectedArea() const;
        int documentLastLine() const;
        int documentLastColumn() const;
        int documentLines() const;
        int documentWidth() const;
        int lineHeight() const;
        QRect getLineRect(int line, int firstline) const;

    private:
        QString hexString(int line, QByteArray *rawline = nullptr) const;
        QString asciiString(int line, QByteArray *rawline = nullptr) const;
        QByteArray getLine(int line) const;
        int rendererLength() const;
        int getAddressWidth() const;
        int getHexColumnX() const;
        int getAsciiColumnX() const;
        int getEndColumnX() const;
        int getCellWidth() const;
        int getNibbleIndex(int line, int relx) const;
        void unprintableChars(QByteArray &ascii) const;

    private:
        void applyDocumentStyles(QPainter* painter, QTextDocument *textdocument) const;
        void applyBasicStyle(QTextCursor& textcursor, const QByteArray& rawline, int factor = 1) const;
        void applyMetadata(QTextCursor& textcursor, int line, int factor = 1) const;
        void applySelection(QTextCursor& textcursor, int line, int factor = 1) const;
        void applyCursorAscii(QTextCursor& textcursor, int line) const;
        void applyCursorHex(QTextCursor& textcursor, int line) const;
        void drawAddress(QPainter *painter, const QPalette &palette, const QRect &linerect, int line);
        void drawHex(QPainter *painter, const QPalette &palette, const QRect &linerect, int line);
        void drawAscii(QPainter *painter, const QPalette &palette, const QRect &linerect, int line);

    private:
        QHexDocument* m_document;
        QFontMetrics m_fontmetrics;
        int m_selectedarea;
        bool m_cursorenabled;
};

#endif // QHEXRENDERER_H
