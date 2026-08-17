/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "PlaybackPosition.h"
#include "util/CapsuleText.h"
#include "value/model/SourceInspection.h"
#include "value/model/SessionSnapshot.h"
#include "value/sequence/PerformanceModel.h"

#include <memory>
#include <vector>

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QWidget>

class HexView;
class SnappingSplitter;
class VGMFileTreeView;

// The existing two-pane file inspector, reworked directly around immutable
// value annotations instead of legacy VGMFile/VGMItem object graphs.
class VGMFileView final : public QWidget {
  Q_OBJECT

public:
  explicit VGMFileView(std::shared_ptr<const vgmtrans::core::SourceInspection> inspection,
                       const vgmtrans::core::Asset& asset,
                       QWidget* parent = nullptr);

  [[nodiscard]] vgmtrans::core::AssetId asset() const noexcept { return inspection_->asset(); }

signals:
  void statusChanged(const QString& name, const CapsuleText& description, const QIcon& icon,
                     int offset, int size);
  // Inspector interactions stay source-ID based; the playback timeline resolves
  // seek requests to ticks without introducing a model adapter.
  void seekToAnnotationRequested(vgmtrans::core::SourceAnnotationId annotation);
  void notePreviewRequested(vgmtrans::core::SourceAnnotationId annotation, bool includeActiveNotesAtTick);
  void notePreviewStopped();
  void playbackSeekRequested(int position, PositionChangeOrigin origin);

public slots:
  void onSelectionChange(vgmtrans::core::SourceInspectionItem item);
  void seekToAnnotation(vgmtrans::core::SourceAnnotationId annotation);
  void setSeekModifierActive(bool active);
  void setPlaybackTimeline(std::vector<vgmtrans::core::SourcePlaybackSpan> timeline);
  void onPlaybackPositionChanged(int current, int maximum, PositionChangeOrigin origin);
  void refreshStatus();
  void resetHexViewFont();
  void increaseHexViewFont();
  void decreaseHexViewFont();

protected:
  void focusInEvent(QFocusEvent* event) override;

private:
  static constexpr int treeViewMinimumWidth = 220;

  void resetSnapRanges() const;
  void updateHexViewFont(qreal sizeIncrement) const;
  void applyHexViewFont(QFont font) const;
  void updateStatus(vgmtrans::core::SourceInspectionItem item);
  void setPlaybackAnnotations(const std::vector<vgmtrans::core::SourceAnnotationId>& annotations,
                              const std::vector<QColor>& colors = {});
  void clearPlaybackAnnotations(bool fade = true);
  [[nodiscard]] u32 playbackTrackIndex(vgmtrans::core::SourceAnnotationId annotation) const;
  [[nodiscard]] std::vector<vgmtrans::core::SourcePlaybackSpan> playbackSpansInRange(int startTick,
                                                                                    int endTick) const;

  std::shared_ptr<const vgmtrans::core::SourceInspection> inspection_;
  VGMFileTreeView* treeView_{};
  HexView* hexView_{};
  SnappingSplitter* splitter_{};
  QFont defaultHexFont_;
  std::vector<vgmtrans::core::SourcePlaybackSpan> playbackTimeline_;
  std::vector<vgmtrans::core::SourceAnnotationId> lastPlaybackAnnotations_;
  std::vector<QColor> lastPlaybackColors_;
  int lastPlaybackPosition_ = 0;
};
