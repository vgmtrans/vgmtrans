/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexView.h"
#include "Helpers.h"
#include "HexViewInput.h"
#include "HexViewRhiHost.h"
#include "VGMFile.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QRectF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QToolTip>
#include <QWidget>

#include <algorithm>
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
constexpr float SHADOW_BLUR_RADIUS = SELECTION_PADDING * 1.0f;
constexpr float SHADOW_STRENGTH = 0.5;
constexpr float SHADOW_EDGE_CURVE = 1.1f;
constexpr uint16_t STYLE_UNASSIGNED = std::numeric_limits<uint16_t>::max();

#ifdef Q_OS_MAC
class NonTransientScrollBarStyle final : public QProxyStyle {
public:
  using QProxyStyle::QProxyStyle;

  int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                const QWidget* widget = nullptr,
                QStyleHintReturn* returnData = nullptr) const override {
    if (hint == QStyle::SH_ScrollBar_Transient) {
      return 0;
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
  }
};
#endif

}  // namespace

// Pack selection offset/length into a stable 64-bit key for set membership checks.
uint64_t HexView::selectionKey(uint32_t offset, uint32_t length) {
  return (static_cast<uint64_t>(offset) << 32) | length;
}

// Overload for plain selection ranges.
uint64_t HexView::selectionKey(const SelectionRange& range) {
  return selectionKey(range.offset, range.length);
}

// Trivial destructor; QObject ownership handles child cleanup.
HexView::~HexView() = default;

// Initialize HexView UI state, RHI host, typography, animations, and signal wiring.
HexView::HexView(VGMFile* vgmfile, QWidget* parent)
    : QAbstractScrollArea(parent), m_vgmfile(vgmfile) {
  setFocusPolicy(Qt::StrongFocus);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setAutoFillBackground(false);

#ifdef Q_OS_MAC
  // With AA_DontCreateNativeWidgetSiblings, the RHI QWindow can cover transient (overlay)
  // scrollbars, so keep HexView's scrollbars non-transient on macOS.
  QStyle* baseStyle = QStyleFactory::create(QStringLiteral("macos"));
  if (!baseStyle)
    baseStyle = QStyleFactory::create(QStringLiteral("macintosh"));
  if (!baseStyle)
    baseStyle = QStyleFactory::create(QStringLiteral("Fusion"));
  auto* scrollStyle = baseStyle ? new NonTransientScrollBarStyle(baseStyle)
                                : new NonTransientScrollBarStyle();
  verticalScrollBar()->setStyle(scrollStyle);
  if (!scrollStyle->parent())
    scrollStyle->setParent(verticalScrollBar());
#endif

  m_rhiHost = new HexViewRhiHost(this, viewport());
  m_rhiHost->setGeometry(viewport()->rect());
  m_rhiHost->show();

  const double appFontPointSize = QApplication::font().pointSizeF();
  QFont font("Roboto Mono", appFontPointSize + 1.0);
  font.setPointSizeF(appFontPointSize + 1.0);
  setShadowStrength(SHADOW_STRENGTH);
  m_shadowEdgeCurve = SHADOW_EDGE_CURVE;

  setFont(font);
  rebuildStyleMap();
  initAnimations();
  updateLayout();

  auto* vbar = verticalScrollBar();
  m_pendingScrollY = vbar->value();
  connect(vbar, &QScrollBar::sliderPressed, this, [this]() {
    m_scrollBarDragging = true;
    m_pendingScrollY = verticalScrollBar()->value();
  });
  connect(vbar, &QScrollBar::sliderMoved, this, [this](int pos) {
    m_pendingScrollY = pos;
  });
  connect(vbar, &QScrollBar::sliderReleased, this, [this]() {
    m_scrollBarDragging = false;
    m_pendingScrollY = verticalScrollBar()->value();
  });

}

// Apply a monospaced font, normalize glyph metrics, and invalidate layout/glyph cache.
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
  requestRhiUpdate(true, true);
}

// Return X origin of the hex byte columns (accounting for optional address column).
int HexView::hexXOffset() const {
  return m_shouldDrawOffset ? ((NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS) * m_charWidth) : 0;
}

// Compute total virtual content height in pixels.
int HexView::getVirtualHeight() const {
  return m_lineHeight * getTotalLines();
}

// Compute full virtual content width (address + hex + ascii).
int HexView::getVirtualFullWidth() const {
  if (m_virtual_full_width == -1) {
    constexpr int numChars = NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS + (BYTES_PER_LINE * 3) +
                             HEX_TO_ASCII_SPACING_CHARS + BYTES_PER_LINE;
    m_virtual_full_width = (numChars * m_charWidth) + SELECTION_PADDING;
  }
  return m_virtual_full_width;
}

// Compute virtual width when ASCII column is hidden.
int HexView::getVirtualWidthSansAscii() const {
  if (m_virtual_width_sans_ascii == -1) {
    constexpr int numChars = NUM_ADDRESS_NIBBLES + ADDRESS_SPACING_CHARS + (BYTES_PER_LINE * 3);
    m_virtual_width_sans_ascii = (numChars * m_charWidth) + SELECTION_PADDING;
  }
  return m_virtual_width_sans_ascii;
}

// Compute virtual width when both ASCII and address columns are hidden.
int HexView::getVirtualWidthSansAsciiAndAddress() const {
  if (m_virtual_width_sans_ascii_and_address == -1) {
    constexpr int numChars = BYTES_PER_LINE * 3;
    m_virtual_width_sans_ascii_and_address = (numChars * m_charWidth) + SELECTION_PADDING;
  }
  return m_virtual_width_sans_ascii_and_address;
}

// Return active virtual width for the current column-visibility mode.
int HexView::getActualVirtualWidth() const {
  if (m_shouldDrawAscii) {
    return getVirtualFullWidth();
  }
  if (m_shouldDrawOffset) {
    return getVirtualWidthSansAscii();
  }
  return getVirtualWidthSansAsciiAndAddress();
}

// Return minimum viewport width needed to render full columns plus padding.
int HexView::getViewportFullWidth() const {
  return getVirtualFullWidth() + VIEWPORT_PADDING;
}

// Return minimum viewport width needed for address + hex columns plus padding.
int HexView::getViewportWidthSansAscii() const {
  return getVirtualWidthSansAscii() + VIEWPORT_PADDING;
}

// Return minimum viewport width needed for hex-only columns plus padding.
int HexView::getViewportWidthSansAsciiAndAddress() const {
  return getVirtualWidthSansAsciiAndAddress() + VIEWPORT_PADDING;
}

// Sync vertical scrollbar range/steps with current content and viewport dimensions.
void HexView::updateScrollBars() {
  const int totalHeight = getVirtualHeight();
  const int pageStep = viewport()->height();
  verticalScrollBar()->setRange(0, std::max(0, totalHeight - pageStep));
  verticalScrollBar()->setPageStep(pageStep);
  verticalScrollBar()->setSingleStep(m_lineHeight);
}

// Forward dirty/update requests to the active RHI host.
void HexView::requestRhiUpdate(bool markBaseDirty, bool markSelectionDirty) {
  if (!m_rhiHost) {
    return;
  }
  if (markBaseDirty) {
    m_rhiHost->markBaseDirty();
  }
  if (markSelectionDirty) {
    m_rhiHost->markSelectionDirty();
  }
  m_rhiHost->requestUpdate();
}

// Recompute responsive column visibility from viewport width and refresh rendering state.
void HexView::updateLayout() {
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

  requestRhiUpdate(offsetChanged || asciiChanged, offsetChanged || asciiChanged);
}

// Return total line count required to display file bytes at 16 bytes per line.
int HexView::getTotalLines() const {
  if (!m_vgmfile) {
    return 0;
  }
  return static_cast<int>((m_vgmfile->length() + BYTES_PER_LINE - 1) / BYTES_PER_LINE);
}

// Clear current manual selection, optionally preserving/animating fade semantics.
void HexView::clearCurrentSelection(bool animateSelection) {
  if (!m_selections.empty()) {
    m_fadeSelections = m_selections;
  }
  m_selections.clear();
  showSelectedItem(false, animateSelection);
  requestRhiUpdate(false, true);
}

// Select the current item's byte range and refresh highlight visuals.
void HexView::selectCurrentItem(bool animateSelection) {
  if (!m_selectedItem) {
    return;
  }
  m_selectedOffset = m_selectedItem->offset();
  m_selections.clear();
  m_selections.push_back({m_selectedItem->offset(), m_selectedItem->length()});
  m_fadeSelections.clear();
  updateHighlightState(animateSelection);
  requestRhiUpdate(false, true);
}

// Refresh overlay/shadow animation state for current selection state.
void HexView::refreshSelectionVisuals(bool animateSelection) {
  updateHighlightState(animateSelection);
  requestRhiUpdate(false, true);
}

// Set selected item, update selection, and scroll it into view when needed.
void HexView::setSelectedItem(VGMItem* item) {
  m_selectedItem = item;

  if (!m_selectedItem) {
    clearCurrentSelection(true);
    return;
  }

  selectCurrentItem(true);

  if (!m_lineHeight) {
    return;
  }

  const int itemBaseOffset = static_cast<int>(m_selectedItem->offset() - m_vgmfile->offset());
  const int line = itemBaseOffset / BYTES_PER_LINE;
  const int endLine = (itemBaseOffset + static_cast<int>(m_selectedItem->length())) / BYTES_PER_LINE;

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

// Build byte-level style-id lookup from VGM leaf items for renderer consumption.
void HexView::rebuildStyleMap() {
  m_styles.clear();
  m_typeToStyleId.clear();

  // Slot 0 is the fallback/default style used for unassigned bytes.
  Style defaultStyle;
  defaultStyle.bg = palette().color(QPalette::Window);
  defaultStyle.fg = palette().color(QPalette::WindowText);
  m_styles.push_back(defaultStyle);

  if (!m_vgmfile) {
    return;
  }

  const uint32_t length = m_vgmfile->length();
  // Start with "unassigned" markers so we can preserve first-write wins below.
  m_styleIds.assign(length, STYLE_UNASSIGNED);

  // Deduplicate styles by item type and keep a compact style table for the renderer.
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

  // Flatten the item tree to leaf items; leaf ranges represent final semantic regions.
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

  // Paint style ids into byte slots. First assignment wins to avoid later overlaps
  // from replacing an already chosen leaf style.
  for (auto* item : leaves) {
    if (!item || item->length() == 0) {
      continue;
    }
    if (item->offset() < m_vgmfile->offset()) {
      continue;
    }
    const uint32_t start = item->offset() - m_vgmfile->offset();
    if (start >= length) {
      continue;
    }
    const uint32_t end = std::min<uint32_t>(start + item->length(), length);
    const uint16_t styleId = styleIdForType(item->type);
    for (uint32_t i = start; i < end; ++i) {
      if (m_styleIds[i] == STYLE_UNASSIGNED) {
        m_styleIds[i] = styleId;
      }
    }
  }

  // Any bytes still unassigned fall back to default style (slot 0).
  for (auto& styleId : m_styleIds) {
    if (styleId == STYLE_UNASSIGNED) {
      styleId = 0;
    }
  }

  // Style-id changes invalidate renderer cache line snapshots.
  if (m_rhiHost) {
    m_rhiHost->invalidateCache();
  }
}

// Lazily rebuild glyph atlas texture and UV table when DPR/font/metrics change.
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

  if (m_rhiHost) {
    m_rhiHost->markBaseDirty();
    m_rhiHost->markSelectionDirty();
  }
}

// Handle tooltip events on the viewport and fall back to default viewport processing.
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

// Keep RHI host geometry and layout in sync with viewport resize.
void HexView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  if (m_rhiHost) {
    m_rhiHost->setGeometry(viewport()->rect());
  }
  updateLayout();
  requestRhiUpdate();
}

// Trigger rerender on scroll changes (content is rendered from absolute scroll offset).
void HexView::scrollContentsBy(int dx, int dy) {
  QAbstractScrollArea::scrollContentsBy(dx, dy);
  requestRhiUpdate();
}

// Provide render-time scroll offset, including live scrollbar drag position.
int HexView::scrollYForRender() const {
  if (m_scrollBarDragging) {
    return m_pendingScrollY;
  }
  return verticalScrollBar()->value();
}

// Capture immutable frame snapshot consumed by the RHI renderer this frame.
HexViewFrame::Data HexView::captureRhiFrameData(float dpr) {
  HexViewFrame::Data frame;
  frame.vgmfile = m_vgmfile;
  frame.viewportSize = viewport()->size();
  frame.totalLines = getTotalLines();
  frame.scrollY = scrollYForRender();
  frame.lineHeight = m_lineHeight;
  frame.charWidth = m_charWidth;
  frame.charHalfWidth = m_charHalfWidth;
  frame.hexStartX = hexXOffset();
  frame.shouldDrawOffset = m_shouldDrawOffset;
  frame.shouldDrawAscii = m_shouldDrawAscii;
  frame.addressAsHex = m_addressAsHex;

  frame.overlayOpacity = m_overlayOpacity;
  frame.shadowBlur = m_shadowBlur;
  frame.shadowStrength = m_shadowStrength;
  frame.shadowOffset = m_shadowOffset;
  frame.shadowEdgeCurve = m_shadowEdgeCurve;

  frame.windowColor = palette().color(QPalette::Window);
  frame.windowTextColor = palette().color(QPalette::WindowText);

  frame.styleIds = &m_styleIds;
  frame.styles.reserve(m_styles.size());
  for (const auto& style : m_styles) {
    frame.styles.push_back({style.bg, style.fg});
  }

  frame.selections.reserve(m_selections.size());
  for (const auto& range : m_selections) {
    frame.selections.push_back({range.offset, range.length});
  }

  frame.fadeSelections.reserve(m_fadeSelections.size());
  for (const auto& range : m_fadeSelections) {
    frame.fadeSelections.push_back({range.offset, range.length});
  }

  ensureGlyphAtlas(dpr);
  if (m_glyphAtlas) {
    frame.glyphAtlas.image = &m_glyphAtlas->image;
    frame.glyphAtlas.uvTable = &m_glyphAtlas->uvTable;
    frame.glyphAtlas.version = m_glyphAtlas->version;
  }

  return frame;
}

// React to palette updates so default style colors stay synchronized with theme changes.
void HexView::changeEvent(QEvent* event) {
  if (event->type() == QEvent::PaletteChange) {
    if (!m_styles.empty()) {
      m_styles[0].bg = palette().color(QPalette::Window);
      m_styles[0].fg = palette().color(QPalette::WindowText);
    }
    requestRhiUpdate(true, true);
  }
  QAbstractScrollArea::changeEvent(event);
}

// Handle keyboard navigation between adjacent items.
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
      const int selectedCol = (m_selectedOffset - m_vgmfile->offset()) % BYTES_PER_LINE;
      const int endOffset = m_selectedItem->offset() - m_vgmfile->offset() + m_selectedItem->length();
      const int itemEndCol = endOffset % BYTES_PER_LINE;
      const int itemEndLine = endOffset / BYTES_PER_LINE;
      newOffset = m_vgmfile->offset() +
                  ((itemEndLine + (selectedCol > itemEndCol ? 0 : 1)) * BYTES_PER_LINE) +
                  selectedCol;
      goto selectNewOffset;
    }

    case Qt::Key_Left:
      newOffset = m_selectedItem->offset() - 1;
      goto selectNewOffset;

    case Qt::Key_Right:
      newOffset = m_selectedItem->offset() + m_selectedItem->length();
      goto selectNewOffset;

    case Qt::Key_Escape:
      selectionChanged(nullptr);
      return;

    selectNewOffset:
      if (newOffset >= m_vgmfile->offset() &&
          newOffset < (m_vgmfile->offset() + m_vgmfile->length())) {
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

// Map viewport position to file byte offset across hex/ascii columns; return -1 if invalid.
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

  const int offset = m_vgmfile->offset() + (line * BYTES_PER_LINE) + byteNum;
  if (offset < static_cast<int>(m_vgmfile->offset()) ||
      offset >= static_cast<int>(m_vgmfile->offset() + m_vgmfile->length())) {
    return -1;
  }
  return offset;
}

// Handle click-to-select and drag-selection behavior.
void HexView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const int offset = getOffsetFromPoint(event->pos());
    auto* item = m_vgmfile->getItemAtOffset(offset, false);
    const bool seekModifier = event->modifiers().testFlag(HexViewInput::kModifier);
    if (seekModifier) {
      if (item && item != m_lastSeekItem) {
        m_lastSeekItem = item;
        seekToEventRequested(item);
      }
      m_isDragging = true;
      QAbstractScrollArea::mousePressEvent(event);
      return;
    }

    m_lastSeekItem = nullptr;
    if (offset == -1) {
      selectionChanged(nullptr);
      return;
    }

    m_selectedOffset = offset;
    if (item == m_selectedItem) {
      selectionChanged(nullptr);
    } else {
      selectionChanged(item);
    }
    m_isDragging = true;
  }

  QAbstractScrollArea::mousePressEvent(event);
}

// End drag interactions.
void HexView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
    m_lastSeekItem = nullptr;
  }
  QAbstractScrollArea::mouseReleaseEvent(event);
}

// Process drag-move updates for selection changes.
void HexView::handleCoalescedMouseMove(const QPoint& pos,
                              Qt::MouseButtons buttons,
                              Qt::KeyboardModifiers mods) {
  if (m_isDragging && buttons & Qt::LeftButton) {
    const int offset = getOffsetFromPoint(pos);
    if (offset == -1) {
      if (!mods.testFlag(HexViewInput::kModifier)) {
        selectionChanged(nullptr);
      }
      m_lastSeekItem = nullptr;
      return;
    }

    if (mods.testFlag(HexViewInput::kModifier)) {
      if (auto* item = m_vgmfile->getItemAtOffset(offset, false);
          item && item != m_lastSeekItem) {
        m_lastSeekItem = item;
        seekToEventRequested(item);
      }
      return;
    }

    m_lastSeekItem = nullptr;
    m_selectedOffset = offset;
    if (m_selectedItem && (m_selectedOffset >= m_selectedItem->offset()) &&
        (m_selectedOffset < (m_selectedItem->offset() + m_selectedItem->length()))) {
      return;
    }
    auto* item = m_vgmfile->getItemAtOffset(offset, false);
    if (item != m_selectedItem) {
      selectionChanged(item);
    }
  }
}

// Route mouse movement to drag-selection logic.
void HexView::mouseMoveEvent(QMouseEvent* event) {
  if (event->buttons() & Qt::LeftButton) {
    handleCoalescedMouseMove(event->pos(), event->buttons(), event->modifiers());
  }
  QAbstractScrollArea::mouseMoveEvent(event);
}

// Toggle address display radix when double-clicking in the address column.
void HexView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    if (m_shouldDrawOffset && event->pos().x() >= 0 &&
        event->pos().x() < (NUM_ADDRESS_NIBBLES * m_charWidth)) {
      m_addressAsHex = !m_addressAsHex;
      requestRhiUpdate(true);
    }
  }
  QAbstractScrollArea::mouseDoubleClickEvent(event);
}

// Property accessor used by selection dim animation.
qreal HexView::overlayOpacity() const {
  return m_overlayOpacity;
}

// Property mutator for dim overlay opacity; requests rerender on change.
void HexView::setOverlayOpacity(qreal opacity) {
  if (qFuzzyCompare(opacity, m_overlayOpacity)) {
    return;
  }
  m_overlayOpacity = opacity;
  requestRhiUpdate();
}

// Property accessor for selection shadow blur amount.
qreal HexView::shadowBlur() const {
  return m_shadowBlur;
}

// Property mutator for selection shadow blur; selection pass needs refresh.
void HexView::setShadowBlur(qreal blur) {
  if (qFuzzyCompare(blur, m_shadowBlur)) {
    return;
  }
  m_shadowBlur = blur;
  requestRhiUpdate(false, true);
}

// Property accessor for selection shadow offset.
QPointF HexView::shadowOffset() const {
  return m_shadowOffset;
}

// Property mutator for selection shadow offset; requests rerender on change.
void HexView::setShadowOffset(const QPointF& offset) {
  if (offset == m_shadowOffset) {
    return;
  }
  m_shadowOffset = offset;
  requestRhiUpdate();
}

// Property accessor for selection shadow strength.
qreal HexView::shadowStrength() const { return m_shadowStrength; }

// Property mutator for shadow strength with clamping and selection refresh.
void HexView::setShadowStrength(qreal s) {
  s = std::max<qreal>(0.0, s);
  if (qFuzzyCompare(s, m_shadowStrength)) return;
  m_shadowStrength = s;
  requestRhiUpdate(false, true);
}

// Configure highlight animation timeline for overlay, blur, and offset channels.
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

// Transition selection highlight visuals in/out, animated or immediate.
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

// Drop lingering fade-selection state after highlight animation completes.
void HexView::clearFadeSelection() {
  m_fadeSelections.clear();
  requestRhiUpdate(false, true);
}

// Resolve whether to show dim/shadow highlight based on selection state.
void HexView::updateHighlightState(bool animateSelection) {
  const bool hasSelection = !m_selections.empty() || !m_fadeSelections.empty();
  showSelectedItem(hasSelection, animateSelection);
}
