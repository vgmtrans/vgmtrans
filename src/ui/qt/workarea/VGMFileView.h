/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "PlaybackPosition.h"
#include "value/base/CoreTypes.h"

#include <memory>
#include <vector>

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QWidget>

class HexView;
class SnappingSplitter;
class VGMFileTreeView;
namespace vgmtrans::ui {
class SourceInspectorModel;
class WorkspaceController;
}  // namespace vgmtrans::ui

// The existing two-pane file inspector, reworked directly around immutable
// value annotations instead of legacy VGMFile/VGMItem object graphs.
class VGMFileView final : public QWidget {
  Q_OBJECT

public:
  struct PlaybackAnnotationSpan {
    vgmtrans::core::SourceAnnotationId annotation;
    u32 trackIndex = 0;
    int startTick = 0;
    int endTick = 0;
  };

  explicit VGMFileView(vgmtrans::ui::WorkspaceController& workspace, vgmtrans::core::AssetId asset,
                       QWidget* parent = nullptr);
  ~VGMFileView() override;

  [[nodiscard]] bool valid() const;
  [[nodiscard]] vgmtrans::core::AssetId asset() const noexcept { return asset_; }

signals:
  void statusChanged(const QString& name, const QString& description, const QIcon& icon, int offset, int size);
  // Playback wiring can consume these once the value player publishes an
  // annotation-to-time index. Keeping IDs here avoids reintroducing adapters.
  void seekToAnnotationRequested(vgmtrans::core::SourceAnnotationId annotation);
  void notePreviewRequested(vgmtrans::core::SourceAnnotationId annotation, bool includeActiveNotesAtTick);
  void notePreviewStopped();
  void playbackSeekRequested(int position, PositionChangeOrigin origin);

public slots:
  void onSelectionChange(vgmtrans::core::SourceAnnotationId annotation);
  void seekToAnnotation(vgmtrans::core::SourceAnnotationId annotation);
  void setSeekModifierActive(bool active);
  void setPlaybackAnnotations(const std::vector<vgmtrans::core::SourceAnnotationId>& annotations,
                              const std::vector<QColor>& colors = {});
  void setPlaybackTimeline(std::vector<PlaybackAnnotationSpan> timeline);
  void onPlaybackPositionChanged(int current, int maximum, PositionChangeOrigin origin);
  void onPlayerStatusChanged(bool playing, bool hasActiveTarget);
  void clearPlaybackAnnotations(bool fade = true);
  void refreshStatus();
  void resetHexViewFont();
  void increaseHexViewFont();
  void decreaseHexViewFont();

protected:
  void focusInEvent(QFocusEvent* event) override;

private:
  static constexpr int treeViewMinimumWidth = 220;

  void resetSnapRanges() const;
  int hexViewFullWidth() const;
  int hexViewWidthSansAscii() const;
  int hexViewWidthSansAsciiAndAddress() const;
  void updateHexViewFont(qreal sizeIncrement) const;
  void applyHexViewFont(QFont font) const;
  void updateStatus(vgmtrans::core::SourceAnnotationId annotation);
  [[nodiscard]] std::vector<PlaybackAnnotationSpan> playbackSpansAt(int tick) const;
  [[nodiscard]] std::vector<PlaybackAnnotationSpan> playbackSpansInRange(int startTick, int endTick) const;

  vgmtrans::core::AssetId asset_;
  std::unique_ptr<vgmtrans::ui::SourceInspectorModel> model_;
  VGMFileTreeView* treeView_{};
  HexView* hexView_{};
  SnappingSplitter* splitter_{};
  QFont defaultHexFont_;
  std::vector<PlaybackAnnotationSpan> playbackTimeline_;
  std::vector<vgmtrans::core::SourceAnnotationId> lastPlaybackAnnotations_;
  std::vector<QColor> lastPlaybackColors_;
  int lastPlaybackPosition_ = 0;
};
