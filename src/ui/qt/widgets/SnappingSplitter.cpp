/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "SnappingSplitter.h"
#include <QMouseEvent>

SnappingSplitter::SnappingSplitter(Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent) {
  connect(this, &SnappingSplitter::splitterMoved, this, &SnappingSplitter::onSplitterMoved);
}

void SnappingSplitter::onSplitterMoved() {
  printf("splitterMoved\n");
  enforceConfigurations();
  state = saveState();
}

void SnappingSplitter::resizeEvent(QResizeEvent* event) {
  QSplitter::resizeEvent(event);
  restoreState(state);
  enforceConfigurationsOnResize();
}

void SnappingSplitter::enforceConfigurations() {
  for (const CollapseRange& range : collapseRanges) {
    QList<int> sizes = this->sizes();

    int halfway = range.upperBound + (range.lowerBound - range.upperBound) / 2;
    if (sizes[range.index] <= range.upperBound && sizes[range.index] > range.lowerBound) {
      if (sizes[range.index] > halfway) {
        // Hold the size at upperBound until halfway point is crossed
        setSizesToUpperBound(range.index, range.upperBound);
      } else if (sizes[range.index] <= halfway) {
        // Collapse when halfway point is crossed
        setSizesToLowerBound(range.index, range.lowerBound);
      }
    }
  }
}

// On a resize event, we immediately collapse when the size is in range, because minimum
// width settings on the other view may otherwise force sizing within the collapsed range
void SnappingSplitter::enforceConfigurationsOnResize() {
  for (const CollapseRange& range : collapseRanges) {
    QList<int> sizes = this->sizes();

    if (sizes[range.index] < range.upperBound && sizes[range.index] > range.lowerBound) {
        setSizesToLowerBound(range.index, range.lowerBound);
    }
  }
}

void SnappingSplitter::setSizesToUpperBound(int index, int threshold) {
  QList<int> newSizes = this->sizes();
  newSizes[index] = threshold;
  newSizes[!index] = this->size().width() - threshold - this->handleWidth();
  this->setSizes(newSizes);
}

void SnappingSplitter::setSizesToLowerBound(int index, int collapsePoint) {
  QList<int> newSizes = this->sizes();
  newSizes[index] = collapsePoint;
  newSizes[!index] = this->size().width() - collapsePoint - this->handleWidth();
  this->setSizes(newSizes);
}

void SnappingSplitter::addCollapseRange(int index, int lowerBound, int upperBound) {
  collapseRanges.append({index, lowerBound, upperBound});
}

void SnappingSplitter::clearCollapseRanges() {
  collapseRanges.clear();
}

void SnappingSplitter::persistState() {
  state = saveState();
}