/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexView.h"
#include "Helpers.h"
#include "HexViewRhiWindow.h"
#include "VGMFile.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QRectF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QToolTip>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>

namespace {
constexpr int BYTES_PER_LINE = 16;
constexpr int NUM_ADDRESS_NIBBLES = 8;
constexpr int ADDRESS_SPACING_CHARS = 4;
constexpr int HEX_TO_ASCII_SPACING_CHARS = 4;
constexpr int SELECTION_PADDING = 18;
constexpr int VIEWPORT_PADDING = 10;
constexpr int DIM_DURATION_MS = 200;
constexpr int OVERLAY_ALPHA = 80;
constexpr float OVERLAY_ALPHA_F = OVERLAY_ALPHA / 255.0f;
constexpr float SHADOW_OFFSET_X = 0.0f;
constexpr float SHADOW_OFFSET_Y = 0.0f;
constexpr float SHADOW_BLUR_RADIUS = SELECTION_PADDING * 2.0f;
constexpr float SHADOW_STRENGTH = 1.5;
constexpr uint16_t STYLE_UNASSIGNED = std::numeric_limits<uint16_t>::max();
}  // namespace

HexView::~HexView() = default;

HexView::HexView(VGMFile* vgmfile, QWidget* parent)
    : QAbstractScrollArea(parent), m_vgmfile(vgmfile) {
  setFocusPolicy(Qt::StrongFocus);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setAutoFillBackground(false);

  m_rhiWindow = new HexViewRhiWindow(this);
  m_rhiContainer = QWidget::createWindowContainer(m_rhiWindow, viewport());
  m_rhiContainer->setFocusPolicy(Qt::NoFocus);
  // m_rhiContainer->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_rhiContainer->setGeometry(viewport()->rect());
  m_rhiContainer->show();

  const double appFontPointSize = QApplication::font().pointSizeF();
  QFont font("Roboto Mono", appFontPointSize + 1.0);
  font.setPointSizeF(appFontPointSize + 1.0);
  setShadowStrength(SHADOW_STRENGTH);

  setFont(font);
  rebuildStyleMap();
  initAnimations();
  updateLayout();
}

void HexView::setFont(const QFont& font) {
  QFont adjustedFont = font;
  QFontMetricsF fontMetrics(adjustedFont);

  const qreal originalCharWidth = fontMetrics.horizontalAdvance("A");
  const qreal originalLetterSpacing = adjustedFont.letterSpacing();
  const qreal charWidthSansSpacing = originalCharWidth - originalLetterSpacing;
  const qreal targetLetterSpacing = std::round(charWidthSansSpacing) - charWidthSansSpacing;
  adjustedFont.setLetterSpacing(QFont::AbsoluteSpacing, targetLetterSpacing);

  fontMetrics = QFontMetricsF(adjustedFont);
  m_charWidth = static_cast<int>(std::round(fontMetrics.horizontalAdvance("A")));
  m_charHalfWidth = static_cast<int>(std::round(fontMetrics.horizontalAdvance("A") / 2.0));
  m_lineHeight = static_cast<int>(std::round(fontMetrics.height()));

  QAbstractScrollArea::setFont(adjustedFont);

  m_virtual_full_width = -1;
  m_virtual_width_sans_ascii = -1;
  m_virtual_width_sans_ascii_and_address = -1;

  if (m_glyphAtlas) {
    m_glyphAtlas->dpr = 0.0;
  }

  updateLayout();
  if (m_rhiWindow) {
    m_rhiWindow->markBaseDirty();
    m_rhiWindow->markSelectionDirty();
    m_rhiWindow->requestUpdate();
  }
}

int HexView::hexXOffset() const {
  return m_shouldDrawOffset ? ((NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS) * m_charWidth) : 0;
}

int HexView::getVirtualHeight() const {
  return m_lineHeight * getTotalLines();
}

int HexView::getVirtualFullWidth() const {
  if (m_virtual_full_width == -1) {
    constexpr int numChars = NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS + (BYTES_PER_LINE * 3) +
                             HEX_TO_ASCII_SPACING_CHARS + BYTES_PER_LINE;
    m_virtual_full_width = (numChars * m_charWidth) + SELECTION_PADDING;
  }
  return m_virtual_full_width;
}

int HexView::getVirtualWidthSansAscii() const {
  if (m_virtual_width_sans_ascii == -1) {
    constexpr int numChars = NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS + (BYTES_PER_LINE * 3);
    m_virtual_width_sans_ascii = (numChars * m_charWidth) + SELECTION_PADDING;
  }
  return m_virtual_width_sans_ascii;
}

int HexView::getVirtualWidthSansAsciiAndAddress() const {
  if (m_virtual_width_sans_ascii_and_address == -1) {
    constexpr int numChars = BYTES_PER_LINE * 3;
    m_virtual_width_sans_ascii_and_address = (numChars * m_charWidth) + SELECTION_PADDING;
  }
  return m_virtual_width_sans_ascii_and_address;
}

int HexView::getActualVirtualWidth() const {
  if (m_shouldDrawAscii) {
    return getVirtualFullWidth();
  }
  if (m_shouldDrawOffset) {
    return getVirtualWidthSansAscii();
  }
  return getVirtualWidthSansAsciiAndAddress();
}

int HexView::getViewportFullWidth() const {
  return getVirtualFullWidth() + VIEWPORT_PADDING;
}

int HexView::getViewportWidthSansAscii() const {
  return getVirtualWidthSansAscii() + VIEWPORT_PADDING;
}

int HexView::getViewportWidthSansAsciiAndAddress() const {
  return getVirtualWidthSansAsciiAndAddress() + VIEWPORT_PADDING;
}

void HexView::updateScrollBars() {
  const int totalHeight = getVirtualHeight();
  const int pageStep = viewport()->height();
  verticalScrollBar()->setRange(0, std::max(0, totalHeight - pageStep));
  verticalScrollBar()->setPageStep(pageStep);
  verticalScrollBar()->setSingleStep(m_lineHeight);
}

void HexView::updateLayout() {
  if (m_rhiContainer) {
    m_rhiContainer->setGeometry(viewport()->rect());
  }

  const int width = viewport()->width();
  const int height = viewport()->height();
  if (width == 0 || height == 0) {
    return;
  }

  const bool newDrawOffset = width >= getViewportWidthSansAsciiAndAddress();
  const bool newDrawAscii = width >= getViewportWidthSansAscii();
  const bool offsetChanged = (newDrawOffset != m_shouldDrawOffset);
  const bool asciiChanged = (newDrawAscii != m_shouldDrawAscii);
  m_shouldDrawOffset = newDrawOffset;
  m_shouldDrawAscii = newDrawAscii;

  updateScrollBars();

  if (m_rhiWindow) {
    if (offsetChanged || asciiChanged) {
      m_rhiWindow->markBaseDirty();
      m_rhiWindow->markSelectionDirty();
    }
    m_rhiWindow->requestUpdate();
  }
}

int HexView::getTotalLines() const {
  if (!m_vgmfile) {
    return 0;
  }
  return static_cast<int>((m_vgmfile->unLength + BYTES_PER_LINE - 1) / BYTES_PER_LINE);
}

void HexView::setSelectedItem(VGMItem* item) {
  m_selectedItem = item;

  if (!m_selectedItem) {
    if (!m_selections.empty()) {
      m_fadeSelections = m_selections;
    }
    m_selections.clear();
    showSelectedItem(false, true);
    if (m_rhiWindow) {
      m_rhiWindow->markSelectionDirty();
      m_rhiWindow->requestUpdate();
    }
    return;
  }

  m_selectedOffset = m_selectedItem->dwOffset;
  m_selections.clear();
  m_selections.push_back({m_selectedItem->dwOffset, m_selectedItem->unLength});
  m_fadeSelections.clear();

  showSelectedItem(true, true);

  if (m_rhiWindow) {
    m_rhiWindow->markSelectionDirty();
    m_rhiWindow->requestUpdate();
  }

  if (!m_lineHeight) {
    return;
  }

  const int itemBaseOffset = static_cast<int>(m_selectedItem->dwOffset - m_vgmfile->dwOffset);
  const int line = itemBaseOffset / BYTES_PER_LINE;
  const int endLine = (itemBaseOffset + static_cast<int>(m_selectedItem->unLength)) / BYTES_PER_LINE;

  const int viewStartLine = verticalScrollBar()->value() / m_lineHeight;
  const int viewEndLine = viewStartLine + (viewport()->height() / m_lineHeight);

  if (line <= viewEndLine && endLine > viewStartLine) {
    return;
  }

  if (line < viewStartLine) {
    verticalScrollBar()->setValue(line * m_lineHeight);
  } else if (endLine > viewEndLine) {
    if ((endLine - line) > (viewport()->height() / m_lineHeight)) {
      verticalScrollBar()->setValue(line * m_lineHeight);
    } else {
      const int y = ((endLine + 1) * m_lineHeight) + 1 - viewport()->height();
      verticalScrollBar()->setValue(y);
    }
  }
}

void HexView::rebuildStyleMap() {
  m_styles.clear();
  m_typeToStyleId.clear();

  Style defaultStyle;
  defaultStyle.bg = palette().color(QPalette::Window);
  defaultStyle.fg = palette().color(QPalette::WindowText);
  m_styles.push_back(defaultStyle);

  if (!m_vgmfile) {
    return;
  }

  const uint32_t length = m_vgmfile->unLength;
  m_styleIds.assign(length, STYLE_UNASSIGNED);

  auto styleIdForType = [&](VGMItem::Type type) -> uint16_t {
    const int key = static_cast<int>(type);
    auto it = m_typeToStyleId.find(key);
    if (it != m_typeToStyleId.end()) {
      return it->second;
    }
    Style style;
    style.bg = colorForItemType(type);
    style.fg = textColorForItemType(type);
    uint16_t id = static_cast<uint16_t>(m_styles.size());
    m_styles.push_back(style);
    m_typeToStyleId.emplace(key, id);
    return id;
  };

  std::vector<VGMItem*> leaves;
  std::function<void(VGMItem*)> walk = [&](VGMItem* item) {
    auto& children = item->children();
    if (children.empty()) {
      leaves.push_back(item);
      return;
    }
    for (auto* child : children) {
      walk(child);
    }
  };

  walk(m_vgmfile);

  for (auto* item : leaves) {
    if (!item || item->unLength == 0) {
      continue;
    }
    if (item->dwOffset < m_vgmfile->dwOffset) {
      continue;
    }
    const uint32_t start = item->dwOffset - m_vgmfile->dwOffset;
    if (start >= length) {
      continue;
    }
    const uint32_t end = std::min<uint32_t>(start + item->unLength, length);
    const uint16_t styleId = styleIdForType(item->type);
    for (uint32_t i = start; i < end; ++i) {
      if (m_styleIds[i] == STYLE_UNASSIGNED) {
        m_styleIds[i] = styleId;
      }
    }
  }

  for (auto& styleId : m_styleIds) {
    if (styleId == STYLE_UNASSIGNED) {
      styleId = 0;
    }
  }

  if (m_rhiWindow) {
    m_rhiWindow->invalidateCache();
  }
}

void HexView::ensureGlyphAtlas(qreal dpr) {
  if (!m_glyphAtlas) {
    m_glyphAtlas = std::make_unique<GlyphAtlas>();
  }

  const bool needsRebuild =
      m_glyphAtlas->dpr != dpr ||
      m_glyphAtlas->glyphWidth != m_charWidth ||
      m_glyphAtlas->glyphHeight != m_lineHeight ||
      m_glyphAtlas->font != font();

  if (!needsRebuild) {
    return;
  }

  const int padding = 1;
  const int glyphWidth = m_charWidth;
  const int glyphHeight = m_lineHeight;
  const int cellWidth = glyphWidth + padding * 2;
  const int cellHeight = glyphHeight + padding * 2;

  std::vector<QChar> glyphs;
  glyphs.reserve(0x7E - 0x20 + 1);
  for (ushort ch = 0x20; ch <= 0x7E; ++ch) {
    glyphs.emplace_back(QChar(ch));
  }

  const int columns = 16;
  const int rows = static_cast<int>((glyphs.size() + columns - 1) / columns);

  const int imageWidth = static_cast<int>(std::ceil(cellWidth * dpr * columns));
  const int imageHeight = static_cast<int>(std::ceil(cellHeight * dpr * rows));

  QImage image(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  image.setDevicePixelRatio(dpr);

  QPainter painter(&image);
  painter.setFont(font());
  painter.setPen(Qt::white);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  const qreal baseline = QFontMetricsF(font()).ascent();

  m_glyphAtlas->uvTable.fill(QRectF());

  for (size_t i = 0; i < glyphs.size(); ++i) {
    const int col = static_cast<int>(i % columns);
    const int row = static_cast<int>(i / columns);
    const qreal cellX = col * cellWidth;
    const qreal cellY = row * cellHeight;

    painter.drawText(QPointF(cellX + padding, cellY + padding + baseline), QString(glyphs[i]));

    const qreal u0 = (cellX + padding) * dpr / imageWidth;
    const qreal v0 = (cellY + padding) * dpr / imageHeight;
    const qreal u1 = (cellX + padding + glyphWidth) * dpr / imageWidth;
    const qreal v1 = (cellY + padding + glyphHeight) * dpr / imageHeight;

    const ushort code = glyphs[i].unicode();
    if (code < m_glyphAtlas->uvTable.size()) {
      m_glyphAtlas->uvTable[code] = QRectF(u0, v0, u1 - u0, v1 - v0);
    }
  }

  m_glyphAtlas->image = std::move(image);
  m_glyphAtlas->dpr = dpr;
  m_glyphAtlas->glyphWidth = glyphWidth;
  m_glyphAtlas->glyphHeight = glyphHeight;
  m_glyphAtlas->cellWidth = cellWidth;
  m_glyphAtlas->cellHeight = cellHeight;
  m_glyphAtlas->font = font();
  m_glyphAtlas->version++;

  if (m_rhiWindow) {
    m_rhiWindow->markBaseDirty();
    m_rhiWindow->markSelectionDirty();
  }
}

bool HexView::viewportEvent(QEvent* event) {
  if (event->type() == QEvent::ToolTip) {
    auto* helpEvent = static_cast<QHelpEvent*>(event);
    const int offset = getOffsetFromPoint(helpEvent->pos());
    if (offset < 0) {
      QToolTip::hideText();
      return true;
    }
    if (VGMItem* item = m_vgmfile->getItemAtOffset(offset, false)) {
      const QString description = getFullDescriptionForTooltip(item);
      if (!description.isEmpty()) {
        QToolTip::showText(helpEvent->globalPos(), description, this);
      }
    }
    return true;
  }

  return QAbstractScrollArea::viewportEvent(event);
}

void HexView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  if (m_rhiContainer) {
    m_rhiContainer->setGeometry(viewport()->rect());
  }
  updateLayout();
  if (m_rhiWindow) {
    m_rhiWindow->requestUpdate();
  }
}

void HexView::scrollContentsBy(int dx, int dy) {
  QAbstractScrollArea::scrollContentsBy(dx, dy);
  if (m_rhiWindow) {
    m_rhiWindow->requestUpdate();
  }
}

void HexView::changeEvent(QEvent* event) {
  if (event->type() == QEvent::PaletteChange) {
    if (!m_styles.empty()) {
      m_styles[0].bg = palette().color(QPalette::Window);
      m_styles[0].fg = palette().color(QPalette::WindowText);
    }
    if (m_rhiWindow) {
      m_rhiWindow->markBaseDirty();
      m_rhiWindow->markSelectionDirty();
      m_rhiWindow->requestUpdate();
    }
  }
  QAbstractScrollArea::changeEvent(event);
}

void HexView::keyPressEvent(QKeyEvent* event) {
  if (!m_selectedItem) {
    QAbstractScrollArea::keyPressEvent(event);
    return;
  }

  uint32_t newOffset = 0;
  switch (event->key()) {
    case Qt::Key_Up:
      newOffset = m_selectedOffset - BYTES_PER_LINE;
      goto selectNewOffset;

    case Qt::Key_Down: {
      const int selectedCol = (m_selectedOffset - m_vgmfile->dwOffset) % BYTES_PER_LINE;
      const int endOffset = m_selectedItem->dwOffset - m_vgmfile->dwOffset + m_selectedItem->unLength;
      const int itemEndCol = endOffset % BYTES_PER_LINE;
      const int itemEndLine = endOffset / BYTES_PER_LINE;
      newOffset = m_vgmfile->dwOffset +
                  ((itemEndLine + (selectedCol > itemEndCol ? 0 : 1)) * BYTES_PER_LINE) +
                  selectedCol;
      goto selectNewOffset;
    }

    case Qt::Key_Left:
      newOffset = m_selectedItem->dwOffset - 1;
      goto selectNewOffset;

    case Qt::Key_Right:
      newOffset = m_selectedItem->dwOffset + m_selectedItem->unLength;
      goto selectNewOffset;

    case Qt::Key_Escape:
      selectionChanged(nullptr);
      return;

    selectNewOffset:
      if (newOffset >= m_vgmfile->dwOffset &&
          newOffset < (m_vgmfile->dwOffset + m_vgmfile->unLength)) {
        m_selectedOffset = newOffset;
        if (auto* item = m_vgmfile->getItemAtOffset(newOffset, false)) {
          selectionChanged(item);
        }
      }
      return;

    default:
      QAbstractScrollArea::keyPressEvent(event);
      return;
  }
}

int HexView::getOffsetFromPoint(QPoint pos) const {
  const int y = pos.y() + verticalScrollBar()->value();
  const int line = m_lineHeight > 0 ? (y / m_lineHeight) : 0;
  if (line < 0 || line >= getTotalLines()) {
    return -1;
  }

  const int hexStart = hexXOffset();
  const int hexEnd = hexStart + (BYTES_PER_LINE * 3 * m_charWidth);
  const int asciiStart = hexEnd + (HEX_TO_ASCII_SPACING_CHARS * m_charWidth);
  const int asciiEnd = asciiStart + (BYTES_PER_LINE * m_charWidth);

  int byteNum = -1;
  if (pos.x() >= hexStart - m_charHalfWidth && pos.x() < hexEnd - m_charHalfWidth) {
    byteNum = ((pos.x() - hexStart + m_charHalfWidth) / m_charWidth) / 3;
  } else if (pos.x() >= asciiStart && pos.x() < asciiEnd) {
    byteNum = (pos.x() - asciiStart) / m_charWidth;
  }
  if (byteNum == -1) {
    return -1;
  }

  const int offset = m_vgmfile->dwOffset + (line * BYTES_PER_LINE) + byteNum;
  if (offset < static_cast<int>(m_vgmfile->dwOffset) ||
      offset >= static_cast<int>(m_vgmfile->dwOffset + m_vgmfile->unLength)) {
    return -1;
  }
  return offset;
}

void HexView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const int offset = getOffsetFromPoint(event->pos());
    if (offset == -1) {
      selectionChanged(nullptr);
      return;
    }

    m_selectedOffset = offset;
    auto* item = m_vgmfile->getItemAtOffset(offset, false);
    if (item == m_selectedItem) {
      selectionChanged(nullptr);
    } else {
      selectionChanged(item);
    }
    m_isDragging = true;
  }

  QAbstractScrollArea::mousePressEvent(event);
}

void HexView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
  }
  QAbstractScrollArea::mouseReleaseEvent(event);
}

void HexView::handleCoalescedMouseMove(const QPoint& pos,
                              Qt::MouseButtons buttons,
                              Qt::KeyboardModifiers mods) {
  if (m_isDragging && buttons & Qt::LeftButton) {
    const int offset = getOffsetFromPoint(pos);
    if (offset == -1) {
      selectionChanged(nullptr);
      return;
    }
    m_selectedOffset = offset;
    if (m_selectedItem && (m_selectedOffset >= m_selectedItem->dwOffset) &&
        (m_selectedOffset < (m_selectedItem->dwOffset + m_selectedItem->unLength))) {
      return;
    }
    auto* item = m_vgmfile->getItemAtOffset(offset, false);
    if (item != m_selectedItem) {
      // setSelectedItem(item);
      selectionChanged(item);
    }
  }
}

void HexView::mouseMoveEvent(QMouseEvent* event) {
  handleCoalescedMouseMove(event->pos(), event->buttons(), event->modifiers());
  QAbstractScrollArea::mouseMoveEvent(event);
}

void HexView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    if (m_shouldDrawOffset && event->pos().x() >= 0 &&
        event->pos().x() < (NUM_ADDRESS_NIBBLES * m_charWidth)) {
      m_addressAsHex = !m_addressAsHex;
      if (m_rhiWindow) {
        m_rhiWindow->markBaseDirty();
        m_rhiWindow->requestUpdate();
      }
    }
  }
  QAbstractScrollArea::mouseDoubleClickEvent(event);
}

qreal HexView::overlayOpacity() const {
  return m_overlayOpacity;
}

void HexView::setOverlayOpacity(qreal opacity) {
  if (qFuzzyCompare(opacity, m_overlayOpacity)) {
    return;
  }
  m_overlayOpacity = opacity;
  if (m_rhiWindow) {
    m_rhiWindow->requestUpdate();
  }
}

qreal HexView::shadowBlur() const {
  return m_shadowBlur;
}

void HexView::setShadowBlur(qreal blur) {
  if (qFuzzyCompare(blur, m_shadowBlur)) {
    return;
  }
  m_shadowBlur = blur;
  if (m_rhiWindow) {
    m_rhiWindow->requestUpdate();
  }
}

QPointF HexView::shadowOffset() const {
  return m_shadowOffset;
}

void HexView::setShadowOffset(const QPointF& offset) {
  if (offset == m_shadowOffset) {
    return;
  }
  m_shadowOffset = offset;
  if (m_rhiWindow) {
    m_rhiWindow->requestUpdate();
  }
}

qreal HexView::shadowStrength() const { return m_shadowStrength; }

void HexView::setShadowStrength(qreal s) {
  s = std::max<qreal>(0.0, s);
  if (qFuzzyCompare(s, m_shadowStrength)) return;
  m_shadowStrength = s;
  if (m_rhiWindow) {
    m_rhiWindow->markSelectionDirty();
    m_rhiWindow->requestUpdate();
  }
}

void HexView::initAnimations() {
  m_selectionAnimation = new QParallelAnimationGroup(this);

  auto* overlayOpacityAnimation = new QPropertyAnimation(this, "overlayOpacity");
  overlayOpacityAnimation->setDuration(DIM_DURATION_MS);
  overlayOpacityAnimation->setStartValue(0.0);
  overlayOpacityAnimation->setEndValue(OVERLAY_ALPHA_F);
  overlayOpacityAnimation->setEasingCurve(QEasingCurve::OutQuad);

  auto* shadowBlurAnimation = new QPropertyAnimation(this, "shadowBlur");
  shadowBlurAnimation->setDuration(DIM_DURATION_MS);
  shadowBlurAnimation->setStartValue(0.0);
  shadowBlurAnimation->setEndValue(SHADOW_BLUR_RADIUS);
  shadowBlurAnimation->setEasingCurve(QEasingCurve::OutQuad);

  auto* shadowOffsetAnimation = new QPropertyAnimation(this, "shadowOffset");
  shadowOffsetAnimation->setDuration(DIM_DURATION_MS);
  shadowOffsetAnimation->setStartValue(QPointF(0.0, 0.0));
  shadowOffsetAnimation->setEndValue(QPointF(SHADOW_OFFSET_X, SHADOW_OFFSET_Y));
  shadowOffsetAnimation->setEasingCurve(QEasingCurve::OutQuad);

  m_selectionAnimation->addAnimation(overlayOpacityAnimation);
  m_selectionAnimation->addAnimation(shadowBlurAnimation);
  m_selectionAnimation->addAnimation(shadowOffsetAnimation);

  connect(m_selectionAnimation, &QParallelAnimationGroup::finished, this,
          [this, overlayOpacityAnimation, shadowBlurAnimation, shadowOffsetAnimation]() {
            if (m_selectionAnimation->direction() == QAbstractAnimation::Backward) {
              clearFadeSelection();
            }
            const auto quadCurve = m_selectionAnimation->direction() == QAbstractAnimation::Forward
                                       ? QEasingCurve::InQuad
                                       : QEasingCurve::OutQuad;
            const auto expoCurve = m_selectionAnimation->direction() == QAbstractAnimation::Forward
                                       ? QEasingCurve::InExpo
                                       : QEasingCurve::OutExpo;
            overlayOpacityAnimation->setEasingCurve(quadCurve);
            shadowBlurAnimation->setEasingCurve(quadCurve);
            shadowOffsetAnimation->setEasingCurve(expoCurve);
          });
}

void HexView::showSelectedItem(bool show, bool animate) {
  if (!animate) {
    m_selectionAnimation->stop();
    if (show) {
      setOverlayOpacity(OVERLAY_ALPHA_F);
      setShadowBlur(SHADOW_BLUR_RADIUS);
      setShadowOffset(QPointF(SHADOW_OFFSET_X, SHADOW_OFFSET_Y));
    } else {
      setOverlayOpacity(0.0);
      setShadowBlur(0.0);
      setShadowOffset(QPointF(0.0, 0.0));
      clearFadeSelection();
    }
    return;
  }

  if (show) {
    if (m_selectionAnimation->state() == QAbstractAnimation::Stopped && m_overlayOpacity > 0.0) {
      return;
    }
    if (m_selectionAnimation->state() == QAbstractAnimation::Running &&
        m_selectionAnimation->direction() == QAbstractAnimation::Forward) {
      return;
    }
    if (m_selectionAnimation->state() == QAbstractAnimation::Running) {
      m_selectionAnimation->pause();
    }
    m_selectionAnimation->setDirection(QAbstractAnimation::Forward);
    if (m_selectionAnimation->state() == QAbstractAnimation::Paused) {
      m_selectionAnimation->resume();
    } else {
      m_selectionAnimation->start();
    }
  } else {
    if (m_selectionAnimation->state() == QAbstractAnimation::Running) {
      m_selectionAnimation->pause();
    }
    m_selectionAnimation->setDirection(QAbstractAnimation::Backward);
    if (m_selectionAnimation->state() == QAbstractAnimation::Paused) {
      m_selectionAnimation->resume();
    } else {
      m_selectionAnimation->start();
    }
  }
}

void HexView::clearFadeSelection() {
  m_fadeSelections.clear();
  if (m_rhiWindow) {
    m_rhiWindow->markSelectionDirty();
    m_rhiWindow->requestUpdate();
  }
}
