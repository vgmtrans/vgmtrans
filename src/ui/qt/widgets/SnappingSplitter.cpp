/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "SnappingSplitter.h"
#include <QMouseEvent>
#include <QScrollArea>

SnappingSplitter::SnappingSplitter(Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent) {
  connect(this, &SnappingSplitter::splitterMoved, this, &SnappingSplitter::onSplitterMoved);
}

void SnappingSplitter::onSplitterMoved() {
  printf("splitterMoved\n");
  enforceSnapRanges();
  state = saveState();
  persistState();
}

void SnappingSplitter::resizeEvent(QResizeEvent* event) {
//  QSplitter::resizeEvent(event);
//  if (bDoRestore) {
    restoreState(state);
    bDoRestore = false;
//  }
//  printf("sizes[0]: %d\n", sizes()[0]);

    if (!enforceSnapRangesOnResize()) {
      forceWidgetWidth(0);
    }
}

void SnappingSplitter::enforceSnapRanges() {
  for (const SnapRange& range : snapRanges) {
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
bool SnappingSplitter::enforceSnapRangesOnResize() {
  for (const SnapRange& range : snapRanges) {
    QList<int> sizes = this->sizes();

//    printf("%d   %d  %d\n", sizes[range.index], range.lowerBound, range.upperBound);
    if (sizes[range.index] < range.upperBound && sizes[range.index] > range.lowerBound) {
        setSizesToLowerBound(range.index, range.lowerBound);
        return true;
    }
  }
  return false;
}

void SnappingSplitter::setSizesToUpperBound(int index, int threshold) {
  QList<int> newSizes = this->sizes();
  newSizes[index] = threshold;
  newSizes[!index] = this->size().width() - threshold - this->handleWidth();
  this->setSizes(newSizes);
  forceWidgetWidth(index);
}

void SnappingSplitter:: setSizesToLowerBound(int index, int collapsePoint) {
  QList<int> newSizes = this->sizes();
  newSizes[index] = collapsePoint;
  newSizes[!index] = this->size().width() - collapsePoint - this->handleWidth();
  this->setSizes(newSizes);
  forceWidgetWidth(index);
}

void SnappingSplitter::forceWidgetWidth(int index) {
//  int size = sizes()[index];
//  auto maybeScrollArea = dynamic_cast<QScrollArea*>(widget(index));
//  if (maybeScrollArea) {
//    maybeScrollArea->widget()->setFixedWidth(size);
//  } else {
//    widget(index)->setFixedWidth(size);
//  }
}

void SnappingSplitter::addSnapRange(int index, int lowerBound, int upperBound) {
  snapRanges.append({index, lowerBound, upperBound});
}

void SnappingSplitter::clearSnapRanges() {
  snapRanges.clear();
}

void SnappingSplitter::persistState() {
  state = saveState();
  bDoRestore = true;
}