/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include "HexViewFrameData.h"
#include "value/model/SourceInspection.h"
#include "workarea/SplitterSnapProvider.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <QAbstractScrollArea>
#include <QBasicTimer>
#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>

class QParallelAnimationGroup;
class QWidget;
class HexViewRhiHost;

static constexpr int OUTLINE_FADE_DURATION_MS = 150;

class HexView final : public QAbstractScrollArea, public SplitterSnapProvider {
  Q_OBJECT
  Q_PROPERTY(qreal overlayOpacity READ overlayOpacity WRITE setOverlayOpacity)
  Q_PROPERTY(qreal shadowBlur READ shadowBlur WRITE setShadowBlur)
  Q_PROPERTY(QPointF shadowOffset READ shadowOffset WRITE setShadowOffset)
  Q_PROPERTY(qreal shadowStrength READ shadowStrength WRITE setShadowStrength)

public:
  explicit HexView(std::shared_ptr<const vgmtrans::core::SourceInspection> inspection,
                   QWidget* parent = nullptr);
  ~HexView() override;
  [[nodiscard]] static QFont defaultViewFont();
  void setSelectedItem(vgmtrans::core::SourceInspectionItem item);
  void setSelectedAnnotation(vgmtrans::core::SourceAnnotationId annotation);
  void setSelectedAnnotations(
      const std::vector<vgmtrans::core::SourceAnnotationId>& annotations,
      vgmtrans::core::SourceAnnotationId primaryAnnotation = {});
  void setPlaybackSelectionsForAnnotations(
      const std::vector<vgmtrans::core::SourceAnnotationId>& annotations,
      const std::vector<QColor>& glowColors = {});
  void clearPlaybackSelections(bool fade = true);
  void setPlaybackActive(bool active);
  void setSeekModifierActive(bool active);
  void requestPlaybackFrame();
  int scrollYForRender() const;
  void setFont(const QFont& font);
  [[nodiscard]] int getVirtualFullWidth() const;
  [[nodiscard]] int getVirtualWidthSansAscii() const;
  [[nodiscard]] int getVirtualWidthSansAsciiAndAddress() const;
  [[nodiscard]] int getActualVirtualWidth() const;
  [[nodiscard]] int getViewportFullWidth() const;
  [[nodiscard]] int getViewportWidthSansAscii() const;
  [[nodiscard]] int getViewportWidthSansAsciiAndAddress() const;
  [[nodiscard]] std::vector<SplitterSnapRange> splitterSnapRanges() const override;
  HexViewFrame::Data captureRhiFrameData(float dpr);

  void handleCoalescedMouseMove(const QPoint& pos,
                                Qt::MouseButtons buttons,
                                Qt::KeyboardModifiers mods);
  void handleTooltipHoverMove(const QPoint& pos, Qt::KeyboardModifiers mods);

signals:
  void selectionChanged(vgmtrans::core::SourceInspectionItem item);
  void seekToEventRequested(vgmtrans::core::SourceAnnotationId annotation);
  void notePreviewRequested(vgmtrans::core::SourceAnnotationId annotation,
                            bool includeActiveNotesAtTick);
  void notePreviewStopped();

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
  using SelectionRange = HexViewFrame::SelectionRange;
  using PlaybackSelection = HexViewFrame::PlaybackSelection;
  using FadePlaybackSelection = HexViewFrame::FadePlaybackSelection;
  using Style = HexViewFrame::Style;
  enum class DragMode {
    Selection,
    SeekScrub,
  };
  struct GlyphAtlas {
    QImage image;
    std::array<QRectF, 128> uvTable{};
    qreal dpr = 0.0;
    int glyphWidth = 0;
    int glyphHeight = 0;
    u64 version = 0;
    QFont font;
  };

  static u64 selectionKey(u32 offset, u32 length);
  static u64 selectionKey(const SelectionRange& range);
  static u64 selectionKey(const PlaybackSelection& range);
  static u64 selectionKey(const FadePlaybackSelection& selection);

  int hexXOffset() const;
  static DragMode dragModeForModifiers(Qt::KeyboardModifiers mods);
  int getVirtualHeight() const;
  int getTotalLines() const;
  int getOffsetFromPoint(QPoint pos) const;
  void handleSelectionPress(int offset, vgmtrans::core::SourceInspectionItem item);
  void handleSeekPress(vgmtrans::core::SourceInspectionItem item, const QPoint& pos);
  void handleSelectionDrag(int offset);
  void handleSeekScrubDrag(int offset);
  void requestRhiUpdate(bool markBaseDirty = false,
                        bool markSelectionDirty = false,
                        bool markPlaybackDirty = false);
  void clearCurrentSelection(bool animateSelection);
  void scrollRangeIntoView(SelectionRange range);
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
  void showTooltip(vgmtrans::core::SourceInspectionItem item, const QPoint& pos);
  void hideTooltip();
  void stopNotePreview();
  [[nodiscard]] const vgmtrans::core::SourceAnnotation* annotation(
      vgmtrans::core::SourceAnnotationId id) const;
  [[nodiscard]] vgmtrans::core::SourceInspectionItem itemAt(u32 offset) const;
  [[nodiscard]] std::optional<SelectionRange> visibleRange(vgmtrans::core::SourceRange range) const;

  std::shared_ptr<const vgmtrans::core::SourceInspection> m_inspection;
  // Interaction state.
  vgmtrans::core::SourceInspectionItem m_selectedItem;
  u32 m_selectedOffset = 0;
  bool m_isDragging = false;
  bool m_seekModifierActive = false;
  vgmtrans::core::SourceInspectionItem m_tooltipItem;
  vgmtrans::core::SourceAnnotationId m_lastSeekAnnotation;
  std::vector<SelectionRange> m_selections;
  std::vector<SelectionRange> m_fadeSelections;
  std::vector<PlaybackSelection> m_playbackSelections;
  std::vector<FadePlaybackSelection> m_fadePlaybackSelections;
  bool m_playbackActive = false;

  int m_charWidth = 0;
  int m_lineHeight = 0;
  bool m_addressAsHex = true;
  bool m_shouldDrawOffset = true;
  bool m_shouldDrawAscii = true;

  // Compact style table used by renderer; index 0 is the default/fallback style.
  std::vector<Style> m_styles;
  // Style id for each byte in the current file data; each entry indexes into m_styles.
  std::vector<u16> m_styleIds;
  std::vector<u16> m_itemIds;

  QParallelAnimationGroup* m_selectionAnimation = nullptr;
  qreal m_overlayOpacity = 0.0;
  qreal m_shadowBlur = 0.0;
  QPointF m_shadowOffset{0.0, 0.0};
  qreal m_shadowStrength = 1.0;
  QElapsedTimer m_playbackFadeClock;
  QBasicTimer m_playbackFadeTimer;
  QBasicTimer m_outlineFadeTimer;
  QElapsedTimer m_outlineFadeClock;
  float m_playbackGlowStrength = 1.0f;
  float m_playbackGlowRadius = 0.5f;
  float m_shadowEdgeCurve = 1.0f;
  float m_playbackGlowEdgeCurve = 1.0f;
  bool m_scrollBarDragging = false;
  int m_pendingScrollY = 0;

  HexViewRhiHost* m_rhiHost = nullptr;
  GlyphAtlas m_glyphAtlas;
};
