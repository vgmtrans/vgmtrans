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
#include <QParallelAnimationGroup>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolTip>
#include <QVarLengthArray>
#include <QStyle>
#include <array>
#include <algorithm>

constexpr qsizetype NUM_CACHED_LINE_PIXMAPS = 600;
constexpr int BYTES_PER_LINE = 16;
constexpr int NUM_ADDRESS_NIBBLES = 8;
constexpr int ADDRESS_SPACING_CHARS = 4;
constexpr int HEX_TO_ASCII_SPACING_CHARS = 4;
constexpr int SELECTION_PADDING = 18;
constexpr int VIEWPORT_PADDING = 10;
constexpr int DIM_DURATION_MS = 200;
constexpr int OVERLAY_ALPHA = 80;
const QColor SHADOW_COLOR = Qt::black;
constexpr int SHADOW_OFFSET_X = 1;
constexpr int SHADOW_OFFSET_Y = 4;
constexpr int SHADOW_BLUR_RADIUS = SELECTION_PADDING * 2;
constexpr int OVERLAY_HEIGHT_IN_SCREENS = 5;

namespace {
const std::array<QChar, 256 * 3> HEX_LOOKUP_TABLE = []() {
    constexpr char hexDigits[] = "0123456789ABCDEF";
    
    std::array<QChar, 256 * 3> data{};
    for (int i = 0; i < 256; ++i) {
        const int index = i * 3;
        data[index]     = QChar(hexDigits[(i >> 4) & 0x0F]);
        data[index + 1] = QChar(hexDigits[i & 0x0F]);
        data[index + 2] = QChar(u' ');
    }
    return data;
}();

static constexpr std::array<QChar, 16> HEX_NIBBLE_TABLE =
  {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
}  // namespace

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
      new LambdaEventFilter([this](QObject* obj, QEvent* event) -> bool {
        if (event->type() == QEvent::Paint) {
          return this->handleOverlayPaintEvent(obj, static_cast<QPaintEvent*>(event));
        }
        return false;
      },
      overlay)
  );

  overlayOpacityEffect = new QGraphicsOpacityEffect(this);
  overlay->setGraphicsEffect(overlayOpacityEffect);

  selectedItemShadowEffect = new QGraphicsDropShadowEffect();
  selectedItemShadowEffect->setBlurRadius(SHADOW_BLUR_RADIUS);
  selectedItemShadowEffect->setColor(SHADOW_COLOR);
  selectedItemShadowEffect->setOffset(SHADOW_OFFSET_X, SHADOW_OFFSET_Y);

  selectionView = new QWidget(this);
  selectionView->setGraphicsEffect(selectedItemShadowEffect);

  selectionView->installEventFilter(
      new LambdaEventFilter([this](QObject* obj, QEvent* event) -> bool {
        if (event->type() == QEvent::Paint) {
          return handleSelectedItemPaintEvent(obj, static_cast<QPaintEvent*>(event));
        }
        return false;
      },
      selectionView)
  );

  initAnimations();
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
  this->lineHeight = static_cast<int>(std::round(fontMetrics.height()));
  this->ascent = static_cast<int>(std::round(fontMetrics.ascent()));

  QWidget::setFont(font);
  updateSize();

  // Force everything to redraw
  selectionViewPixmap = QPixmap();
  selectionViewPixmapWithShadow = QPixmap();
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

void HexView::updateSize() {
  const QScrollArea* scrollArea = getContainingScrollArea(this);
  const int scrollBarThickness = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
  const int scrollAreaWidth = scrollArea ? scrollArea->width() : getVirtualFullWidth();
  // It is important that the width is not greater than its container. If it is greater, scrolling
  // will invalidate the entire viewport rather than just the area scrolled into view
  const int hexViewWidth = scrollAreaWidth - scrollBarThickness - 2;
  setFixedSize({hexViewWidth, getVirtualHeight()});
}

int HexView::getTotalLines() const {
  return (vgmfile->length() + BYTES_PER_LINE - 1) / BYTES_PER_LINE;
}

void HexView::setSelectedItem(VGMItem *item) {
  selectedItem = item;

  if (selectedItem == nullptr) {
    showSelectedItem(false, true);
    return;
  }
  // Invalidate the cached selected item pixmaps
  selectionViewPixmap = QPixmap();
  selectionViewPixmapWithShadow = QPixmap();

  showSelectedItem(true, true);
  drawSelectedItem();
  selectionView->show();

  // Handle scrolling to the selected event
  QScrollArea* scrollArea = getContainingScrollArea(this);
  if (!scrollArea) return;

  auto itemBaseOffset = static_cast<int>(item->offset() - vgmfile->offset());
  int line = itemBaseOffset / BYTES_PER_LINE;
  int endLine = static_cast<int>(item->offset() + item->length() - vgmfile->offset()) / BYTES_PER_LINE;

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

void HexView::resizeOverlays(int y, int viewportHeight) const {
  const int overlayHeight = viewportHeight * OVERLAY_HEIGHT_IN_SCREENS;
  overlay->setGeometry(
      hexXOffset() - charHalfWidth,
      std::max(0, y - ((overlayHeight - viewportHeight) / 2)),
      width(),
      overlayHeight
  );
}

void HexView::redrawOverlay() {
  if (QScrollArea* scrollArea = getContainingScrollArea(this)) {
    int y = scrollArea->verticalScrollBar()->value();
    resizeOverlays(y, scrollArea->height());
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

            updateSize();

            if (scrollAreaHeight * (OVERLAY_HEIGHT_IN_SCREENS - 1) > overlay->height()) {
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
              selectionViewPixmap = QPixmap();
              selectionViewPixmapWithShadow = QPixmap();
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
        },
        scrollArea)
    );

    connect(scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, [this, scrollArea](int value) {
      // If the overlay has moved out of frame, center it back into frame
      if (value < overlay->geometry().top() || value + scrollArea->height() > overlay->geometry().bottom()) {
        overlay->move(
          overlay->x(),
          std::max(0, value - ((overlay->height() - scrollArea->height()) / 2))
        );
      }

      if (selectedItem != nullptr) {
        int startLine = value / lineHeight;
        int endLine = (value + scrollArea->height()) / lineHeight;

        int selectedItemStartLine = ((selectedItem->offset() - vgmfile->offset()) / BYTES_PER_LINE) - 1;
        int selectedItemEndLine = (((selectedItem->offset() - vgmfile->offset()) + selectedItem->length()) / BYTES_PER_LINE) + 1;
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
      int selectedCol = (selectedOffset - vgmfile->offset()) % BYTES_PER_LINE;
      int endOffset = selectedItem->offset() - vgmfile->offset() + selectedItem->length();
      int itemEndCol = endOffset % BYTES_PER_LINE;
      int itemEndLine = endOffset / BYTES_PER_LINE;
      newOffset = vgmfile->offset() + ((itemEndLine + (selectedCol > itemEndCol ? 0 : 1)) * BYTES_PER_LINE) + selectedCol;
      goto selectNewOffset;
    }

    case Qt::Key_Left:
      newOffset = selectedItem->offset() - 1;
      goto selectNewOffset;

    case Qt::Key_Right:
      newOffset = selectedItem->offset() + selectedItem->length();
      goto selectNewOffset;

    case Qt::Key_Escape:
      selectionChanged(nullptr);
      break;

    selectNewOffset:
      if (newOffset >= vgmfile->offset() && newOffset < (vgmfile->offset() + vgmfile->length())) {
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

bool HexView::handleOverlayPaintEvent(QObject* obj, QPaintEvent* event) const {
  auto overlay = qobject_cast<QWidget*>(obj);
  auto scrollArea = getContainingScrollArea(overlay->parentWidget());
  QPainter painter(static_cast<QWidget*>(obj));

  auto eventRect = event->rect();
  int drawHeight = eventRect.height();
  int drawYPos = eventRect.y();

  // Sometimes the entire view is invalidated. We only need to draw the portion inside the viewport.
  if (eventRect.height() > scrollArea->viewport()->height()) {
    drawHeight = scrollArea->viewport()->height();
    drawYPos = scrollArea->verticalScrollBar()->value() - overlay->y();
  }

  painter.fillRect(QRect(0, drawYPos, BYTES_PER_LINE * 3 * charWidth, drawHeight),
    QColor(0, 0, 0, OVERLAY_ALPHA));

  if (shouldDrawAscii) {
    painter.fillRect(
        QRect(((BYTES_PER_LINE * 3) + HEX_TO_ASCII_SPACING_CHARS) * charWidth + charHalfWidth,
              drawYPos,
              BYTES_PER_LINE * charWidth, drawHeight),
        QColor(0, 0, 0, OVERLAY_ALPHA));
  }

  return true;
}

bool HexView::handleSelectedItemPaintEvent(QObject* obj, QPaintEvent* event) {
  auto widget = static_cast<QWidget*>(obj);

  // When no item is selected, paint using the cached pixmap. This is necessary because the
  // selected item is still visible during the deselection animation, after which it will be hidden
  if (!selectedItem) {
    QPainter painter(widget);
    painter.drawPixmap(0, 0, selectionViewPixmap);
    return true;
  }

  bool shouldDrawNonShadowedItem = selectionViewPixmap.isNull();
  bool shouldDrawShadowedItem = selectionViewPixmapWithShadow.isNull() && !selectedItemShadowEffect->isEnabled();

  qreal dpr = devicePixelRatioF();

  if (shouldDrawNonShadowedItem) {
    auto widgetSize = widget->size();
    selectionViewPixmap = QPixmap(widgetSize.width() * dpr, widgetSize.height() * dpr);
    selectionViewPixmap.setDevicePixelRatio(dpr);
    selectionViewPixmap.fill(Qt::transparent);

    QPainter pixmapPainter = QPainter(&selectionViewPixmap);

    int baseOffset = static_cast<int>(selectedItem->offset() - vgmfile->offset());
    int startColumn = baseOffset % BYTES_PER_LINE;
    int numLines = ((startColumn + static_cast<int>(selectedItem->length())) / BYTES_PER_LINE) + 1;

    auto itemData = std::vector<uint8_t>(selectedItem->length());
    vgmfile->readBytes(selectedItem->offset(), selectedItem->length(), itemData.data());

    QColor bgColor = colorForItemType(selectedItem->type);
    QColor textColor = textColorForItemType(selectedItem->type);

    pixmapPainter.setFont(this->font());
    pixmapPainter.translate(SELECTION_PADDING, SELECTION_PADDING);

    int startLine = baseOffset / BYTES_PER_LINE;

    int col = startColumn;
    int offsetIntoEvent = 0;
    for (int line=0; line<numLines; line++) {
      pixmapPainter.save();
      pixmapPainter.translate(0, line * lineHeight);

      int bytesToPrint = std::min(
                    std::min(static_cast<int>(selectedItem->length()) - offsetIntoEvent,BYTES_PER_LINE - col),
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
          int startAddress = static_cast<int>(selectedItem->offset() + offsetIntoEvent);
          int endAddress = static_cast<int>(selectedItem->offset() + selectedItem->length());
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
      pixmapPainter.restore();
    }
  }
  if (shouldDrawShadowedItem) {
    selectionViewPixmapWithShadow = QPixmap(selectionViewPixmap.width(), selectionViewPixmap.height());
    selectionViewPixmapWithShadow.setDevicePixelRatio(dpr);

    auto shadowEffect = new QGraphicsDropShadowEffect();
    shadowEffect->setBlurRadius(SHADOW_BLUR_RADIUS);
    shadowEffect->setColor(SHADOW_COLOR);
    shadowEffect->setOffset(SHADOW_OFFSET_X, SHADOW_OFFSET_Y);

    selectionViewPixmapWithShadow.fill(Qt::transparent);
    applyEffectToPixmap(selectionViewPixmap, selectionViewPixmapWithShadow, shadowEffect, 0);
  }

  QPixmap* pixmapToDraw = selectedItemShadowEffect->isEnabled() ? &selectionViewPixmap : &selectionViewPixmapWithShadow;
  QPainter painter(widget);

  auto eventRect = event->rect();
  QRect sourceRect = QRect(eventRect.topLeft() * dpr, eventRect.size() * dpr);
  painter.drawPixmap(eventRect, *pixmapToDraw, sourceRect);

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
  int endLine = paintRect.bottom() / lineHeight;

  qreal dpr = devicePixelRatioF();

  painter.translate(0, startLine * lineHeight);
  for (int line = startLine; line <= endLine; line++) {
    // Skip drawing lines that are totally eclipsed by the selected item
    if (selectedItem && selectedItem->length() >= BYTES_PER_LINE) {
      auto startAddress = vgmfile->offset() + (line * BYTES_PER_LINE);
      auto endAddress = startAddress + BYTES_PER_LINE;
      if (selectedItem->offset() <= startAddress && selectedItem->offset() + selectedItem->length() >= endAddress) {
        if (shouldDrawOffset)
          printAddress(painter, line);
        painter.translate(0, lineHeight);
        continue;
      }
    }

    auto cachedPixmap = lineCache.object(line);
    if (!cachedPixmap) {
      auto linePixmap = new QPixmap(int(getActualVirtualWidth() * dpr), int(lineHeight * dpr));
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

  auto startAddress = vgmfile->offset() + (line * BYTES_PER_LINE);
  auto endAddress = vgmfile->offset() + vgmfile->length();
  printData(painter, startAddress, endAddress);
  painter.restore();
}

static inline QString formatAddressHex8(quint32 v) {
  const auto& t = HEX_NIBBLE_TABLE;
  QChar out[8];
  // highest nibble first
  out[0] = t[(v >> 28) & 0xF];
  out[1] = t[(v >> 24) & 0xF];
  out[2] = t[(v >> 20) & 0xF];
  out[3] = t[(v >> 16) & 0xF];
  out[4] = t[(v >> 12) & 0xF];
  out[5] = t[(v >>  8) & 0xF];
  out[6] = t[(v >>  4) & 0xF];
  out[7] = t[(v >>  0) & 0xF];
  return QString(out, 8);
}

static inline QString formatAddressDec8(quint32 v) {
  return QString::number(v).rightJustified(8, QLatin1Char('0'));
}

void HexView::printAddress(QPainter& painter, int line) const {
  const quint32 fileOffset = quint32(vgmfile->offset()) + quint32(line * BYTES_PER_LINE);
  const QString s = addressAsHex ? formatAddressHex8(fileOffset) : formatAddressDec8(fileOffset);
  painter.drawText(QPoint(0, ascent), s);
}

void HexView::printData(QPainter& painter, int startAddress, int endAddress) const {
  if (endAddress > static_cast<int>(vgmfile->offset() + vgmfile->length()) || (startAddress >= endAddress)) {
    return;
  }

  int startCol = static_cast<int>(startAddress - vgmfile->offset()) % BYTES_PER_LINE;
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
      QColor bgColor = colorForItemType(item->type);
      QColor textColor = textColorForItemType(item->type);
      int col = startCol + offset;

      // In case the event spans multiple lines, account for how far into the event we are at this line
      int offsetIntoEvent = std::max(0, startAddress - static_cast<int>(item->offset()));
      auto numEventBytesToPrint = std::min(static_cast<int>(item->length() - offsetIntoEvent), bytesToPrint - offset);
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
    const QColor& bgColor,
    const QColor& textColor
) const {
  // Draw background color
  auto width = length * charWidth * 3;
  int rectWidth = width;
  int rectHeight = lineHeight;
  painter.fillRect(-charHalfWidth, 0, rectWidth, rectHeight, bgColor);

  // Draw bytes string
  const auto& table = HEX_LOOKUP_TABLE;
  QVarLengthArray<QChar, 48> hexChars(length * 3);
  QChar* out = hexChars.data();
  for (int i = 0; i < length; ++i) {
    // const QChar* src = table.data() + (static_cast<int>(data[i]) * 3);
    // std::copy_n(src, 3, out + (i * 3));
    const QChar* src = table.data() + (int(data[i]) * 3);
    QChar* dst = out + (i * 3);
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
  }
  const QString hexString(hexChars.constData(), hexChars.size());

  painter.setPen(textColor);
  painter.drawText(QPoint(0, ascent), hexString);
  painter.translate(width, 0);
}

void HexView::translateAndPrintHex(
    QPainter& painter,
    const uint8_t* data,
    int offset,
    int length,
    const QColor& bgColor,
    const QColor& textColor
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
    const QColor& bgColor,
    const QColor& textColor
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
    const QColor& bgColor,
    const QColor& textColor
) const {
  // Draw background color
  auto width = length * charWidth;
  painter.fillRect(0, 0, width, lineHeight, bgColor);

  // Draw ascii characters
  QVarLengthArray<QChar, 16> asciiChars(length);
  QChar* out = asciiChars.data();
  for (int i = 0; i < length; ++i) {
    const uint8_t value = data[i];
    out[i] = (value < 0x20 || value > 0x7E) ? QChar('.') : QChar(value);
  }
  const QString asciiString(asciiChars.constData(), asciiChars.size());
  painter.setPen(textColor);
  painter.drawText(QPoint(0, ascent), asciiString);
}

void HexView::initAnimations() {
  selectionAnimation = new QParallelAnimationGroup(this);

  auto overlayOpacityAnimation = new QPropertyAnimation(overlayOpacityEffect, "opacity");
  overlayOpacityAnimation->setDuration(DIM_DURATION_MS);
  overlayOpacityAnimation->setStartValue(0);
  overlayOpacityAnimation->setEndValue(1.0);
  overlayOpacityAnimation->setEasingCurve(QEasingCurve::OutQuad);

  auto selectedItemShadowBlurAnimation = new QPropertyAnimation(selectedItemShadowEffect, "blurRadius");
  selectedItemShadowBlurAnimation->setDuration(DIM_DURATION_MS);
  selectedItemShadowBlurAnimation->setStartValue(0);
  selectedItemShadowBlurAnimation->setEndValue(SHADOW_BLUR_RADIUS);
  selectedItemShadowBlurAnimation->setEasingCurve(QEasingCurve::OutQuad);

  auto selectedItemShadowOffsetAnimation = new QPropertyAnimation(selectedItemShadowEffect, "offset");
  selectedItemShadowOffsetAnimation->setDuration(DIM_DURATION_MS);
  selectedItemShadowOffsetAnimation->setStartValue(QPointF(0, 0));
  selectedItemShadowOffsetAnimation->setEndValue(QPointF(SHADOW_OFFSET_X, SHADOW_OFFSET_Y));
  selectedItemShadowOffsetAnimation->setEasingCurve(QEasingCurve::OutQuad);

  selectionAnimation->addAnimation(overlayOpacityAnimation);
  selectionAnimation->addAnimation(selectedItemShadowOffsetAnimation);
  selectionAnimation->addAnimation(selectedItemShadowBlurAnimation);

  QObject::connect(selectionAnimation, &QPropertyAnimation::finished,
    [this, overlayOpacityAnimation, selectedItemShadowBlurAnimation, selectedItemShadowOffsetAnimation]() {
    // After the animation has finished, we draw the final shadowed pixmap to cache so that the
    // effect doesn't have to be redrawn on every subsequent draw pass
    selectedItemShadowEffect->setEnabled(false);
    if (selectionAnimation->direction() == QAbstractAnimation::Backward) {
      overlay->hide();
      selectionView->hide();
      selectionViewPixmap = QPixmap();
      selectionViewPixmapWithShadow = QPixmap();
    }
    auto quadCurve = selectionAnimation->direction() == QAbstractAnimation::Forward ? QEasingCurve::InQuad : QEasingCurve::OutQuad;
    auto expoCurve = selectionAnimation->direction() == QAbstractAnimation::Forward ? QEasingCurve::InExpo : QEasingCurve::OutExpo;
    overlayOpacityAnimation->setEasingCurve(quadCurve);
    selectedItemShadowBlurAnimation->setEasingCurve(quadCurve);
    selectedItemShadowOffsetAnimation->setEasingCurve(expoCurve);
  });

}

void HexView::showSelectedItem(bool show, bool animate) {
  if (! animate) {
    selectionAnimation->stop();
    selectedItemShadowEffect->setEnabled(false);
    if (show) {
      overlay->show();
    } else {
      overlay->hide();
    }
    return;
  }

  if (show) {
    if (selectionAnimation->state() == QAbstractAnimation::Stopped && !overlay->isHidden() ||
        selectionAnimation->state() == QAbstractAnimation::Running && selectionAnimation->direction() == QAbstractAnimation::Forward) {
      return;
    }

    // If a current animation is running, stop it and delete it - we replace it with a new animation
    if (selectionAnimation->state() == QAbstractAnimation::Running) {
      selectionAnimation->pause();
    }

    selectionAnimation->setDirection(QAbstractAnimation::Forward);

    if (selectionAnimation->state() == QAbstractAnimation::Paused) {
      selectionAnimation->resume();
    } else {
      selectionAnimation->start();
    }
    overlay->show();
  } else {
    if (selectionAnimation->state() == QAbstractAnimation::Running) {
      selectionAnimation->pause();
    }

    selectionAnimation->setDirection(QAbstractAnimation::Backward);
    if (selectionAnimation->state() == QAbstractAnimation::Paused) {
      selectionAnimation->resume();
    } else {
      selectionAnimation->start();
    }
  }
  selectedItemShadowEffect->setEnabled(true);
}

void HexView::drawSelectedItem() const {
  if (!selectionView) {
    return;
  }
  // Set a limit for selected item size, as it can halt the system to draw huge items
  if (!selectedItem || selectedItem->length() > 0x3000) {
    selectionView->setGeometry(QRect(0, 0, 0, 0));
    return;
  }

  int baseOffset = static_cast<int>(selectedItem->offset() - vgmfile->offset());
  int startLine = baseOffset / BYTES_PER_LINE;
  int startColumn = baseOffset % BYTES_PER_LINE;
  int numLines = ((startColumn + static_cast<int>(selectedItem->length())) / BYTES_PER_LINE) + 1;

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
  return vgmfile->offset() + (line * BYTES_PER_LINE) + byteNum;
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
    if (selectedItem && (selectedOffset >= selectedItem->offset()) &&
        (selectedOffset < (selectedItem->offset() + selectedItem->length()))) {
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
