/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "HexView.h"
#include "Helpers.h"
#include "HexViewInput.h"
#include "HexViewRhiHost.h"
#include "util/NonTransientScrollBarStyle.h"
#include "services/NotificationCenter.h"
#include "VGMFile.h"

#include <QApplication>
#include <QBuffer>
#include <QCursor>
#include <QFontMetricsF>
#include <QHash>
#include <QHelpEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QRectF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimerEvent>
#include <QToolTip>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_set>

namespace {
constexpr int BYTES_PER_LINE = 16;
constexpr int NUM_ADDRESS_NIBBLES = 8;
constexpr int ADDRESS_SPACING_CHARS = 4;
constexpr int HEX_TO_ASCII_SPACING_CHARS = 4;
constexpr int SELECTION_PADDING = 18;
constexpr int VIEWPORT_PADDING = 10;
constexpr int DIM_DURATION_MS = 200;
constexpr int PLAYBACK_FADE_DURATION_MS = 300;
constexpr float PLAYBACK_FADE_CURVE = 3.0f;
constexpr int OVERLAY_ALPHA = 80;
constexpr float OVERLAY_ALPHA_F = OVERLAY_ALPHA / 255.0f;
constexpr float SHADOW_OFFSET_X = 0.0f;
constexpr float SHADOW_OFFSET_Y = 0.0f;
constexpr float SHADOW_BLUR_RADIUS = SELECTION_PADDING * 1.0f;
constexpr float SHADOW_STRENGTH = 0.5;
constexpr float SHADOW_EDGE_CURVE = 1.1f;
constexpr float PLAYBACK_GLOW_STRENGTH = 0.55f;
constexpr float PLAYBACK_GLOW_RADIUS = 1.8f;
constexpr float PLAYBACK_GLOW_EDGE_CURVE = 0.85f;
const QColor PLAYBACK_GLOW_LOW(40, 40, 40);
const QColor PLAYBACK_GLOW_HIGH(230, 230, 230);
constexpr uint16_t STYLE_UNASSIGNED = std::numeric_limits<uint16_t>::max();

QString tooltipIconDataUrl(VGMItem::Type type) {
  static QHash<int, QString> cache;
  const int key = static_cast<int>(type);
  auto it = cache.find(key);
  if (it != cache.end()) {
    return it.value();
  }
  const QIcon& icon = iconForItemType(type);
  const QPixmap pixmap = icon.pixmap(QSize(16, 16));
  if (pixmap.isNull()) {
    cache.insert(key, QString());
    return {};
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  pixmap.toImage().save(&buffer, "PNG");
  const QString dataUrl =
      QString("data:image/png;base64,%1").arg(QString::fromLatin1(bytes.toBase64()));
  cache.insert(key, dataUrl);
  return dataUrl;
}

QString tooltipHtmlWithIcon(VGMItem* item) {
  if (!item) {
    return {};
  }
  const QString iconData = tooltipIconDataUrl(item->type);
  const QString description = getFullDescriptionForTooltip(item);
  QString content = description;
  if (!iconData.isEmpty()) {
    content = QString("<table cellspacing=\"0\" cellpadding=\"0\">"
                      "<tr>"
                      "<td style=\"padding-right:6px; vertical-align:top;\">"
                      "<img src=\"%1\" width=\"16\" height=\"16\">"
                      "</td>"
                      "<td>%2</td>"
                      "</tr>"
                      "</table>")
                  .arg(iconData, description);
  }
  return QString("<table cellspacing=\"0\" cellpadding=\"6\"><tr><td>%1</td></tr></table>")
      .arg(content);
}

}  // namespace

// Pack selection offset/length into a stable 64-bit key for set membership checks.
uint64_t HexView::selectionKey(uint32_t offset, uint32_t length) {
  return (static_cast<uint64_t>(offset) << 32) | length;
}

// Overload for plain selection ranges.
uint64_t HexView::selectionKey(const SelectionRange& range) {
  return selectionKey(range.offset, range.length);
}

// Overload for fade playback selections (keyed by underlying range).
uint64_t HexView::selectionKey(const FadePlaybackSelection& selection) {
  return selectionKey(selection.range);
}

// Trivial destructor; QObject ownership handles child cleanup.
HexView::~HexView() = default;

QFont HexView::defaultViewFont() {
  const double appFontPointSize = QApplication::font().pointSizeF();
  QFont font("Roboto Mono", appFontPointSize + 1.0);
  font.setPointSizeF(appFontPointSize + 1.0);
  return font;
}

// Initialize HexView UI state, RHI host, typography, animations, and signal wiring.
HexView::HexView(VGMFile* vgmfile, QWidget* parent)
    : QAbstractScrollArea(parent), m_vgmfile(vgmfile) {
  setFocusPolicy(Qt::StrongFocus);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setAutoFillBackground(false);

#ifdef Q_OS_MAC
  // With AA_DontCreateNativeWidgetSiblings, the RHI QWindow can cover transient (overlay)
  // scrollbars, so keep HexView's scrollbars non-transient on macOS.
  QtUi::applyNonTransientScrollBarStyle(verticalScrollBar());
#endif

  m_rhiHost = new HexViewRhiHost(this, viewport());
  m_rhiHost->setGeometry(viewport()->rect());
  m_rhiHost->show();

  QFont font = defaultViewFont();
  setShadowStrength(SHADOW_STRENGTH);
  m_playbackGlowLow = PLAYBACK_GLOW_LOW;
  m_playbackGlowHigh = PLAYBACK_GLOW_HIGH;
  m_playbackGlowStrength = PLAYBACK_GLOW_STRENGTH;
  m_playbackGlowRadius = PLAYBACK_GLOW_RADIUS;
  m_shadowEdgeCurve = SHADOW_EDGE_CURVE;
  m_playbackGlowEdgeCurve = PLAYBACK_GLOW_EDGE_CURVE;

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

  connect(NotificationCenter::the(), &NotificationCenter::seekModifierChanged, this,
          [this](bool active) {
            if (m_seekModifierActive == active) {
              return;
            }
            m_seekModifierActive = active;
            m_outlineFadeClock.restart();
            if (!m_outlineFadeTimer.isActive()) {
              m_outlineFadeTimer.start(16, this);
            }
            if (m_isDragging) {
              hideTooltip();
              requestRhiUpdate();
              return;
            }
            if (active) {
              const QPoint vp = viewport()->mapFromGlobal(QCursor::pos());
              if (viewport()->rect().contains(vp)) {
                handleTooltipHoverMove(vp, Qt::KeyboardModifiers(HexViewInput::kModifier));
              } else {
                hideTooltip();
              }
            } else {
              hideTooltip();
            }
            requestRhiUpdate();
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

HexView::DragMode HexView::dragModeForModifiers(Qt::KeyboardModifiers mods) {
  return mods.testFlag(HexViewInput::kModifier) ? DragMode::SeekScrub : DragMode::Selection;
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

std::vector<SplitterSnapRange> HexView::splitterSnapRanges() const {
  return {
      {getViewportWidthSansAsciiAndAddress(), getViewportWidthSansAscii()},
      {getViewportWidthSansAscii(), getViewportFullWidth()},
  };
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
  if (m_playbackActive) {
    m_selections.clear();
    m_fadeSelections.clear();
    updateHighlightState(false);
  } else {
    if (!m_selections.empty()) {
      m_fadeSelections = m_selections;
    }
    m_selections.clear();
    showSelectedItem(false, animateSelection);
  }
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

// Refresh overlay/shadow animation state for current selection/playback state.
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

void HexView::setSelectedItems(const std::vector<const VGMItem*>& items,
                               const VGMItem* primaryItem) {
  if (items.empty()) {
    setSelectedItem(nullptr);
    return;
  }

  VGMItem* resolvedPrimary = const_cast<VGMItem*>(primaryItem);
  if (!resolvedPrimary) {
    for (const auto* item : items) {
      if (item) {
        resolvedPrimary = const_cast<VGMItem*>(item);
        break;
      }
    }
  }

  if (!resolvedPrimary) {
    setSelectedItem(nullptr);
    return;
  }

  m_selectedItem = resolvedPrimary;
  m_selectedOffset = m_selectedItem->offset();

  std::vector<SelectionRange> selections;
  selections.reserve(items.size());
  std::unordered_set<uint64_t> keys;
  keys.reserve(items.size() * 2 + 1);

  for (const auto* item : items) {
    if (!item) {
      continue;
    }
    const uint32_t length = item->length() > 0 ? item->length() : 1u;
    const SelectionRange range{item->offset(), length};
    if (keys.insert(selectionKey(range)).second) {
      selections.push_back(range);
    }
  }

  if (selections.empty()) {
    setSelectedItem(nullptr);
    return;
  }

  std::sort(selections.begin(), selections.end(), [](const SelectionRange& a, const SelectionRange& b) {
    if (a.offset != b.offset) {
      return a.offset < b.offset;
    }
    return a.length < b.length;
  });

  m_selections = std::move(selections);
  m_fadeSelections.clear();
  updateHighlightState(true);
  requestRhiUpdate(false, true);

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

// Update playback selections from active items and seed fade-out entries for removed ones.
void HexView::setPlaybackSelectionsForItems(const std::vector<const VGMItem*>& items) {
  std::vector<SelectionRange> next;
  next.reserve(items.size());
  for (const auto* item : items) {
    if (!item) {
      continue;
    }
    const uint32_t length = item->length() > 0 ? item->length() : 1u;
    next.push_back({item->offset(), length});
  }

  // Track incoming active playback ranges for fast membership checks below.
  std::unordered_set<uint64_t> nextKeys;
  nextKeys.reserve(next.size() * 2 + 1);
  for (const auto& selection : next) {
    nextKeys.insert(selectionKey(selection));
  }

  // If a range became active again, remove its stale fade entry.
  if (!m_fadePlaybackSelections.empty()) {
    m_fadePlaybackSelections.erase(
        std::remove_if(m_fadePlaybackSelections.begin(), m_fadePlaybackSelections.end(),
                       [&](const FadePlaybackSelection& selection) {
                         return nextKeys.find(selectionKey(selection)) != nextKeys.end();
                       }),
        m_fadePlaybackSelections.end());
  }

  // Seed fade entries only for ranges that were active and are now gone.
  std::unordered_set<uint64_t> fadeKeys;
  fadeKeys.reserve(m_fadePlaybackSelections.size() * 2 + 1);
  for (const auto& selection : m_fadePlaybackSelections) {
    fadeKeys.insert(selectionKey(selection));
  }

  const bool fadeEnabled = PLAYBACK_FADE_DURATION_MS > 0;
  bool addedFade = false;
  if (fadeEnabled) {
    const qint64 now = playbackNowMs();
    for (const auto& selection : m_playbackSelections) {
      const uint64_t key = selectionKey(selection);
      if (nextKeys.find(key) == nextKeys.end() && fadeKeys.insert(key).second) {
        m_fadePlaybackSelections.push_back({selection, now, 1.0f});
        addedFade = true;
      }
    }
  } else {
    m_fadePlaybackSelections.clear();
  }

  if (addedFade || !m_fadePlaybackSelections.empty()) {
    ensurePlaybackFadeTimer();
    updatePlaybackFade();
  } else {
    m_playbackFadeTimer.stop();
  }

  m_playbackSelections = std::move(next);
  refreshSelectionVisuals(false);
}

// Clear playback selection set immediately or convert it into fading playback highlights.
void HexView::clearPlaybackSelections(bool fade) {
  if (m_playbackSelections.empty()) {
    return;
  }
  if (fade && PLAYBACK_FADE_DURATION_MS > 0) {
    // Convert the current active set into fade-out entries in one step.
    const qint64 now = playbackNowMs();
    std::unordered_set<uint64_t> fadeKeys;
    fadeKeys.reserve(m_fadePlaybackSelections.size() * 2 + 1);
    for (const auto& selection : m_fadePlaybackSelections) {
      fadeKeys.insert(selectionKey(selection));
    }
    for (const auto& selection : m_playbackSelections) {
      const uint64_t key = selectionKey(selection);
      if (fadeKeys.insert(key).second) {
        m_fadePlaybackSelections.push_back({selection, now, 1.0f});
      }
    }
  } else {
    m_fadePlaybackSelections.clear();
  }
  m_playbackSelections.clear();
  if (!m_fadePlaybackSelections.empty()) {
    ensurePlaybackFadeTimer();
    updatePlaybackFade();
  } else {
    m_playbackFadeTimer.stop();
  }
  refreshSelectionVisuals(false);
}

// Toggle playback-highlight mode and reconcile existing playback selection state.
void HexView::setPlaybackActive(bool active) {
  if (m_playbackActive == active) {
    if (!active && !m_playbackSelections.empty()) {
      clearPlaybackSelections();
    }
    return;
  }
  m_playbackActive = active;
  if (!m_playbackActive && !m_playbackSelections.empty()) {
    clearPlaybackSelections();
    return;
  }
  refreshSelectionVisuals(false);
}

// Request another frame while playback/outline effects are animating.
void HexView::requestPlaybackFrame() {
  requestRhiUpdate();
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
      hideTooltip();
      return true;
    }
    if (VGMItem* item = m_vgmfile->getItemAtOffset(offset, false)) {
      showTooltip(item, helpEvent->pos());
    } else {
      hideTooltip();
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
  frame.seekModifierActive = m_seekModifierActive;

  frame.overlayOpacity = m_overlayOpacity;
  frame.shadowBlur = m_shadowBlur;
  frame.shadowStrength = m_shadowStrength;
  frame.shadowOffset = m_shadowOffset;
  frame.playbackGlowLow = m_playbackGlowLow;
  frame.playbackGlowHigh = m_playbackGlowHigh;
  frame.playbackGlowStrength = m_playbackGlowStrength;
  frame.playbackGlowRadius = m_playbackGlowRadius;
  frame.shadowEdgeCurve = m_shadowEdgeCurve;
  frame.playbackGlowEdgeCurve = m_playbackGlowEdgeCurve;

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

  frame.playbackSelections.reserve(m_playbackSelections.size());
  for (const auto& range : m_playbackSelections) {
    frame.playbackSelections.push_back({range.offset, range.length});
  }

  frame.fadePlaybackSelections.reserve(m_fadePlaybackSelections.size());
  for (const auto& fade : m_fadePlaybackSelections) {
    frame.fadePlaybackSelections.push_back(
        {{fade.range.offset, fade.range.length}, fade.alpha});
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

void HexView::handleSeekPress(VGMItem* item, const QPoint& pos) {
  if (item) {
    if (item != m_lastSeekItem) {
      m_lastSeekItem = item;
      seekToEventRequested(item);
      notePreviewRequested(item, true);
    }
    showTooltip(item, pos);
  } else {
    stopNotePreview();
    hideTooltip();
  }
}

void HexView::handleSelectionPress(int offset, VGMItem* item) {
  if (offset == -1) {
    selectionChanged(nullptr);
    stopNotePreview();
    return;
  }

  m_selectedOffset = offset;
  if (item == m_selectedItem) {
    selectionChanged(nullptr);
    stopNotePreview();
  } else {
    selectionChanged(item);
    notePreviewRequested(item, false);
  }
  hideTooltip();
}

void HexView::handleSeekScrubDrag(int offset) {
  if (offset >= 0) {
    if (auto* item = m_vgmfile->getItemAtOffset(offset, false)) {
      if (item != m_lastSeekItem) {
        m_lastSeekItem = item;
        seekToEventRequested(item);
        notePreviewRequested(item, true);
      }
    } else {
      stopNotePreview();
    }
  } else {
    stopNotePreview();
  }
  hideTooltip();
}

void HexView::handleSelectionDrag(int offset) {
  if (offset == -1) {
    selectionChanged(nullptr);
    stopNotePreview();
    hideTooltip();
    return;
  }

  m_selectedOffset = offset;
  if (m_selectedItem && (m_selectedOffset >= m_selectedItem->offset()) &&
      (m_selectedOffset < (m_selectedItem->offset() + m_selectedItem->length()))) {
    hideTooltip();
    return;
  }

  auto* item = m_vgmfile->getItemAtOffset(offset, false);
  if (item != m_selectedItem) {
    selectionChanged(item);
    notePreviewRequested(item, false);
  }
  hideTooltip();
}

// Handle click-to-select and modifier-drag seek behavior.
void HexView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const int offset = getOffsetFromPoint(event->pos());
    auto* item = m_vgmfile->getItemAtOffset(offset, false);
    const DragMode mode = dragModeForModifiers(event->modifiers());
    if (mode == DragMode::SeekScrub) {
      handleSeekPress(item, event->pos());
      m_isDragging = true;
      QAbstractScrollArea::mousePressEvent(event);
      return;
    }
    handleSelectionPress(offset, item);
    if (offset == -1) {
      return;
    }
    m_isDragging = true;
  }

  QAbstractScrollArea::mousePressEvent(event);
}

// End drag interactions and refresh hover tooltip state.
void HexView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
    stopNotePreview();
    m_lastSeekItem = nullptr;
    const QPoint vp = viewport()->mapFromGlobal(QCursor::pos());
    handleTooltipHoverMove(vp, QApplication::keyboardModifiers());
  }
  QAbstractScrollArea::mouseReleaseEvent(event);
}

// Process drag-move updates for selection changes or modifier-based seek scrubbing.
void HexView::handleCoalescedMouseMove(const QPoint& pos,
                              Qt::MouseButtons buttons,
                              Qt::KeyboardModifiers mods) {
  if (!(m_isDragging && (buttons & Qt::LeftButton))) {
    return;
  }

  const int offset = getOffsetFromPoint(pos);
  switch (dragModeForModifiers(mods)) {
    case DragMode::SeekScrub:
      handleSeekScrubDrag(offset);
      return;
    case DragMode::Selection:
      handleSelectionDrag(offset);
      return;
  }
}

// Show/hide tooltip while hovering with the seek modifier held.
void HexView::handleTooltipHoverMove(const QPoint& pos, Qt::KeyboardModifiers mods) {
  if (!mods.testFlag(HexViewInput::kModifier)) {
    hideTooltip();
    return;
  }
  const int offset = getOffsetFromPoint(pos);
  if (offset < 0) {
    hideTooltip();
    return;
  }
  if (auto* item = m_vgmfile->getItemAtOffset(offset, false)) {
    showTooltip(item, pos);
  } else {
    hideTooltip();
  }
}

// Route mouse movement to drag-selection logic or modifier hover tooltip logic.
void HexView::mouseMoveEvent(QMouseEvent* event) {
  if (event->buttons() & Qt::LeftButton) {
    handleCoalescedMouseMove(event->pos(), event->buttons(), event->modifiers());
  } else {
    handleTooltipHoverMove(event->pos(), event->modifiers());
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

// Ensure fade timer is running while playback fade-outs are active.
void HexView::ensurePlaybackFadeTimer() {
  if (!m_playbackFadeTimer.isActive()) {
    m_playbackFadeTimer.start(16, this);
  }
}

// Return monotonically increasing playback-fade clock in milliseconds.
qint64 HexView::playbackNowMs() {
  if (!m_playbackFadeClock.isValid()) {
    m_playbackFadeClock.start();
  }
  return m_playbackFadeClock.elapsed();
}

// Advance playback fade-out alphas and prune completed fade entries.
void HexView::updatePlaybackFade() {
  if (m_fadePlaybackSelections.empty()) {
    return;
  }
  const qint64 nowMs = playbackNowMs();
  const float duration = static_cast<float>(PLAYBACK_FADE_DURATION_MS);
  const float curve = std::max(0.01f, PLAYBACK_FADE_CURVE);

  // Decay alpha with a non-linear curve so fade starts strong and eases out.
  for (auto& selection : m_fadePlaybackSelections) {
    const qint64 elapsed = nowMs - selection.startMs;
    const float t = duration > 0.0f ? std::clamp(elapsed / duration, 0.0f, 1.0f) : 1.0f;
    const float inv = 1.0f - t;
    selection.alpha = inv > 0.0f ? std::pow(inv, curve) : 0.0f;
  }

  // Drop fully faded entries to keep the per-frame mask work bounded.
  m_fadePlaybackSelections.erase(
      std::remove_if(m_fadePlaybackSelections.begin(), m_fadePlaybackSelections.end(),
                     [](const FadePlaybackSelection& selection) {
                       return selection.alpha <= 0.0f;
                     }),
      m_fadePlaybackSelections.end());

  if (m_fadePlaybackSelections.empty() && m_playbackFadeTimer.isActive()) {
    m_playbackFadeTimer.stop();
  }
}

// Dispatch timer ticks for playback fade and outline-fade redraw scheduling.
void HexView::timerEvent(QTimerEvent* event) {
  if (event->timerId() == m_playbackFadeTimer.timerId()) {
    updatePlaybackFade();
    requestRhiUpdate(false, true);
    if (m_fadePlaybackSelections.empty()) {
      m_playbackFadeTimer.stop();
    }
    return;
  }
  if (event->timerId() == m_outlineFadeTimer.timerId()) {
    if (!m_outlineFadeClock.isValid()) {
      m_outlineFadeClock.start();
    }
    if (m_outlineFadeClock.elapsed() > OUTLINE_FADE_DURATION_MS) {
      m_outlineFadeTimer.stop();
      return;
    }
    requestRhiUpdate();
    return;
  }
  QAbstractScrollArea::timerEvent(event);
}

// Resolve whether to show dim/shadow highlight based on selection and playback state.
void HexView::updateHighlightState(bool animateSelection) {
  const bool hasSelection = !m_selections.empty() || !m_fadeSelections.empty();
  const bool hasPlayback = m_playbackActive;

  if (!hasSelection && !hasPlayback) {
    showSelectedItem(false, animateSelection);
    return;
  }

  if (hasSelection && !hasPlayback) {
    showSelectedItem(true, animateSelection);
    return;
  }

  if (m_selectionAnimation && m_selectionAnimation->state() != QAbstractAnimation::Stopped) {
    m_selectionAnimation->stop();
  }
  m_fadeSelections.clear();
  setOverlayOpacity(OVERLAY_ALPHA_F);
  setShadowBlur(SHADOW_BLUR_RADIUS);
  setShadowOffset(QPointF(SHADOW_OFFSET_X, SHADOW_OFFSET_Y));
}

void HexView::stopNotePreview() {
  emit notePreviewStopped();
  m_lastSeekItem = nullptr;
}

// Show rich HTML tooltip for a hovered item, avoiding redundant re-show for same item.
void HexView::showTooltip(VGMItem* item, const QPoint& pos) {
  if (!item) {
    hideTooltip();
    return;
  }
  if (m_tooltipItem == item) {
    return;
  }
  const QString description = tooltipHtmlWithIcon(item);
  if (description.isEmpty()) {
    hideTooltip();
    return;
  }
  QToolTip::showText(viewport()->mapToGlobal(pos), description, this);
  m_tooltipItem = item;
}

// Hide active tooltip and clear tracked tooltip item.
void HexView::hideTooltip() {
  if (m_tooltipItem == nullptr) {
    return;
  }
  QToolTip::hideText();
  m_tooltipItem = nullptr;
}
