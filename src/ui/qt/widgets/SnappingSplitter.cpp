/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "SnappingSplitter.h"

SnappingSplitter::SnappingSplitter(Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent) {
  connect(this, &SnappingSplitter::splitterMoved, this, &SnappingSplitter::onSplitterMoved);
}

void SnappingSplitter::onSplitterMoved() {
  enforceSnapRanges();
  persistState();
}

void SnappingSplitter::resizeEvent(QResizeEvent* event) {
  QSplitter::resizeEvent(event);
  restoreState(state);
  enforceSnapRangesOnResize();
}

void SnappingSplitter::enforceSnapRanges() {
  for (const SnapRange& range : snapRanges) {
    QList<int> sizes = this->sizes();

    int halfway = range.upperBound + (range.lowerBound - range.upperBound) / 2;
    if (sizes[range.index] <= range.upperBound && sizes[range.index] > range.lowerBound) {
      if (sizes[range.index] > halfway) {
        // Hold the size at upperBound until halfway point is crossed
        setSizesToBound(Upper, range);
      } else if (sizes[range.index] <= halfway) {
        // Collapse when halfway point is crossed
        setSizesToBound(Lower, range);
      }
    }
  }
}

// On a resize event, we immediately collapse when the size is in range, because minimum
// width settings on the other view may otherwise force sizing within the collapsed range
void SnappingSplitter::enforceSnapRangesOnResize() {
  for (const SnapRange& range : snapRanges) {
    QList<int> sizes = this->sizes();

    if (sizes[range.index] < range.upperBound && sizes[range.index] > range.lowerBound) {
        setSizesToBound(Lower, range);
    }
  }
}

void SnappingSplitter::setSizesToBound(Bound bound, const SnapRange& range) {
  QList<int> newSizes = this->sizes();

  int targetSize = bound == Upper ? range.upperBound : range.lowerBound;
  int fallbackSize = bound == Upper ? range.lowerBound : range.upperBound;

  newSizes[range.index] = targetSize;
  newSizes[!range.index] = this->size().width() - targetSize - this->handleWidth();
  this->setSizes(newSizes);

  // If the new size couldn't be realized (likely due to min size of opposite index), reverse it
  if (sizes()[range.index] != targetSize) {
    newSizes[range.index] = fallbackSize;
    newSizes[!range.index] = this->size().width() - fallbackSize - this->handleWidth();
    this->setSizes(newSizes);
  }
}

void SnappingSplitter::addSnapRange(int index, int lowerBound, int upperBound) {
  snapRanges.append({index, lowerBound, upperBound});
}

void SnappingSplitter::clearSnapRanges() {
  snapRanges.clear();
}

void SnappingSplitter::persistState() {
  state = saveState();
}