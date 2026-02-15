/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QAbstractScrollArea>
#include <QBasicTimer>
#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QSize>
#include <QPointF>
#include <QRectF>
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include "HexViewFrameData.h"

class QParallelAnimationGroup;
class QWidget;
class VGMFile;
class VGMItem;
class HexViewRhiHost;

class HexView final : public QAbstractScrollArea {
  Q_OBJECT
  Q_PROPERTY(qreal overlayOpacity READ overlayOpacity WRITE setOverlayOpacity)
  Q_PROPERTY(qreal shadowBlur READ shadowBlur WRITE setShadowBlur)
  Q_PROPERTY(QPointF shadowOffset READ shadowOffset WRITE setShadowOffset)
  Q_PROPERTY(qreal shadowStrength READ shadowStrength WRITE setShadowStrength)

public:
  explicit HexView(VGMFile* vgmfile, QWidget* parent = nullptr);
  ~HexView() override;
  void setSelectedItem(VGMItem* item);
  void setPlaybackSelectionsForItems(const std::vector<const VGMItem*>& items);
  void clearPlaybackSelections(bool fade = true);
  void setPlaybackActive(bool active);
  int scrollYForRender() const;
  void setFont(const QFont& font);
  [[nodiscard]] int getVirtualFullWidth() const;
  [[nodiscard]] int getVirtualWidthSansAscii() const;
  [[nodiscard]] int getVirtualWidthSansAsciiAndAddress() const;
  [[nodiscard]] int getActualVirtualWidth() const;
  [[nodiscard]] int getViewportFullWidth() const;
  [[nodiscard]] int getViewportWidthSansAscii() const;
  [[nodiscard]] int getViewportWidthSansAsciiAndAddress() const;
  HexViewFrame::Data captureRhiFrameData(float dpr);

  void handleCoalescedMouseMove(const QPoint& pos,
                                Qt::MouseButtons buttons,
                                Qt::KeyboardModifiers mods);

signals:
  void selectionChanged(VGMItem* item);
  void seekToEventRequested(VGMItem* item);

protected:
  bool viewportEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void scrollContentsBy(int dx, int dy) override;
  void changeEvent(QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void timerEvent(QTimerEvent* event) override;

private:
  struct SelectionRange {
    uint32_t offset;
    uint32_t length;
  };
  struct FadePlaybackSelection {
    SelectionRange range;
    qint64 startMs = 0;
    float alpha = 0.0f;
  };
  struct Style {
    QColor bg;
    QColor fg;
  };
  struct GlyphAtlas {
    QImage image;
    std::array<QRectF, 128> uvTable{};
    qreal dpr = 0.0;
    int glyphWidth = 0;
    int glyphHeight = 0;
    int cellWidth = 0;
    int cellHeight = 0;
    uint64_t version = 0;
    QFont font;
  };

  static uint64_t selectionKey(uint32_t offset, uint32_t length);
  static uint64_t selectionKey(const SelectionRange& range);
  static uint64_t selectionKey(const FadePlaybackSelection& selection);

  int hexXOffset() const;
  int getVirtualHeight() const;
  int getTotalLines() const;
  int getOffsetFromPoint(QPoint pos) const;
  void requestRhiUpdate(bool markBaseDirty = false, bool markSelectionDirty = false);
  void clearCurrentSelection(bool animateSelection);
  void selectCurrentItem(bool animateSelection);
  void refreshSelectionVisuals(bool animateSelection);
  void updateLayout();
  void updateScrollBars();
  void rebuildStyleMap();
  void ensureGlyphAtlas(qreal dpr);
  qreal overlayOpacity() const;
  void setOverlayOpacity(qreal opacity);
  qreal shadowBlur() const;
  void setShadowBlur(qreal blur);
  QPointF shadowOffset() const;
  void setShadowOffset(const QPointF& offset);
  qreal shadowStrength() const;
  void setShadowStrength(qreal s);
  void initAnimations();
  void showSelectedItem(bool show, bool animate);
  void clearFadeSelection();
  void updatePlaybackFade();
  void ensurePlaybackFadeTimer();
  qint64 playbackNowMs();
  void updateHighlightState(bool animateSelection);

  VGMFile* m_vgmfile = nullptr;
  // Interaction state.
  VGMItem* m_selectedItem = nullptr;
  uint32_t m_selectedOffset = 0;
  bool m_isDragging = false;
  bool m_seekModifierActive = false;
  VGMItem* m_lastSeekItem = nullptr;
  std::vector<SelectionRange> m_selections;
  std::vector<SelectionRange> m_fadeSelections;
  std::vector<SelectionRange> m_playbackSelections;
  std::vector<FadePlaybackSelection> m_fadePlaybackSelections;
  bool m_playbackActive = false;

  int m_charWidth = 0;
  int m_charHalfWidth = 0;
  int m_lineHeight = 0;
  bool m_addressAsHex = true;
  bool m_shouldDrawOffset = true;
  bool m_shouldDrawAscii = true;

  mutable int m_virtual_full_width = -1;
  mutable int m_virtual_width_sans_ascii = -1;
  mutable int m_virtual_width_sans_ascii_and_address = -1;

  // Compact style table used by renderer; index 0 is the default/fallback style.
  std::vector<Style> m_styles;
  // Style id for each byte in the current file data; each entry indexes into m_styles.
  std::vector<uint16_t> m_styleIds;
  std::unordered_map<int, uint16_t> m_typeToStyleId;

  QParallelAnimationGroup* m_selectionAnimation = nullptr;
  qreal m_overlayOpacity = 0.0;
  qreal m_shadowBlur = 0.0;
  QPointF m_shadowOffset{0.0, 0.0};
  qreal m_shadowStrength = 1.0;
  QElapsedTimer m_playbackFadeClock;
  QBasicTimer m_playbackFadeTimer;
  QColor m_playbackGlowLow;
  QColor m_playbackGlowHigh;
  float m_playbackGlowStrength = 1.0f;
  float m_playbackGlowRadius = 0.5f;
  float m_shadowEdgeCurve = 1.0f;
  float m_playbackGlowEdgeCurve = 1.0f;
  bool m_scrollBarDragging = false;
  int m_pendingScrollY = 0;

  HexViewRhiHost* m_rhiHost = nullptr;
  std::unique_ptr<GlyphAtlas> m_glyphAtlas;
};
