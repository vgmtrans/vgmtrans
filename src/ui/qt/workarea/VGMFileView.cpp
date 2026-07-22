/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMFileView.h"

#include "hexview/HexView.h"
#include "SnappingSplitter.h"
#include "VGMFileTreeView.h"
#include "workarea/SourceInspectorPresentation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

#include <QFocusEvent>
#include <QFontMetricsF>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace {

int boundedInt(u64 value) {
  return static_cast<int>(std::min<u64>(value, std::numeric_limits<int>::max()));
}

}  // namespace

VGMFileView::VGMFileView(std::shared_ptr<const vgmtrans::core::SourceInspection> inspection, QWidget* parent)
    : QWidget(parent), inspection_(std::move(inspection)) {
  Q_ASSERT(inspection_);
  setWindowTitle(QString::fromStdString(inspection_->metadata().name));
  setAttribute(Qt::WA_DeleteOnClose);
  setCursor(Qt::ArrowCursor);

  splitter_ = new SnappingSplitter(Qt::Horizontal, this);
  hexView_ = new HexView(inspection_, splitter_);
  treeView_ = new VGMFileTreeView(inspection_, splitter_);

  hexView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  splitter_->addWidget(hexView_);
  splitter_->addWidget(treeView_);
  splitter_->setSizes(QList<int>{hexView_->getViewportFullWidth(), treeViewMinimumWidth});
  splitter_->setStretchFactor(0, 0);
  splitter_->setStretchFactor(1, 1);
  splitter_->persistState();
  resetSnapRanges();
  hexView_->setMaximumWidth(hexView_->getViewportFullWidth());
  treeView_->setMinimumWidth(treeViewMinimumWidth);
  defaultHexFont_ = hexView_->font();

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(splitter_);

  connect(hexView_, &HexView::selectionChanged, this, &VGMFileView::onSelectionChange);
  connect(hexView_, &HexView::seekToEventRequested, this, &VGMFileView::seekToAnnotation);
  connect(hexView_, &HexView::notePreviewRequested, this, &VGMFileView::notePreviewRequested);
  connect(hexView_, &HexView::notePreviewStopped, this, &VGMFileView::notePreviewStopped);
  connect(treeView_, &VGMFileTreeView::seekToAnnotationRequested, this, &VGMFileView::seekToAnnotation);
  connect(treeView_, &VGMFileTreeView::statusAnnotationChanged, this, &VGMFileView::updateStatus);
  connect(treeView_, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* item, QTreeWidgetItem*) { onSelectionChange(treeView_->annotationForItem(item)); });

  connect(new QShortcut(QKeySequence::ZoomIn, this), &QShortcut::activated, this, &VGMFileView::increaseHexViewFont);
  connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), this), &QShortcut::activated, this,
          &VGMFileView::increaseHexViewFont);
  connect(new QShortcut(QKeySequence::ZoomOut, this), &QShortcut::activated, this, &VGMFileView::decreaseHexViewFont);
  connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this), &QShortcut::activated, this,
          &VGMFileView::resetHexViewFont);
}

void VGMFileView::focusInEvent(QFocusEvent* event) {
  QWidget::focusInEvent(event);
  treeView_->updateStatusBar();
}

void VGMFileView::resetSnapRanges() const {
  splitter_->clearSnapRanges();
  for (const auto range : hexView_->splitterSnapRanges()) {
    splitter_->addSnapRange(0, range.lowerBound, range.upperBound);
  }
}

void VGMFileView::updateHexViewFont(qreal sizeIncrement) const {
  QFont font = hexView_->font();
  QFontMetricsF metrics(font);
  const qreal originalWidth = metrics.horizontalAdvance("A");
  qreal fontSize = font.pointSizeF();
  for (int index = 0; index < 3; ++index) {
    fontSize += sizeIncrement;
    font.setPointSizeF(fontSize);
    metrics = QFontMetricsF(font);
    if (!qFuzzyCompare(metrics.horizontalAdvance("A"), originalWidth)) {
      break;
    }
  }
  applyHexViewFont(font);
}

void VGMFileView::applyHexViewFont(QFont font) const {
  const QList<int> splitterSizes = splitter_->sizes();
  const int actualWidthBeforeResize =
      splitterSizes.isEmpty() ? hexView_->getViewportFullWidth() : splitterSizes.first();
  const int fullWidthBeforeResize = std::max(1, hexView_->getViewportFullWidth());

  hexView_->setFont(font);
  hexView_->setMaximumWidth(hexView_->getViewportFullWidth());

  const float percentVisible = static_cast<float>(actualWidthBeforeResize) / static_cast<float>(fullWidthBeforeResize);
  const int fullWidthAfterResize = std::max(1, hexView_->getViewportFullWidth());
  const int widthChange = fullWidthAfterResize - fullWidthBeforeResize;
  const int scaledWidthChange = static_cast<int>(std::round(static_cast<float>(widthChange) * percentVisible));
  const int newWidth = std::max(1, actualWidthBeforeResize + scaledWidthChange);
  resetSnapRanges();
  splitter_->setSizes(QList<int>{newWidth, treeViewMinimumWidth});
  splitter_->persistState();
}

void VGMFileView::resetHexViewFont() {
  applyHexViewFont(defaultHexFont_);
}

void VGMFileView::increaseHexViewFont() {
  updateHexViewFont(+0.5);
}

void VGMFileView::decreaseHexViewFont() {
  updateHexViewFont(-0.5);
}

void VGMFileView::onSelectionChange(vgmtrans::core::SourceAnnotationId annotation) {
  hexView_->setSelectedAnnotation(annotation);
  const QSignalBlocker blocker(treeView_);
  treeView_->setSelectedAnnotation(annotation);
  updateStatus(annotation);
}

void VGMFileView::seekToAnnotation(vgmtrans::core::SourceAnnotationId annotation) {
  if (!annotation.valid()) {
    return;
  }
  emit seekToAnnotationRequested(annotation);
  const auto found = std::ranges::find_if(
      playbackTimeline_, [annotation](const auto& span) { return span.annotation == annotation; });
  if (found != playbackTimeline_.end()) {
    emit playbackSeekRequested(boundedInt(found->beginTick), PositionChangeOrigin::HexView);
  }
}

void VGMFileView::setSeekModifierActive(bool active) {
  hexView_->setSeekModifierActive(active);
}

void VGMFileView::setPlaybackAnnotations(const std::vector<vgmtrans::core::SourceAnnotationId>& annotations,
                                         const std::vector<QColor>& colors) {
  hexView_->setPlaybackActive(!annotations.empty());
  hexView_->setPlaybackSelectionsForAnnotations(annotations, colors);
  treeView_->setPlaybackAnnotations(annotations);
}

void VGMFileView::setPlaybackTimeline(std::vector<vgmtrans::core::SourcePlaybackSpan> timeline) {
  playbackTimeline_ = std::move(timeline);
  lastPlaybackAnnotations_.clear();
  lastPlaybackColors_.clear();
  lastPlaybackPosition_ = 0;
  clearPlaybackAnnotations(false);
}

void VGMFileView::onPlaybackPositionChanged(int current, int maximum, PositionChangeOrigin origin) {
  if (!isVisible() || playbackTimeline_.empty()) {
    return;
  }

  const int tickDifference = current - lastPlaybackPosition_;
  std::vector<vgmtrans::core::SourcePlaybackSpan> spans;
  switch (origin) {
    case PositionChangeOrigin::Playback:
      spans = tickDifference >= 0 && tickDifference <= 20
                  ? playbackSpansInRange(lastPlaybackPosition_, current)
                  : playbackSpansInRange(current, current);
      break;
    case PositionChangeOrigin::SeekBar:
      if (tickDifference >= -500 && tickDifference <= 500) {
        spans =
            playbackSpansInRange(std::min(lastPlaybackPosition_, current), std::max(lastPlaybackPosition_, current));
        // Passed-through events remain visible for this frame. The immediate
        // HexView-origin refresh retains only events active at the destination,
        // allowing the rest to use the renderer's existing fade path.
        QTimer::singleShot(0, this, [this, current, maximum] {
          onPlaybackPositionChanged(current, maximum, PositionChangeOrigin::HexView);
        });
      } else {
        spans = playbackSpansInRange(current, current);
      }
      break;
    case PositionChangeOrigin::HexView:
      spans = playbackSpansInRange(current, current);
      break;
  }
  lastPlaybackPosition_ = current;

  std::vector<vgmtrans::core::SourceAnnotationId> annotations;
  std::vector<QColor> colors;
  annotations.reserve(spans.size());
  colors.reserve(spans.size());
  std::unordered_set<u32> seen;
  for (const auto& span : spans) {
    if (!span.annotation.valid() || !seen.insert(span.annotation.value).second) {
      continue;
    }
    annotations.push_back(span.annotation);
    colors.push_back(
        QColor::fromHsv(static_cast<int>((playbackTrackIndex(span.annotation) * 43) % 360), 190, 235));
  }

  if (annotations == lastPlaybackAnnotations_ && colors == lastPlaybackColors_) {
    hexView_->requestPlaybackFrame();
    return;
  }
  lastPlaybackAnnotations_ = annotations;
  lastPlaybackColors_ = colors;
  setPlaybackAnnotations(annotations, colors);
}

void VGMFileView::clearPlaybackAnnotations(bool fade) {
  hexView_->clearPlaybackSelections(fade);
  hexView_->setPlaybackActive(false);
  treeView_->setPlaybackAnnotations({});
}

void VGMFileView::refreshStatus() {
  treeView_->updateStatusBar();
}

u32 VGMFileView::playbackTrackIndex(
    vgmtrans::core::SourceAnnotationId annotationId) const {
  while (annotationId.valid()) {
    const auto* annotation = inspection_->annotation(annotationId);
    if (annotation == nullptr) {
      break;
    }
    if (annotation->owner &&
        annotation->owner->kind == vgmtrans::core::ObjectKind::SequenceTrack) {
      return annotation->owner->index0;
    }
    annotationId = annotation->parent.value_or(vgmtrans::core::SourceAnnotationId{});
  }
  return 0;
}

std::vector<vgmtrans::core::SourcePlaybackSpan>
VGMFileView::playbackSpansInRange(int startTick, int endTick) const {
  if (endTick < startTick) {
    std::swap(startTick, endTick);
  }
  const u64 firstTick = static_cast<u64>(std::max(0, startTick));
  const u64 lastTick = static_cast<u64>(std::max(0, endTick));
  std::vector<vgmtrans::core::SourcePlaybackSpan> result;
  for (const auto& span : playbackTimeline_) {
    if (span.beginTick > lastTick) {
      break;
    }
    if (span.endTick > firstTick) {
      result.push_back(span);
    }
  }
  return result;
}

void VGMFileView::updateStatus(vgmtrans::core::SourceAnnotationId annotationId) {
  const auto* annotation = inspection_->annotation(annotationId);
  if (annotation == nullptr) {
    emit statusChanged({}, {}, {}, -1, -1);
    return;
  }
  emit statusChanged(QStringLiteral("<b>%1</b>").arg(QString::fromStdString(annotation->label)),
                     SourceInspectorPresentation::description(*annotation),
                     SourceInspectorPresentation::icon(*annotation), boundedInt(annotation->range.offset),
                     boundedInt(annotation->range.size));
}
