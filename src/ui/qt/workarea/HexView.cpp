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
#include "SeqTrack.h"

#include <QFontDatabase>
#include <QPainter>
#include <QApplication>
#include <QPaintEvent>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolTip>
#include <QStyle>

constexpr qsizetype NUM_CACHED_LINE_PIXMAPS = 600;
constexpr int BYTES_PER_LINE = 16;
constexpr int NUM_ADDRESS_NIBBLES = 8;
constexpr int ADDRESS_SPACING_CHARS = 4;
constexpr int HEX_TO_ASCII_SPACING_CHARS = 4;
constexpr int SELECTION_PADDING = 20;
constexpr int VIEWPORT_PADDING = 10;
constexpr int DIM_DURATION_MS = 200;
constexpr qreal OVERLAY_OPACITY_LEVEL = 0.8;
const QColor SHADOW_COLOR = Qt::black;
constexpr int SHADOW_OFFSET_X = 1;
constexpr int SHADOW_OFFSET_Y = 2;
constexpr int SHADOW_BLUR_RADIUS = 20;

HexView::HexView(VGMFile* vgmfile, QWidget *parent) :
      QWidget(parent), vgmfile(vgmfile), selectedItem(nullptr) {

  lineCache.setMaxCost(NUM_CACHED_LINE_PIXMAPS);

  double appFontPointSize = QApplication::font().pointSizeF();
  QFont font("Roboto Mono", appFontPointSize + 1);
  // Call setPointSizeF, as QFont() doesn't accept a float size value
  font.setPointSizeF(appFontPointSize + 1.0);

  this->setFont(font);
  this->setFocusPolicy(Qt::StrongFocus);

  overlay = new QWidget(this);
  overlay->setAttribute(Qt::WA_NoSystemBackground);
  overlay->setAttribute(Qt::WA_TranslucentBackground);
  overlay->hide();

  overlay->installEventFilter(
      new LambdaEventFilter([this](QObject* obj, const QEvent* event) -> bool {
        if (event->type() == QEvent::Paint) {
          return this->handleOverlayPaintEvent(obj, event);
        }
        return false;
      })
  );

  overlayOpacityEffect = new QGraphicsOpacityEffect(this);
  overlay->setGraphicsEffect(overlayOpacityEffect);

  selectionView = new QWidget(this);
  selectionView->installEventFilter(
      new LambdaEventFilter([this](QObject* obj, QEvent* event) -> bool {
        if (event->type() == QEvent::Paint) {
          return handleSelectedItemPaintEvent(obj, event);
        }
        return false;
      })
  );
}

void HexView::setFont(QFont& font) {
  QFontMetricsF fontMetrics(font);

  // We need both charWidth and the actual font character width to be a whole number, otherwise,
  // we run into a host of drawing problems. (alternatively we could draw one byte at a time in
  // printHex). Qt isn't very flexible about fractional font sizing, but it is flexible about
  // letter spacing. So here we find the letter spacing which results in the closest whole number
  // character width. We could probably achieve the same with word spacing instead, since we
  // render hex with actual ASCII spaces between bytes, but the visual difference is minimal.
  auto originalCharWidth = fontMetrics.horizontalAdvance("A");
  auto originalLetterSpacing = font.letterSpacing();
  auto charWidthSansSpacing = originalCharWidth - originalLetterSpacing;
  auto targetLetterSpacing = std::round(charWidthSansSpacing) - charWidthSansSpacing;
  font.setLetterSpacing(QFont::SpacingType::AbsoluteSpacing, targetLetterSpacing);

  fontMetrics = QFontMetrics(font);
  // We need to use horizontalAdvance(), as averageCharWidth() doesn't capture letter spacing.
  this->charWidth = static_cast<int>(std::round(fontMetrics.horizontalAdvance("A")));
  this->charHalfWidth = static_cast<int>(std::round(fontMetrics.horizontalAdvance("A") / 2));
  this->lineHeight = static_cast<int>(std::round(fontMetrics.height()));

  QWidget::setFont(font);
  this->setMinimumWidth(getVirtualFullWidth());
  this->setFixedHeight(getVirtualHeight());

  // Force everything to redraw
  prevSelectedItem = nullptr;
  m_virtual_full_width = -1;
  m_virtual_width_sans_ascii = -1;
  m_virtual_width_sans_ascii_and_address = -1;
  lineCache.clear();
  redrawOverlay();
  drawSelectedItem();
}

// The x offset to print hexadecimal
int HexView::hexXOffset() const {
  if (shouldDrawOffset)
    return ((NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS) * charWidth);
  else
    return 0;
}

int HexView::getVirtualHeight() const {
  return lineHeight * getTotalLines();
}

int HexView::getVirtualFullWidth() {
  if (m_virtual_full_width == -1) {
    constexpr int numChars = NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS + (BYTES_PER_LINE * 3) +
                             HEX_TO_ASCII_SPACING_CHARS + BYTES_PER_LINE;
    m_virtual_full_width = (numChars * charWidth) + SELECTION_PADDING;
  }
  return m_virtual_full_width;
}

int HexView::getVirtualWidthSansAscii() {
  if (m_virtual_width_sans_ascii == -1) {
    constexpr int numChars = NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS + (BYTES_PER_LINE * 3);
    m_virtual_width_sans_ascii = (numChars * charWidth) + SELECTION_PADDING;
  }
  return m_virtual_width_sans_ascii;
}

int HexView::getVirtualWidthSansAsciiAndAddress() {
  if (m_virtual_width_sans_ascii_and_address == -1) {
    constexpr int numChars = BYTES_PER_LINE * 3;
    m_virtual_width_sans_ascii_and_address = (numChars * charWidth) + SELECTION_PADDING;
  }
  return m_virtual_width_sans_ascii_and_address;
}

int HexView::getActualVirtualWidth() {
  if (shouldDrawAscii) {
    return getVirtualFullWidth();
  }
  if (shouldDrawOffset) {
    return getVirtualWidthSansAscii();
  }
  return getVirtualWidthSansAsciiAndAddress();
}

int HexView::getViewportFullWidth() {
  return getVirtualFullWidth() + VIEWPORT_PADDING;
}

int HexView::getViewportWidthSansAscii() {
  return getVirtualWidthSansAscii() + VIEWPORT_PADDING;
}

int HexView::getViewportWidthSansAsciiAndAddress() {
  return getVirtualWidthSansAsciiAndAddress() + VIEWPORT_PADDING;
}

int HexView::getTotalLines() const {
  return (vgmfile->unLength + BYTES_PER_LINE - 1) / BYTES_PER_LINE;
}

void HexView::setSelectedItem(VGMItem *item) {
  selectedItem = item;

  if (selectedItem == nullptr) {
    showOverlay(false, true);
    selectionView->hide();
    prevSelectedItem = nullptr;
    return;
  }
  showOverlay(true, true);
  drawSelectedItem();
  selectionView->show();

  // Handle scrolling to the selected event
  QScrollArea* scrollArea = getContainingScrollArea(this);
  if (!scrollArea) return;

  auto itemBaseOffset = static_cast<int>(item->dwOffset - vgmfile->dwOffset);
  int line = itemBaseOffset / BYTES_PER_LINE;
  int endLine = static_cast<int>(item->dwOffset + item->unLength - vgmfile->dwOffset) / BYTES_PER_LINE;

  int viewPortStartLine = scrollArea->verticalScrollBar()->value() / lineHeight;
  int viewPortEndLine = viewPortStartLine + ((scrollArea->viewport()->height()) / lineHeight);

  // If the item is already visible, don't scroll.
  if (line <= viewPortEndLine && endLine > viewPortStartLine) {
    return;
  }

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

void HexView::resizeOverlays(int height) const {
  overlay->setGeometry(
      hexXOffset() - charHalfWidth,
      overlay->y(),
      ((BYTES_PER_LINE * 3 + HEX_TO_ASCII_SPACING_CHARS + BYTES_PER_LINE) * charWidth) + charHalfWidth,
      height
  );
}

void HexView::redrawOverlay() {
  if (QScrollArea* scrollArea = getContainingScrollArea(this)) {
    resizeOverlays(scrollArea->height());
  }
}

bool HexView::event(QEvent *e) {
  if (e->type() == QEvent::ToolTip) {
    auto *helpevent = dynamic_cast<QHelpEvent *>(e);

    int offset = getOffsetFromPoint(helpevent->pos());
    if (offset < 0) {
      return true;
    }

    if (VGMItem* item = vgmfile->getItemAtOffset(offset, false)) {
      auto description = getFullDescriptionForTooltip(item);
      if (!description.isEmpty()) {
        QToolTip::showText(helpevent->globalPos(), description, this);
      }
    }
    return true;
  }

  return QWidget::event(e);
}

void HexView::changeEvent(QEvent *event) {
  if (event->type() == QEvent::ParentChange) {
    QScrollArea* scrollArea = getContainingScrollArea(this);
    if (!scrollArea) return;

    scrollArea->installEventFilter(
      new LambdaEventFilter([this]([[maybe_unused]] QObject* obj, const QEvent* event) -> bool {
          if (event->type() == QEvent::Resize) {
            QScrollArea* scrollArea = getContainingScrollArea(this);

            int scrollAreaWidth = scrollArea->width();
            int scrollAreaHeight = scrollArea->height();

            if (scrollAreaHeight > prevHeight) {
              redrawOverlay();
            }
            prevHeight = scrollAreaHeight;

            if (scrollAreaWidth == prevWidth ||
                (scrollAreaWidth > getViewportWidthSansAsciiAndAddress() &&
                scrollAreaWidth != getViewportWidthSansAscii() &&
                scrollAreaWidth != getViewportFullWidth())) {

              return false;
            }
            prevWidth = scrollAreaWidth;

            bool prevShowOffset = shouldDrawOffset;
            bool prevShouldDrawAscii = shouldDrawAscii;
            shouldDrawOffset = scrollAreaWidth > getViewportWidthSansAsciiAndAddress();
            shouldDrawAscii = scrollAreaWidth > getViewportWidthSansAscii();

            if (prevShowOffset != shouldDrawOffset || prevShouldDrawAscii != shouldDrawAscii) {
              prevSelectedItem = nullptr;
              lineCache.clear();
              drawSelectedItem();
              redrawOverlay();
            }

            // For optimization, we hide/show the selection view on scroll based on whether it's in viewport, but
            // scroll events don't trigger on resize, and the user could expand the viewport so that it's in view
            if (selectedItem && selectionView) {
              selectionView->show();
            }

            return false;
          }
          return false;
        })
    );

    connect(scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, [this, scrollArea](int value) {
      overlay->move(overlay->x(), value);
      if (selectedItem != nullptr) {
        int startLine = value / lineHeight;
        int endLine = (value + scrollArea->height()) / lineHeight;

        int selectedItemStartLine = ((selectedItem->dwOffset - vgmfile->dwOffset) / BYTES_PER_LINE) - 1;
        int selectedItemEndLine = (((selectedItem->dwOffset - vgmfile->dwOffset) + selectedItem->unLength) / BYTES_PER_LINE) + 1;
        bool selectionVisible = ((startLine <= selectedItemEndLine) && (selectedItemStartLine <= endLine));
        selectionVisible ? selectionView->show() : selectionView->hide();
      }
    });
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

    case Qt::Key_Escape:
      selectionChanged(nullptr);
      break;

    selectNewOffset:
      if (newOffset >= vgmfile->dwOffset && newOffset < (vgmfile->dwOffset + vgmfile->unLength)) {
        selectedOffset = newOffset;
        if (auto item = vgmfile->getItemAtOffset(newOffset, false)) {
          selectionChanged(item);
        }
      }
      break;
    default:
      QWidget::keyPressEvent(event);  // Pass event up to base class, if not handled here
  }
}

bool HexView::handleOverlayPaintEvent(QObject* obj, const QEvent* event) const {
  auto overlay = qobject_cast<QWidget*>(obj);

  QPainter painter(static_cast<QWidget*>(obj));
  painter.fillRect(QRect(0, 0, BYTES_PER_LINE * 3 * charWidth, overlay->height()),
                   QColor(0, 0, 0, 100));

  if (shouldDrawAscii) {
    painter.fillRect(
        QRect(((BYTES_PER_LINE * 3) + HEX_TO_ASCII_SPACING_CHARS) * charWidth + charHalfWidth,
              0,
              BYTES_PER_LINE * charWidth, overlay->height()),
        QColor(0, 0, 0, 100));
  }

  return true;
}

bool HexView::handleSelectedItemPaintEvent(QObject* obj, QEvent* event) {
  if (!selectedItem) {
    return true;
  }
  auto widget = static_cast<QWidget*>(obj);
  if (prevSelectedItem != selectedItem) {

    prevSelectedItem = selectedItem;

    qreal dpr = devicePixelRatioF();
    auto widgetSize = widget->size();
    auto pixmap = QPixmap(widgetSize.width() * dpr, widgetSize.height() * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter pixmapPainter = QPainter(&pixmap);

    int baseOffset = static_cast<int>(selectedItem->dwOffset - vgmfile->dwOffset);
    int startColumn = baseOffset % BYTES_PER_LINE;
    int numLines = ((startColumn + static_cast<int>(selectedItem->unLength)) / BYTES_PER_LINE) + 1;

    auto itemData = std::vector<uint8_t>(selectedItem->unLength);
    vgmfile->readBytes(selectedItem->dwOffset, selectedItem->unLength, itemData.data());

    QColor bgColor = colorForEventColor(selectedItem->color);
    QColor textColor = textColorForEventColor(selectedItem->color);

    pixmapPainter.setFont(this->font());
    pixmapPainter.translate(SELECTION_PADDING, SELECTION_PADDING);

    int startLine = baseOffset / BYTES_PER_LINE;

    int col = startColumn;
    int offsetIntoEvent = 0;
    for (int line=0; line<numLines; line++) {
      pixmapPainter.save();
      pixmapPainter.translate(0, line * lineHeight);

      int bytesToPrint = std::min(
                    std::min(static_cast<int>(selectedItem->unLength) - offsetIntoEvent,BYTES_PER_LINE - col),
                    BYTES_PER_LINE);

      // If there is a cached pixmap of the line, copy portions of it to construct the selected item pixmap
      if (auto linePixmap = lineCache.object(startLine + line)) {
        // Hex segment
        auto [hex_rect, ascii_rect] = calculateSelectionRectsForLine(col, bytesToPrint, dpr);
        QRect targetRect((col * 3 * charWidth) - charHalfWidth, 0,
          bytesToPrint * 3 * charWidth, lineHeight);
        pixmapPainter.drawPixmap(targetRect, *linePixmap, hex_rect);

        // ASCII segment
        if (shouldDrawAscii) {
          int ascii_start_in_chars = (BYTES_PER_LINE * 3) + HEX_TO_ASCII_SPACING_CHARS;
          targetRect = { (ascii_start_in_chars + col) * charWidth, 0,
            bytesToPrint * charWidth, lineHeight };
          pixmapPainter.drawPixmap(targetRect, *linePixmap, ascii_rect);
        }
        offsetIntoEvent += bytesToPrint;
        col = 0;
      }
      else {
        // If the selected item has children, draw them.
        if (!selectedItem->children().empty()) {
          int startAddress = static_cast<int>(selectedItem->dwOffset + offsetIntoEvent);
          int endAddress = static_cast<int>(selectedItem->dwOffset + selectedItem->unLength);
          printData(pixmapPainter, startAddress, endAddress);
          offsetIntoEvent += BYTES_PER_LINE - col;
          col = 0;
        } else {
          translateAndPrintAscii(pixmapPainter, itemData.data() + offsetIntoEvent, col, bytesToPrint, bgColor, textColor);
          translateAndPrintHex(pixmapPainter, itemData.data() + offsetIntoEvent, col, bytesToPrint, bgColor, textColor);

            offsetIntoEvent += bytesToPrint;
            col = 0;
          }
        }
      }
      pixmapPainter.restore();
    }
    auto glowEffect = new QGraphicsDropShadowEffect();
    glowEffect->setBlurRadius(SELECTION_PADDING);
    glowEffect->setColor(SHADOW_COLOR);
    glowEffect->setOffset(SHADOW_OFFSET_X, SHADOW_OFFSET_Y);
    glowEffect->setBlurRadius(SHADOW_BLUR_RADIUS);

    selectionViewPixmap = QPixmap(pixmap.width(), pixmap.height());
    selectionViewPixmap.setDevicePixelRatio(dpr);
    selectionViewPixmap.fill(Qt::transparent);
    applyEffectToPixmap(pixmap, selectionViewPixmap, glowEffect, 0);
  }
  QPainter painter(widget);
  painter.drawPixmap(0, 0, selectionViewPixmap);
  return true;
}

std::pair<QRect,QRect> HexView::calculateSelectionRectsForLine(int startColumn, int length, qreal dpr) const {
  int hexCharsStartOffsetInChars = shouldDrawOffset ? NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS : 0;
  int asciiStartOffsetInChars = hexCharsStartOffsetInChars + (BYTES_PER_LINE * 3) + HEX_TO_ASCII_SPACING_CHARS;
  int left = (hexCharsStartOffsetInChars + (startColumn * 3)) * charWidth - charHalfWidth;
  int width = length * 3 * charWidth;
  QRect hexRect = QRect(left * dpr, 0, width * dpr, lineHeight * dpr);

  left = (asciiStartOffsetInChars + startColumn) * charWidth;
  width = length * charWidth;
  QRect asciiRect = QRect(left * dpr, 0, width * dpr, lineHeight * dpr);

  return { hexRect, asciiRect };
}

QRect HexView::calculateSelectionRectForLine(int startColumn, int length) {
  qreal dpr = devicePixelRatioF();

  int hexCharsStartOffsetInChars = shouldDrawOffset ? NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS : 0;
  int left = (hexCharsStartOffsetInChars + (startColumn * 3)) * charWidth;
  // left = 0;
  left -= charWidth / 2;
  // int right = left + (length * 3 * charWidth);
  int width = length * 3 * charWidth;
  return QRect(left * dpr, 0, width * dpr, lineHeight * dpr);
}

void HexView::paintEvent(QPaintEvent *e) {
  QPainter painter(this);
  painter.setFont(this->font());

  QRect paintRect = e->rect();

#ifdef Q_OS_MAC
  // On MacOS, the scrollbar appears only upon scrolling and extra paint events are sent to draw it.
  // We want to ignore these paint events if they don't overlap with the drawn HexView area.
  int scrollBarThickness = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
  if (paintRect.left() >= getActualVirtualWidth() - VIEWPORT_PADDING - scrollBarThickness + 2) {
    return;
  }
#endif

  int startLine = paintRect.top() / lineHeight;
  int endLine = (paintRect.bottom() + lineHeight - 1) / lineHeight;

  qreal dpr = devicePixelRatioF();

  painter.translate(0, startLine * lineHeight);
  for (int line = startLine; line <= endLine; line++) {
    // Skip drawing lines that are totally eclipsed by the selected item
    if (selectedItem && selectedItem->unLength >= BYTES_PER_LINE) {
      auto startAddress = vgmfile->dwOffset + (line * BYTES_PER_LINE);
      auto endAddress = startAddress + BYTES_PER_LINE;
      if (selectedItem->dwOffset <= startAddress && selectedItem->dwOffset + selectedItem->unLength >= endAddress) {
        if (shouldDrawOffset)
          printAddress(painter, line);
        painter.translate(0, lineHeight);
        continue;
      }
    }

    auto cachedPixmap = lineCache.object(line);
    if (!cachedPixmap) {
      auto linePixmap = new QPixmap(static_cast<int>(getVirtualFullWidth() * dpr), static_cast<int>(lineHeight * dpr));
      linePixmap->setDevicePixelRatio(dpr);
      linePixmap->fill(Qt::transparent);

      QPainter linePainter(linePixmap);
      linePainter.setFont(this->font());
      linePainter.setRenderHints(painter.renderHints());
      linePainter.setPen(painter.pen());
      linePainter.setBrush(painter.brush());

      printLine(linePainter, line);
      lineCache.insert(line, linePixmap);
      painter.drawPixmap(0, 0, *linePixmap);
    } else {
      painter.drawPixmap(0, 0, *cachedPixmap);
    }
    painter.translate(0, lineHeight);
  }
}

void HexView::printLine(QPainter& painter, int line) const {
  painter.save();
  if (shouldDrawOffset) {
    printAddress(painter, line);
  }
  painter.translate(hexXOffset(), 0);

  auto startAddress = vgmfile->dwOffset + (line * BYTES_PER_LINE);
  auto endAddress = vgmfile->dwOffset + vgmfile->unLength;
  printData(painter, startAddress, endAddress);
  painter.restore();
}

void HexView::printAddress(QPainter& painter, int line) const {
  auto fileOffset = vgmfile->dwOffset + (line * BYTES_PER_LINE);
  QString hexString = QString("%1").arg(fileOffset, 8, addressAsHex ? 16 : 10, QChar('0')).toUpper();
  painter.drawText(rect(), Qt::AlignLeft, hexString);
}

void HexView::printData(QPainter& painter, int startAddress, int endAddress) const {
  if (endAddress > static_cast<int>(vgmfile->dwOffset + vgmfile->unLength) || (startAddress >= endAddress)) {
    return;
  }

  int startCol = static_cast<int>(startAddress - vgmfile->dwOffset) % BYTES_PER_LINE;
  auto bytesToPrint = std::min(BYTES_PER_LINE - startCol, endAddress - startAddress);

  auto defaultTextColor = painter.pen().color();
  QColor windowColor = this->palette().color(QPalette::Window);

  uint8_t data[16];
  vgmfile->readBytes(startAddress, bytesToPrint, data);
  int emptyAddressBytes = 0;
  auto offset = 0;
  while (offset < bytesToPrint) {
    if (auto item = vgmfile->getItemAtOffset(startAddress + offset, false)) {
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
      auto numEventBytesToPrint = std::min(static_cast<int>(item->unLength - offsetIntoEvent), bytesToPrint - offset);
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
    const uint8_t* data,
    int length,
    QColor bgColor,
    QColor textColor
) const {
  // Draw background color
  auto width = length * charWidth * 3;
  int rectWidth = width;
  int rectHeight = lineHeight;
  painter.fillRect(-charHalfWidth, 0, rectWidth, rectHeight, bgColor);

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
    const uint8_t* data,
    int offset,
    int length,
    QColor bgColor,
    QColor textColor
) const {
  painter.save();
  painter.translate(offset * 3 * charWidth, 0);
  printHex(painter, data, length, bgColor, textColor);
  painter.restore();
}

void HexView::translateAndPrintAscii(
    QPainter& painter,
    const uint8_t* data,
    int offset,
    int length,
    QColor bgColor,
    QColor textColor
) const {
  if (!shouldDrawAscii)
    return;
  painter.save();
  painter.translate(((BYTES_PER_LINE * 3) + HEX_TO_ASCII_SPACING_CHARS + offset) * charWidth, 0);
  printAscii(painter, data, length, bgColor, textColor);
  painter.restore();
}

void HexView::printAscii(
    QPainter& painter,
    const uint8_t* data,
    int length,
    QColor bgColor,
    QColor textColor
) const {
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
    overlayAnimation->setDuration(DIM_DURATION_MS);
    overlayAnimation->setStartValue(overlayOpacityEffect == nullptr ? 0 : overlayOpacityEffect->opacity());
    overlayAnimation->setEndValue(OVERLAY_OPACITY_LEVEL);
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
    overlayAnimation->setDuration(DIM_DURATION_MS);
    overlayAnimation->setStartValue(overlayOpacityEffect == nullptr ? OVERLAY_OPACITY_LEVEL : overlayOpacityEffect->opacity());
    overlayAnimation->setEndValue(0.0);

    // Connect the finished signal of the animation to a lambda function
    QObject::connect(overlayAnimation, &QPropertyAnimation::finished, [this]() {
      overlay->hide();
      overlayAnimation = nullptr;
    });

    overlayAnimation->start(QAbstractAnimation::DeleteWhenStopped);
  }
}

void HexView::drawSelectedItem() const {
  if (!selectionView) {
    return;
  }
  // Set a limit for selected item size, as it can halt the system to draw huge items
  if (!selectedItem || selectedItem->unLength > 0x3000) {
    selectionView->setGeometry(QRect(0, 0, 0, 0));
    return;
  }

  int baseOffset = static_cast<int>(selectedItem->dwOffset - vgmfile->dwOffset);
  int startLine = baseOffset / BYTES_PER_LINE;
  int startColumn = baseOffset % BYTES_PER_LINE;
  int numLines = ((startColumn + static_cast<int>(selectedItem->unLength)) / BYTES_PER_LINE) + 1;

  int widgetX = hexXOffset() - SELECTION_PADDING;
  int widgetY = (startLine * lineHeight) - SELECTION_PADDING;
  int widgetWidth = (((BYTES_PER_LINE * 3) + HEX_TO_ASCII_SPACING_CHARS + BYTES_PER_LINE) *
                     charWidth) + (SELECTION_PADDING * 2);
  int widgetHeight = (numLines * lineHeight) + (SELECTION_PADDING * 2);

  selectionView->setGeometry(QRect(widgetX, widgetY, widgetWidth, widgetHeight));
  selectionView->update();
}

// Find the VGMFile offset represented at the given QPoint. Returns -1 for invalid points.
int HexView::getOffsetFromPoint(QPoint pos) const {
  auto hexStart = hexXOffset();
  auto hexEnd = hexStart + (BYTES_PER_LINE * 3 * charWidth);

  auto asciiStart = hexEnd + (HEX_TO_ASCII_SPACING_CHARS * charWidth);
  auto asciiEnd = asciiStart + (BYTES_PER_LINE * charWidth);

  int byteNum = -1;
  if (pos.x() >= hexStart - charHalfWidth && pos.x() < hexEnd - charHalfWidth) {
    byteNum = ((pos.x() - hexStart + charHalfWidth) / charWidth) / 3;
  } else if (pos.x() >= asciiStart && pos.x() < asciiEnd) {
    byteNum = (pos.x() - asciiStart) / charWidth;
  }
  if (byteNum == -1) {
    return -1;
  }
  int line = pos.y() / lineHeight;
  return vgmfile->dwOffset + (line * BYTES_PER_LINE) + byteNum;
}

void HexView::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    QPoint pos = event->pos();

    int offset = getOffsetFromPoint(pos);
    if (offset == -1) {
      selectionChanged(nullptr);
      return;
    }

    this->selectedOffset = offset;
    auto item = vgmfile->getItemAtOffset(offset, false);
    if (item == selectedItem) {
      selectionChanged(nullptr);
    } else {
      selectionChanged(item);
    }
    isDragging = true;
  }

  QWidget::mousePressEvent(event);
}

void HexView::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    isDragging = false;
  }
  QWidget::mouseReleaseEvent(event);
}

void HexView::mouseMoveEvent(QMouseEvent *event) {
  if (isDragging && event->buttons() & Qt::LeftButton) {

    QPoint pos = event->pos();
    int offset = getOffsetFromPoint(pos);
    if (offset == -1) {
      selectionChanged(nullptr);
      return;
    }
    this->selectedOffset = offset;
    // If the new offset overlaps with the currently selected item, do nothing
    if (selectedItem && (selectedOffset >= selectedItem->dwOffset) &&
        (selectedOffset < (selectedItem->dwOffset + selectedItem->unLength))) {
      return;
    }
    auto item = vgmfile->getItemAtOffset(offset, false);
    if (item != selectedItem) {
      selectionChanged(item);
    }
  }
  QWidget::mouseMoveEvent(event);
}

void HexView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    QPoint pos = event->pos();
    if (pos.x() >= 0 && pos.x() < (NUM_ADDRESS_NIBBLES * charWidth)) {
      addressAsHex = !addressAsHex;
      lineCache.clear();
      update();
    }
  }

  QWidget::mouseDoubleClickEvent(event);
}