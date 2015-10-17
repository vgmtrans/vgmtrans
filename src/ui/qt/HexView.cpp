#include "HexView.h"
#include "VGMFile.h"
#include "Helpers.h"

const int horzPadding = 10;

HexView::HexView(VGMFile *file, QWidget *parent)
        : QAbstractScrollArea(parent)
        , vgmfile(file)
        , mLinesPerScreen(0)
{
    QFont courierFont = QFont("Courier New", 20);
//    courierFont.setPixelSize(20);
    setFont(courierFont);
    QFontMetrics metrics(font());
    mLineHeight = metrics.height();
    mLineBaseline = metrics.ascent();

    verticalScrollBar()->setRange(0, vgmfile->unLength / 16);
    verticalScrollBar()->setPageStep(5);
    verticalScrollBar()->setSingleStep(1);
}

HexView::~HexView()
{
}


void HexView::paintEvent(QPaintEvent *event)
{
//    QAbstractScrollArea::paintEvent(event);

    QPainter painter(this->viewport());
    QFontMetrics fontMetrics = painter.fontMetrics();

//    bool didElide = false;
    int lineSpacing = fontMetrics.lineSpacing();

    int y = 0;

    int firstLine = verticalScrollBar()->value();
    int lastLine = firstLine + mLinesPerScreen;

//    if (lastLine > vgmfile->lineCount()) {
//        lastLine = vgmfile->lineCount();
//    }

    auto beginOffset = vgmfile->dwOffset;

    for (int line = firstLine; line < lastLine; ++line) {

        uint8_t b[16];

        uint32_t lineOffset = line * 16 + beginOffset;
        drawLineColor(painter, fontMetrics, line);

        uint8_t nCount = vgmfile->GetBytes (lineOffset, 16, b);

        QChar zeroChar = QChar('0');

        QString hexPortion = QString();
        for (int i=0; i<16; i++) {
            hexPortion.append(QString("%1 ").arg(b[i], 2, 16, zeroChar));
        }

        QString text = QString("%1    %2")
                .arg((line * 16) + beginOffset, 8, 16, zeroChar)
                .arg(hexPortion);
//        QString text = mModel->textAt(line);

//        text = text.left(horizontalScrollBar()->value());

        painter.drawText(horzPadding, y + mLineBaseline, text);

        y += mLineHeight;
    }


//    QTextLayout textLayout("BLAH", painter.font());
//    textLayout.beginLayout();
//    forever {
//        QTextLine line = textLayout.createLine();
//
//        if (!line.isValid())
//            break;
//
//        line.setLineWidth(width());
//        int nextLineY = y + lineSpacing;
//
//        if (height() >= nextLineY + lineSpacing) {
//            line.draw(&painter, QPoint(0, y));
//            y = nextLineY;
//        } else {
////            QString lastLine = content.mid(line.textStart());
////            QString elidedLastLine = fontMetrics.elidedText(lastLine, Qt::ElideRight, width());
////            painter.drawText(QPoint(0, y + fontMetrics.ascent()), elidedLastLine);
////            line = textLayout.createLine();
////            didElide = line.isValid();
//            break;
//        }
//    }
//    textLayout.endLayout();
}

void HexView::drawLineColor(QPainter &painter, QFontMetrics &fontMetrics, uint32_t line) {

    uint32_t lineOffset = line * 16 + vgmfile->dwOffset;
    int charWidth = fontMetrics.averageCharWidth();
    int charHeight = fontMetrics.lineSpacing();
    int scrollLine = verticalScrollBar()->value();

    qDebug() << "charWidth: " << charWidth << "  Width of 0: " << fontMetrics.width('0');

    int i = 0;
    uint32_t offset = lineOffset;
    while (i < 16) {
        VGMItem* item = vgmfile->GetItemFromOffset(offset, false);
        if (!item) {
            offset++;
            i++;
            continue;
        }
        uint32_t itemLength = item->unLength;

        QColor color = colorForEventColor(item->color);
        QRect r = QRect(horzPadding-3 + charWidth * (12 + (i * 3)), (line - scrollLine) * charHeight, min(16 - i, (int)itemLength) * charWidth * 3, charHeight);
        painter.fillRect(r, color);

        offset += itemLength;
        i += itemLength;
    }
}

void HexView::resizeEvent(QResizeEvent *event)
{
    mLinesPerScreen = viewport()->height() / mLineHeight + 1;
    QAbstractScrollArea::resizeEvent(event);
    qDebug() << "lines per=" << mLinesPerScreen;
}