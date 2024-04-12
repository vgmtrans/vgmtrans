/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include <QSplitter>
#include <QSplitterHandle>
#include <QList>

class QMouseEvent;

// A CollapseRange will prevent the splitter from setting the size of the view to any value between
// the upper and lower bound, instead snapping to the closest bound when mouse is within the range.
struct CollapseRange {
  int index;          // Index of the widget this behavior should apply to (0 or 1)
  int lowerBound;     // Lower bound for snapping
  int upperBound;     // Upper bound for snapping
};

class SnappingSplitter : public QSplitter {
  QList<CollapseRange> collapseRanges;

public:
  SnappingSplitter(Qt::Orientation orientation, QWidget* parent = nullptr);

  void enforceConfigurations();
  void enforceConfigurationsOnResize();
  void addCollapseRange(int index, int lowerBound, int upperBound);
  void clearCollapseRanges();

  void persistState();
  QByteArray state;

protected:
  void onSplitterMoved();
  void resizeEvent(QResizeEvent* event) override;
  void setSizesToUpperBound(int index, int threshold);
  void setSizesToLowerBound(int index, int threshold);
};
