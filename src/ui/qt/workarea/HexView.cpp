/*
* VGMTrans (c) 2002-2023
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexView.h"
#include "VGMFile.h"
#include "Helpers.h"
#include "UIHelpers.h"
#include "LambdaEventFilter.h"
#include <QFontDatabase>
#include <QPainter>
#include <QApplication>
#include <QPaintEvent>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

#define BYTES_PER_LINE 16
#define NUM_ADDRESS_NIBBLES 8
#define ADDRESS_SPACING_CHARS 4
#define HEX_TO_ASCII_SPACING_CHARS 4
#define SELECTION_PADDING 20

HexView::HexView(VGMFile* vgmfile, QWidget *parent) :
      QWidget(parent), vgmfile(vgmfile), selectedItem(nullptr) {

  QFont font("Ubuntu Mono", QApplication::font().pointSize() + 4);
  font.setPointSizeF(QApplication::font().pointSizeF() + 4.0);

  this->setFont(font);
  this->setFocusPolicy(Qt::StrongFocus);

  overlay = new QWidget(this);
  overlay->hide();

  overlay->installEventFilter(
      new LambdaEventFilter([this](QObject* obj, QEvent* event) -> bool {
        return this->handleOverlayPaintEvent(obj, event);
      })
  );

  overlayOpacityEffect = new QGraphicsOpacityEffect(this);
  overlay->setGraphicsEffect(overlayOpacityEffect);
}

void HexView::setFont(const QFont& font) {
  QFontMetrics fontMetrics(font);
  this->charWidth = fontMetrics.averageCharWidth();
  this->lineHeight = fontMetrics.height();
  QWidget::setFont(font);
  this->setMinimumWidth(getVirtualWidth());
  this->setFixedHeight(getVirtualHeight());

  redrawOverlay();
  redrawSelectedItem();
}

int HexView::getVirtualHeight() {
  return lineHeight * getTotalLines();
}

int HexView::getVirtualWidth() {
  const int numChars = NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS + (BYTES_PER_LINE * 3) +
                       HEX_TO_ASCII_SPACING_CHARS + BYTES_PER_LINE;
  return (numChars * charWidth) + 15;
}

int HexView::getTotalLines() {
  return (vgmfile->unLength + BYTES_PER_LINE - 1) / BYTES_PER_LINE;
}

void HexView::setSelectedItem(VGMItem *item) {
  selectedItem = item;

  // Delete the selection view if it exists
  if (selectionView != nullptr) {
    delete selectionView;
    selectionView = nullptr;
  }

  if (selectedItem == nullptr) {
    showOverlay(false, true);
  } else {
    showOverlay(true, true);
    drawSelectedItem();

    // Handle scrolling to the selected event
    QScrollArea* scrollArea = getContainingScrollArea(this);
    if (scrollArea) {
      auto itemBaseOffset = static_cast<int>(item->dwOffset - vgmfile->dwOffset);
      int line = itemBaseOffset / BYTES_PER_LINE;
      int endLine = static_cast<int>(item->dwOffset + item->unLength - vgmfile->dwOffset) / BYTES_PER_LINE;

      int viewPortStartLine = scrollArea->verticalScrollBar()->value() / lineHeight;
      int viewPortEndLine = viewPortStartLine + ((scrollArea->viewport()->height()) / lineHeight);

      if (line < viewPortStartLine) {
        scrollArea->verticalScrollBar()->setValue(line * lineHeight);
      } else if (endLine > viewPortEndLine) {

        if ((endLine - line) > (scrollArea->size().height() / lineHeight)) {
          int y = (line * lineHeight);
          scrollArea->verticalScrollBar()->setValue(y);
        } else {
          int y = ((endLine + 1) * lineHeight) + 1 - scrollArea->size().height();
          scrollArea->verticalScrollBar()->setValue(y);
        }
      }
    }
  }
}

void HexView::resizeOverlays(int height) {
  overlay->setGeometry(
      ((NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS) * charWidth) - charWidth/2, overlay->y(),
      ((BYTES_PER_LINE * 3 + HEX_TO_ASCII_SPACING_CHARS + BYTES_PER_LINE) * charWidth) + charWidth/2,
      height
  );
}

void HexView::redrawOverlay() {
  QScrollArea* scrollArea = getContainingScrollArea(this);
  if (scrollArea) {
    resizeOverlays(scrollArea->height());
  }
}

void HexView::redrawSelectedItem() {
  if (selectionView != nullptr) {
    delete selectionView;
    selectionView = nullptr;
  }
  if (selectedItem) {
    drawSelectedItem();
  }
}

void HexView::changeEvent(QEvent *event) {
  if (event->type() == QEvent::ParentChange) {
    QScrollArea* scrollArea = getContainingScrollArea(this);
    if (scrollArea) {
      scrollArea->installEventFilter(
        new LambdaEventFilter([this](QObject* obj, QEvent* event) -> bool {
            if (event->type() == QEvent::Resize) {
              redrawOverlay();
              return false;
            }
            return false;
          })
      );

      connect(scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) { overlay->move(overlay->x(), value);
      });
    }
  }
  QWidget::changeEvent(event);
}

void HexView::keyPressEvent(QKeyEvent* event) {
  if (! selectedItem) {
    return QWidget::keyPressEvent(event);
  }

  uint32_t newOffset;
  switch (event->key()) {
    case Qt::Key_Up:
      newOffset = selectedOffset - BYTES_PER_LINE;
      goto selectNewOffset;

    case Qt::Key_Down: {
      int selectedCol = (selectedOffset - vgmfile->dwOffset) % BYTES_PER_LINE;
      int endOffset = selectedItem->dwOffset - vgmfile->dwOffset + selectedItem->unLength;
      int itemEndCol = endOffset % BYTES_PER_LINE;
      int itemEndLine = endOffset / BYTES_PER_LINE;
      newOffset = vgmfile->dwOffset + ((itemEndLine + (selectedCol > itemEndCol ? 0 : 1)) * BYTES_PER_LINE) + selectedCol;
      goto selectNewOffset;
    }

    case Qt::Key_Left:
      newOffset = selectedItem->dwOffset - 1;
      goto selectNewOffset;

    case Qt::Key_Right:
      newOffset = selectedItem->dwOffset + selectedItem->unLength;
      goto selectNewOffset;

    selectNewOffset:
      if (newOffset >= vgmfile->dwOffset && newOffset < (vgmfile->dwOffset + vgmfile->unLength)) {
        selectedOffset = newOffset;
        auto item = vgmfile->GetItemFromOffset(newOffset, false);
        if (item) {
          selectionChanged(item);
        }
      }
      break;
    default:
      QWidget::keyPressEvent(event);  // Pass event up to base class, if not handled here
  }
}

bool HexView::handleOverlayPaintEvent(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::Paint) {
    auto overlay = qobject_cast<QWidget*>(obj);

    QPainter painter(static_cast<QWidget*>(obj));
    painter.fillRect(QRect(0, 0, BYTES_PER_LINE * 3 * charWidth, overlay->height()),
                     QColor(0, 0, 0, 100));

    painter.fillRect(QRect(((BYTES_PER_LINE * 3) + HEX_TO_ASCII_SPACING_CHARS) * charWidth + (charWidth / 2),
                           0,
                           BYTES_PER_LINE * charWidth, overlay->height()),
                           QColor(0, 0, 0, 100));

    return true;
  }
  return false;
}

void HexView::paintEvent(QPaintEvent *e) {
  QPainter painter(this);
  painter.setFont(this->font());

  QRect paintRect = e->rect();

  int startLine = paintRect.top() / lineHeight;
  int endLine = (paintRect.bottom() + lineHeight - 1) / lineHeight;

  painter.translate(0, startLine * lineHeight);
  for (int line = startLine; line <= endLine; line++) {
    printLine(painter, line);
    painter.translate(0, lineHeight);
  }
}

void HexView::printLine(QPainter& painter, int line) {
  painter.save();
  printAddress(painter, line);
  painter.translate((NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS) * charWidth, 0);
  auto startAddress = vgmfile->dwOffset + (line * BYTES_PER_LINE);
  auto endAddress = vgmfile->dwOffset + vgmfile->unLength;
  printData(painter, startAddress, endAddress);
  painter.restore();
}

void HexView::printAddress(QPainter& painter, int line) {
  auto fileOffset = vgmfile->dwOffset + (line * BYTES_PER_LINE);
  QString hexString = QString("%1").arg(fileOffset, 8, addressAsHex ? 16 : 10, QChar('0')).toUpper();
  painter.drawText(rect(), Qt::AlignLeft, hexString);
}

void HexView::printData(QPainter& painter, int startAddress, int endAddress) {
  if (endAddress > static_cast<int>(vgmfile->dwOffset + vgmfile->unLength) || (startAddress >= endAddress)) {
    return;
  }

  int startCol = static_cast<int>(startAddress - vgmfile->dwOffset) % BYTES_PER_LINE;
  auto bytesToPrint = min(BYTES_PER_LINE - startCol, endAddress - startAddress);

  auto defaultTextColor = painter.pen().color();
  QColor windowColor = this->palette().color(QPalette::Window);

  uint8_t data[16];
  vgmfile->GetBytes(startAddress, bytesToPrint, data);
  int emptyAddressBytes = 0;
  auto offset = 0;
  while (offset < bytesToPrint) {
    auto item = vgmfile->GetItemFromOffset(startAddress + offset, false);
    if (item) {
      if (emptyAddressBytes > 0) {
        int dataOffset = offset - emptyAddressBytes;
        int col = startCol + dataOffset;
        translateAndPrintHex(painter, data+dataOffset, col, emptyAddressBytes, windowColor, defaultTextColor);
        translateAndPrintAscii(painter, data + dataOffset, col, emptyAddressBytes, windowColor, defaultTextColor);
        emptyAddressBytes = 0;
      }
      QColor bgColor = colorForEventColor(item->color);
      QColor textColor = textColorForEventColor(item->color);
      int col = startCol + offset;

      // In case the event spans multiple lines, account for how far into the event we are at this line
      int offsetIntoEvent = std::max(0, startAddress - static_cast<int>(item->dwOffset));
      auto numEventBytesToPrint = min(static_cast<int>(item->unLength - offsetIntoEvent), bytesToPrint - offset);
      translateAndPrintHex(painter, data+offset, col, numEventBytesToPrint, bgColor, textColor);
      translateAndPrintAscii(painter, data + offset, col, numEventBytesToPrint, bgColor, textColor);

      offset += numEventBytesToPrint;
    } else {
      offset += 1;
      emptyAddressBytes += 1;
    }
  }
  if (emptyAddressBytes > 0) {
    int dataOffset = offset - emptyAddressBytes;
    int col = startCol + dataOffset;
    translateAndPrintHex(painter, data+dataOffset, col, emptyAddressBytes, windowColor, defaultTextColor);
    translateAndPrintAscii(painter, data + dataOffset, col, emptyAddressBytes, windowColor, defaultTextColor);
  }
}

void HexView::printHex(
    QPainter& painter,
    uint8_t* data,
    int length,
    QColor bgColor,
    QColor textColor
) {
  // Draw background color
  auto width = length * charWidth * 3;
  int rectWidth = width;
  int rectHeight = lineHeight;
  painter.fillRect(-charWidth/2, 0, rectWidth, rectHeight, bgColor);

  // Draw bytes string
  QString hexString;
  hexString.reserve(length * 3); // 2 characters for hex and 1 for space

  for (int i = 0; i < length; ++i) {
    hexString += QString("%1 ").arg(data[i], 2, 16, QLatin1Char('0')).toUpper();
  }

  painter.setPen(textColor);
  painter.drawText(rect(), Qt::AlignLeft, hexString);
  painter.translate(width, 0);
}

void HexView::translateAndPrintHex(
    QPainter& painter,
    uint8_t* data,
    int offset,
    int length,
    QColor bgColor,
    QColor textColor
) {
  painter.save();
  painter.translate(offset * 3 * charWidth, 0);
  printHex(painter, data, length, bgColor, textColor);
  painter.restore();
}

void HexView::translateAndPrintAscii(
    QPainter& painter,
    uint8_t* data,
    int offset,
    int length,
    QColor bgColor,
    QColor textColor
) {
  painter.save();
  painter.translate(((BYTES_PER_LINE * 3) + HEX_TO_ASCII_SPACING_CHARS + offset) * charWidth, 0);
  printAscii(painter, data, length, bgColor, textColor);
  painter.restore();
}

void HexView::printAscii(
    QPainter& painter,
    uint8_t* data,
    int length,
    QColor bgColor,
    QColor textColor
) {
  // Draw background color
  auto width = length * charWidth;
  painter.fillRect(0, 0, width, lineHeight, bgColor);

  // Draw ascii characters
  QString asciiString;
  asciiString.reserve(length);

  for (int i = 0; i < length; ++i) {
    asciiString.append((data[i] < 0x20 || data[i] > 0x7E) ? '.' : QChar(data[i]));
  }
  painter.setPen(textColor);
  painter.drawText(rect(), Qt::AlignLeft, asciiString);
}

void HexView::showOverlay(bool show, bool animate) {
  if (! animate) {
    if (overlayAnimation) {
      overlayAnimation->stop();
      delete overlayAnimation;
      overlayAnimation = nullptr;
    }
    if (show) {
      overlay->show();
    } else {
      overlay->hide();
    }
    return;
  }

  if (show) {
    if (
        (overlayAnimation == nullptr && !overlay->isHidden()) ||
        (overlayAnimation && (overlayAnimation->endValue() == 1.0))
     ) {
      return;
    }

    // If a current animation is running, stop it and delete it - we replace it with a new animation
    if (overlayAnimation && overlayAnimation->state() == QAbstractAnimation::Running) {
      overlayAnimation->stop();
      delete overlayAnimation;
      overlayAnimation = nullptr;
    }

    overlayAnimation = new QPropertyAnimation(overlayOpacityEffect, "opacity");
    overlayAnimation->setDuration(200);
    overlayAnimation->setStartValue(overlayOpacityEffect == nullptr ? 0 : overlayOpacityEffect->opacity());
    overlayAnimation->setEndValue(1.0);
    overlayAnimation->setEasingCurve(QEasingCurve::OutQuad);

    QObject::connect(overlayAnimation, &QPropertyAnimation::finished, [this]() {
      overlayAnimation = nullptr;
    });

    overlayAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    overlay->show();
  } else {
    // If a current animation is running, stop it and delete it - we replace it with a new animation
    if (overlayAnimation && overlayAnimation->state() == QAbstractAnimation::Running) {
      overlayAnimation->stop();
      delete overlayAnimation;
      overlayAnimation = nullptr;
    }

    overlayAnimation = new QPropertyAnimation(overlayOpacityEffect, "opacity");
    overlayAnimation->setDuration(200);
    overlayAnimation->setStartValue(overlayOpacityEffect == nullptr ? 1.0 : overlayOpacityEffect->opacity());
    overlayAnimation->setEndValue(0.0);

    // Connect the finished signal of the animation to a lambda function
    QObject::connect(overlayAnimation, &QPropertyAnimation::finished, [this]() {
      overlay->hide();
      overlayAnimation = nullptr;
    });

    overlayAnimation->start(QAbstractAnimation::DeleteWhenStopped);
  }
}

void HexView::drawSelectedItem() {

  // Set a limit for selected item size, lest we grind the system to a halt painting a giant view
  if (selectedItem->unLength > 0x1000) {
    return;
  }

  selectionView = new QWidget(this);

  int baseOffset = static_cast<int>(selectedItem->dwOffset - vgmfile->dwOffset);
  int startLine = baseOffset / BYTES_PER_LINE;
  int startColumn = baseOffset % BYTES_PER_LINE;
  int numLines = ((startColumn + static_cast<int>(selectedItem->unLength)) / BYTES_PER_LINE) + 1;

  int widgetX = ((NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS) * charWidth) - SELECTION_PADDING;
  int widgetY = (startLine * lineHeight) - SELECTION_PADDING;
  int widgetWidth = (((BYTES_PER_LINE * 3) + HEX_TO_ASCII_SPACING_CHARS + BYTES_PER_LINE) *
                     charWidth) + (SELECTION_PADDING * 2);
  int widgetHeight = (numLines * lineHeight) + (SELECTION_PADDING * 2);

  selectionView->setGeometry(QRect(widgetX, widgetY, widgetWidth, widgetHeight));

  selectionView->installEventFilter(
      new LambdaEventFilter([this, numLines, startColumn](QObject* obj, QEvent* event) -> bool {
        if (event->type() == QEvent::Paint) {
          uint8_t* data = new uint8_t[selectedItem->unLength];
          vgmfile->GetBytes(selectedItem->dwOffset, selectedItem->unLength, data);

          QColor bgColor = colorForEventColor(selectedItem->color);
          QColor textColor = textColorForEventColor(selectedItem->color);

          QPainter painter(static_cast<QWidget*>(obj));
          painter.setFont(this->font());
          painter.translate(SELECTION_PADDING, SELECTION_PADDING);

          int col = startColumn;
          int offsetIntoEvent = 0;
          for (int line=0; line<numLines; line++) {
            painter.save();
            painter.translate(0, line * lineHeight);

            // If the selected item is a container item, then we need to draw all of its sub items.
            if (selectedItem->IsContainerItem()) {
              int startAddress = selectedItem->dwOffset + offsetIntoEvent;
              int endAddress = selectedItem->dwOffset + selectedItem->unLength;
              printData(painter, startAddress, endAddress);
              offsetIntoEvent += BYTES_PER_LINE - col;
              col = 0;
            } else {
              int bytesToPrint = min(
                  min(static_cast<int>(selectedItem->unLength) - offsetIntoEvent, BYTES_PER_LINE - col),
                  BYTES_PER_LINE);
              translateAndPrintAscii(painter, data + offsetIntoEvent, col, bytesToPrint, bgColor, textColor);
              translateAndPrintHex(painter, data + offsetIntoEvent, col, bytesToPrint, bgColor, textColor);

              offsetIntoEvent += bytesToPrint;
              col = 0;
            }
            painter.restore();
          }
          delete[] data;
          return true;
        }
        return false;
      })
    );

  QGraphicsDropShadowEffect* glowEffect = new QGraphicsDropShadowEffect();
  glowEffect->setBlurRadius(20);
  glowEffect->setColor(Qt::black);
  glowEffect->setOffset(0, 0);
  selectionView->setGraphicsEffect(glowEffect);

  selectionView->show();
}

void HexView::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    QPoint pos = event->pos();

    auto halfCharWidth = charWidth / 2;
    auto hexStart = (NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS) * charWidth;
    auto hexEnd = hexStart + (BYTES_PER_LINE * 3 * charWidth);

    auto asciiStart = hexEnd + (HEX_TO_ASCII_SPACING_CHARS * charWidth);
    auto asciiEnd = asciiStart + (BYTES_PER_LINE * charWidth);

    int byteNum = -1;
    if (pos.x() >= hexStart - halfCharWidth && pos.x() < hexEnd - halfCharWidth) {
      byteNum = ((pos.x() - hexStart + halfCharWidth) / charWidth) / 3;
    } else if (pos.x() >= asciiStart && pos.x() < asciiEnd) {
      byteNum = (pos.x() - asciiStart) / charWidth;
    }
    if (byteNum == -1) {
      selectionChanged(nullptr);
      return;
    }
    int line = pos.y() / lineHeight;
    selectedOffset = vgmfile->dwOffset + (line * BYTES_PER_LINE) + byteNum;
    auto item = vgmfile->GetItemFromOffset(selectedOffset, false);
    if (item == selectedItem) {
      selectionChanged(nullptr);
    } else {
      selectionChanged(item);
    }
  }

  QWidget::mousePressEvent(event);
}

void HexView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    QPoint pos = event->pos();
    if (pos.x() >= 0 && pos.x() < (NUM_ADDRESS_NIBBLES * charWidth)) {
      addressAsHex = !addressAsHex;
      update();
    }
  }

  QWidget::mouseDoubleClickEvent(event);
}