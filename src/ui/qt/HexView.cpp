#include "HexView.h"
#include "VGMFile.h"
#include "Helpers.h"

const int horzPadding = 5;

HexView::HexView(VGMFile *file, QWidget *parent)
        : QAbstractScrollArea(parent)
        , vgmfile(file)
        , mLinesPerScreen(0)
{
    QFont courierFont = QFont("Courier New", 13, QFont::Bold);
//    courierFont.setPixelSize(20);
    setFont(courierFont);
    QFontMetrics metrics(font());
    mLineHeight = metrics.height();
    mLineBaseline = metrics.ascent();

//    verticalScrollBar()->setRange(0, (vgmfile->unLength / 16) - heightLines);
    verticalScrollBar()->setPageStep(5);
    verticalScrollBar()->setSingleStep(1);

    setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContentsOnFirstShow);
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
    painter.setBackgroundMode(Qt::OpaqueMode);


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

        if (lineOffset >= vgmfile->dwOffset + vgmfile->unLength)
            break;

        uint8_t nCount = vgmfile->GetBytes (lineOffset, 16, b);

        QChar zeroChar = QChar('0');

        QString hexPortion = QString();
        for (int i=0; i<16; i++) {
            hexPortion.append(QString("%1 ").arg(b[i], 2, 16, zeroChar));
        }

        int charWidth = fontMetrics.averageCharWidth();

        painter.setBackground(QColor(43, 43, 43));
        painter.setPen(QColor(169, 183, 198));

        QString text = QString("%1    ")
                .arg((line * 16) + beginOffset, 8, 16, zeroChar).toUpper();
        painter.drawText(horzPadding, y + mLineBaseline, text);
        for(int i=0; i<nCount; i++) {

            VGMItem* item = vgmfile->GetItemFromOffset(lineOffset + i, false);
            QColor color = item ? colorForEventColor(item->color) : Qt::white;
            QColor textColor = item ? textColorForEventColor(item->color) : Qt::black;
            painter.setBackground(color);
            painter.setPen(textColor);

            painter.drawText(horzPadding + ((10 + i*3) * charWidth), y, 3 * charWidth, mLineHeight, Qt::AlignCenter, QString(" %1 ").arg(b[i], 2, 16, zeroChar).toUpper());
        }

        y += mLineHeight;
    }
}

void HexView::resizeEvent(QResizeEvent *event)
{
    const auto heightLines = this->viewport()->size().height() / mLineHeight;

    mLinesPerScreen = viewport()->height() / mLineHeight + 1;
    verticalScrollBar()->setRange(0, ((vgmfile->unLength + 15) / 16) - heightLines);
    QAbstractScrollArea::resizeEvent(event);
}