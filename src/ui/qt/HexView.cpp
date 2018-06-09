/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "HexView.h"
#include "VGMFile.h"
#include "Helpers.h"

constexpr int hor_padding = 5;

HexView::HexView(VGMFile *vgmfile, QWidget *parent)
    : QAbstractScrollArea(parent), ui_hexview_vgmfile(vgmfile) {
  // Use whatever monospace font the system can offer
  QFont font("Courier");
  font.setStyleHint(QFont::TypeWriter);
  font.setPointSize(12);
  setFont(font);

  QFontMetrics metrics(font);
  hexview_line_height = metrics.height();
  hexview_line_ascent = metrics.ascent();

  QSize viewport_size = viewport()->size();
  verticalScrollBar()->setRange(0, (vgmfile->unLength / 16) - 1);
  verticalScrollBar()->setPageStep(viewport_size.height() / hexview_line_height);
  verticalScrollBar()->setSingleStep(1);
}

void HexView::paintEvent(QPaintEvent *event) {
  QPainter painter(viewport());
  painter.setBackground(palette().color(QPalette::Base));
  painter.setPen(palette().color(QPalette::WindowText));

  int y = 5;
  int hexview_firstline = verticalScrollBar()->value();
  int lastLine = hexview_firstline + hexview_lines_per_screen;
  QChar null_char = QChar('0');

  const auto begin_offset = ui_hexview_vgmfile->dwOffset;
  const int hexview_font_width = painter.fontMetrics().averageCharWidth();

  painter.drawText(hor_padding, y + hexview_line_ascent, "Offset (h)");
  for (int i = 0; i < 16; i++) {
    painter.drawText(hor_padding + ((10 + 3 * i) * hexview_font_width), y, 3 * hexview_font_width,
                     hexview_line_height, Qt::AlignCenter, QString(i).toLatin1().toHex());
  }
  y += hexview_line_height;

  for (int line = hexview_firstline; line < lastLine; ++line) {
    char b[16];
    uint32_t lineOffset = line * 16 + begin_offset;

    if (lineOffset >= ui_hexview_vgmfile->dwOffset + ui_hexview_vgmfile->unLength)
      break;

    // Make sure the colors are neutral and print out the address
    painter.setBackground(palette().color(QPalette::Base));
    painter.setPen(palette().color(QPalette::WindowText));

    QString hexview_address =
        QString("%1    ").arg((line * 16) + begin_offset, 8, 16, null_char).toUpper();
    painter.drawText(hor_padding, y + hexview_line_ascent, hexview_address);

    uint8_t bytes_to_print = ui_hexview_vgmfile->GetBytes(lineOffset, 16, b);

    QByteArray buffer = QByteArray(b, 16);
    for (int i = 0; i < bytes_to_print; i++) {
      // Retrieve the item
      VGMItem *item = ui_hexview_vgmfile->GetItemFromOffset(lineOffset + i, false);

      // Query the corresponding color for the given item
      QColor color = item ? colorForEventColor(item->color) : Qt::white;
      QColor text_color = item ? textColorForEventColor(item->color) : Qt::black;
      painter.setBrush(color);
      painter.setPen(color);

      QRect hexview_item = QRect(hor_padding + ((10 + 3 * i) * hexview_font_width), y,
                                 3 * hexview_font_width, hexview_line_height);
      painter.drawRect(hexview_item);

      painter.setPen(text_color);
      painter.drawText(hexview_item, Qt::AlignCenter,
                       QString("%1").arg((unsigned char)buffer.at(i), 2, 16, QChar('0')).toUpper());
    }

    y += hexview_line_height;
  }
}

void HexView::resizeEvent(QResizeEvent *event) {
  hexview_lines_per_screen = viewport()->height() / hexview_line_height + 1;
  QAbstractScrollArea::resizeEvent(event);
}
