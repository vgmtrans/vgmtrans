#include "qhexrenderer.h"
#include <QApplication>
#include <QTextCursor>
#include <QWidget>
#include <cctype>
#include <cmath>

#define HEX_UNPRINTABLE_CHAR '.'

QHexRenderer::QHexRenderer(QHexDocument *document, const QFontMetrics &fontmetrics, QObject *parent)
    : QObject(parent), m_document(document), m_fontmetrics(fontmetrics) {
    m_selectedarea = QHexRenderer::HexArea;
    m_cursorenabled = false;
}

void QHexRenderer::renderFrame(QPainter *painter) {
    QRect rect = painter->window();
    int hexx = this->getHexColumnX();
    int asciix = this->getAsciiColumnX();
    int endx = this->getEndColumnX();

    painter->drawLine(rect.x() + hexx, rect.top(), rect.x() + hexx, rect.bottom());

    painter->drawLine(rect.x() + asciix, rect.top(), rect.x() + asciix, rect.bottom());

    painter->drawLine(rect.x() + endx, rect.top(), rect.x() + endx, rect.bottom());
}

void QHexRenderer::render(QPainter *painter, int start, int count, int firstline) {
    int end = start + count;
    QPalette palette = qApp->palette();

    for (int line = start; line < std::min(end, this->documentLines()); line++) {
        QRect linerect = this->getLineRect(line, firstline);

        if (line % 2)
            painter->fillRect(linerect, palette.brush(QPalette::Window));
        else
            painter->fillRect(linerect, palette.brush(QPalette::Base));

        this->drawAddress(painter, palette, linerect, line);
        this->drawHex(painter, palette, linerect, line);
        this->drawAscii(painter, palette, linerect, line);
    }
}

void QHexRenderer::enableCursor(bool b) {
    m_cursorenabled = b;
}

void QHexRenderer::selectArea(const QPoint &pt) {
    int area = this->hitTestArea(pt);

    if (area == QHexRenderer::AddressArea)
        return;

    m_selectedarea = area;
}

bool QHexRenderer::hitTest(const QPoint &pt, QHexPosition *position, int firstline) const {
    int area = this->hitTestArea(pt);

    if (area == QHexRenderer::AddressArea)
        return false;

    position->line = std::min(firstline + (pt.y() / this->lineHeight()), this->documentLastLine());

    if (area == QHexRenderer::HexArea) {
        int relx = pt.x() - this->getHexColumnX();
        position->column = relx / (this->getCellWidth() * 3);
        position->nibbleindex = this->getNibbleIndex(position->line, relx);
    } else {
        int relx = pt.x() - this->getAsciiColumnX() - this->getCellWidth();
        position->column = relx / this->getCellWidth();
        position->nibbleindex = 1;
    }

    if (position->line == this->documentLastLine())  // Check last line's columns
    {
        QByteArray ba = this->getLine(position->line);
        position->column = std::min(position->column, ba.length());
    } else
        position->column = std::min(position->column, HEX_LINE_LAST_COLUMN);

    return true;
}

int QHexRenderer::hitTestArea(const QPoint &pt) const {
    if ((pt.x() >= 0) && (pt.x() <= this->getHexColumnX()))
        return QHexRenderer::AddressArea;

    if ((pt.x() > this->getHexColumnX()) && (pt.x() < this->getAsciiColumnX()))
        return QHexRenderer::HexArea;

    if ((pt.x() > this->getAsciiColumnX()) && (pt.x() < this->getEndColumnX()))
        return QHexRenderer::AsciiArea;

    return QHexRenderer::ExtraArea;
}

int QHexRenderer::selectedArea() const {
    return m_selectedarea;
}
int QHexRenderer::documentLastLine() const {
    return this->documentLines() - 1;
}
int QHexRenderer::documentLastColumn() const {
    return this->getLine(this->documentLastLine()).length();
}
int QHexRenderer::documentLines() const {
    return std::ceil(this->rendererLength() / static_cast<float>(HEX_LINE_LENGTH));
}
int QHexRenderer::documentWidth() const {
    return this->getAsciiColumnX() + (this->getCellWidth() * (HEX_LINE_LENGTH + 1));
}
int QHexRenderer::lineHeight() const {
    return m_fontmetrics.height();
}
QRect QHexRenderer::getLineRect(int line, int firstline) const {
    return QRect(0, (line - firstline) * m_fontmetrics.height(), this->getEndColumnX(),
                 m_fontmetrics.height());
}

QString QHexRenderer::hexString(int line, QByteArray *rawline) const {
    QByteArray lrawline = this->getLine(line);

    if (rawline)
        *rawline = lrawline;

    return lrawline.toHex(' ').toUpper() + " ";
}

QString QHexRenderer::asciiString(int line, QByteArray *rawline) const {
    QByteArray lrawline = this->getLine(line);

    if (rawline)
        *rawline = lrawline;

    QByteArray ascii = lrawline;
    this->unprintableChars(ascii);
    return ascii;
}

QByteArray QHexRenderer::getLine(int line) const {
    return m_document->read(line * HEX_LINE_LENGTH, HEX_LINE_LENGTH);
}
void QHexRenderer::blinkCursor() {
    m_cursorenabled = !m_cursorenabled;
}
int QHexRenderer::rendererLength() const {
    return m_document->length() + 1;
}

int QHexRenderer::getAddressWidth() const {
    if (this->rendererLength() <= 0xFFFF)
        return 8;

    if (static_cast<unsigned int>(this->rendererLength()) <= 0xFFFFFFFF)
        return 16;

    return QString::number(this->rendererLength(), 16).length();
}

int QHexRenderer::getHexColumnX() const {
    return this->getCellWidth() * (this->getAddressWidth() + 1);
}
int QHexRenderer::getAsciiColumnX() const {
    return this->getHexColumnX() + (this->getCellWidth() * (HEX_LINE_LENGTH * 3));
}
int QHexRenderer::getEndColumnX() const {
    return this->getAsciiColumnX() + (this->getCellWidth() * (HEX_LINE_LENGTH + 1));
}

int QHexRenderer::getCellWidth() const {
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    return m_fontmetrics.horizontalAdvance(" ");
#else
    return m_fontmetrics.width(" ");
#endif
}

int QHexRenderer::getNibbleIndex(int line, int relx) const {
    relx -= this->getCellWidth();  // Remove padding
    QString hexstring = this->hexString(line);

    for (int i = 0; i < hexstring.size(); i++) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
        int x = m_fontmetrics.horizontalAdvance(hexstring, i + 1);
#else
        int x = m_fontmetrics.width(hexstring, i + 1);
#endif
        if (x < relx)
            continue;

        if ((i == (hexstring.size() - 1)) || (hexstring[i + 1] == ' '))
            return 0;

        break;
    }

    return 1;
}

void QHexRenderer::unprintableChars(QByteArray &ascii) const {
    for (char &ch : ascii) {
        if (std::isprint(static_cast<unsigned char>(ch)))
            continue;

        ch = HEX_UNPRINTABLE_CHAR;
    }
}

void QHexRenderer::applyDocumentStyles(QPainter *painter, QTextDocument *textdocument) const {
    textdocument->setDocumentMargin(0);
    textdocument->setUndoRedoEnabled(false);
    textdocument->setDefaultFont(painter->font());
}

void QHexRenderer::applyBasicStyle(QTextCursor &textcursor, const QByteArray &rawline,
                                   int factor) const {
    QPalette palette = qApp->palette();
    QColor color = palette.color(QPalette::WindowText);

    if (color.lightness() < 50) {
        if (color == Qt::black)
            color = Qt::gray;
        else
            color = color.lighter();
    } else
        color = color.darker();

    QTextCharFormat charformat;
    charformat.setForeground(color);

    for (int i = 0; i < rawline.length(); i++) {
        if ((rawline[i] != 0x00) && (static_cast<uchar>(rawline[i]) != 0xFF))
            continue;

        textcursor.setPosition(i * factor);
        textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, factor);
        textcursor.setCharFormat(charformat);
    }
}

void QHexRenderer::applyMetadata(QTextCursor &textcursor, int line, int factor) const {
    QHexMetadata *metadata = m_document->metadata();

    if (!metadata->hasMetadata(line))
        return;

    const QHexLineMetadata &linemetadata = metadata->get(line);

    for (const QHexMetadataItem &mi : linemetadata) {
        QTextCharFormat charformat;

        if (mi.background.isValid())
            charformat.setBackground(mi.background);

        if (mi.foreground.isValid())
            charformat.setForeground(mi.foreground);

        textcursor.setPosition(mi.start * factor);
        textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                                (mi.length * factor) - (factor > 1 ? 1 : 0));
        textcursor.setCharFormat(charformat);
    }
}

void QHexRenderer::applySelection(QTextCursor &textcursor, int line, int factor) const {
    QHexCursor *cursor = m_document->cursor();

    if (!cursor->isLineSelected(line))
        return;

    const QHexPosition &startsel = cursor->selectionStart();
    const QHexPosition &endsel = cursor->selectionEnd();

    if (startsel.line == endsel.line) {
        textcursor.setPosition(startsel.column * factor);
        textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                                ((endsel.column - startsel.column + 1) * factor) - 1);
    } else {
        if (line == startsel.line)
            textcursor.setPosition(startsel.column * factor);
        else
            textcursor.setPosition(0);

        if (line == endsel.line)
            textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor,
                                    ((endsel.column + 1) * factor) - 1);
        else
            textcursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
    }

    QPalette palette = qApp->palette();

    QTextCharFormat charformat;
    charformat.setBackground(palette.color(QPalette::Highlight));
    charformat.setForeground(palette.color(QPalette::HighlightedText));
    textcursor.setCharFormat(charformat);
}

void QHexRenderer::applyCursorAscii(QTextCursor &textcursor, int line) const {
    QHexCursor *cursor = m_document->cursor();

    if ((line != cursor->currentLine()) || !m_cursorenabled)
        return;

    textcursor.clearSelection();
    textcursor.setPosition(m_document->cursor()->currentColumn());
    textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);

    QPalette palette = qApp->palette();

    QTextCharFormat charformat;

    if ((cursor->insertionMode() == QHexCursor::OverwriteMode) ||
        (m_selectedarea != QHexRenderer::AsciiArea)) {
        charformat.setForeground(palette.color(QPalette::Window));

        if (m_selectedarea == QHexRenderer::AsciiArea)
            charformat.setBackground(palette.color(QPalette::WindowText));
        else
            charformat.setBackground(palette.color(QPalette::WindowText).lighter(250));
    }

    textcursor.setCharFormat(charformat);
}

void QHexRenderer::applyCursorHex(QTextCursor &textcursor, int line) const {
    QHexCursor *cursor = m_document->cursor();

    if ((line != cursor->currentLine()) || !m_cursorenabled)
        return;

    textcursor.clearSelection();
    textcursor.setPosition(m_document->cursor()->currentColumn() * 3);

    if ((m_selectedarea == QHexRenderer::HexArea) && !m_document->cursor()->currentNibble())
        textcursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor);

    textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);

    if (m_selectedarea == QHexRenderer::AsciiArea)
        textcursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);

    QPalette palette = qApp->palette();
    QTextCharFormat charformat;

    if ((cursor->insertionMode() == QHexCursor::OverwriteMode) ||
        (m_selectedarea != QHexRenderer::HexArea)) {
        charformat.setForeground(palette.color(QPalette::Window));

        if (m_selectedarea == QHexRenderer::HexArea)
            charformat.setBackground(palette.color(QPalette::WindowText));
        else
            charformat.setBackground(palette.color(QPalette::WindowText).lighter(250));
    }

    textcursor.setCharFormat(charformat);
}

void QHexRenderer::drawAddress(QPainter *painter, const QPalette &palette, const QRect &linerect,
                               int line) {
    QRect addressrect = linerect;
    addressrect.setWidth(this->getHexColumnX());

    painter->save();
    painter->setPen(palette.color(QPalette::Highlight));

    painter->drawText(addressrect, Qt::AlignHCenter | Qt::AlignVCenter,
                      QString("%1")
                          .arg(line * HEX_LINE_LENGTH + m_document->baseAddress(),
                               this->getAddressWidth(), 16, QLatin1Char('0'))
                          .toUpper());
    painter->restore();
}

void QHexRenderer::drawHex(QPainter *painter, const QPalette &palette, const QRect &linerect,
                           int line) {
    QTextDocument textdocument;
    QTextCursor textcursor(&textdocument);
    QByteArray rawline;

    textcursor.insertText(this->hexString(line, &rawline));

    if (line == this->documentLastLine())
        textcursor.insertText(" ");

    QRect hexrect = linerect;
    hexrect.setX(this->getHexColumnX() + this->getCellWidth());

    this->applyDocumentStyles(painter, &textdocument);
    this->applyBasicStyle(textcursor, rawline, 3);
    this->applyMetadata(textcursor, line, 3);
    this->applySelection(textcursor, line, 3);
    this->applyCursorHex(textcursor, line);

    painter->save();
    painter->translate(hexrect.topLeft());
    textdocument.drawContents(painter);
    painter->restore();
}

void QHexRenderer::drawAscii(QPainter *painter, const QPalette &palette, const QRect &linerect,
                             int line) {
    QTextDocument textdocument;
    QTextCursor textcursor(&textdocument);
    QByteArray rawline;
    textcursor.insertText(this->asciiString(line, &rawline));

    if (line == this->documentLastLine())
        textcursor.insertText(" ");

    QRect asciirect = linerect;
    asciirect.setX(this->getAsciiColumnX() + this->getCellWidth());

    this->applyDocumentStyles(painter, &textdocument);
    this->applyBasicStyle(textcursor, rawline);
    this->applyMetadata(textcursor, line);
    this->applySelection(textcursor, line);
    this->applyCursorAscii(textcursor, line);

    painter->save();
    painter->translate(asciirect.topLeft());
    textdocument.drawContents(painter);
    painter->restore();
}
