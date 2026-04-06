#include "DockSeparatorHitTest.h"

#include <QDockWidget>
#include <QLayout>
#include <QMainWindow>
#include <QRect>
#include <QVector>
#include <algorithm>
#include <limits>

namespace {
constexpr int kDockSeparatorGrabMargin = 6;
constexpr int kDockSeparatorGapTolerance = 8;

struct SeparatorCandidate {
  QRect rect{};
  Qt::CursorShape cursorShape{Qt::ArrowCursor};
};

bool isDockSeparatorCursor(Qt::CursorShape shape) {
  return shape == Qt::SplitHCursor || shape == Qt::SplitVCursor;
}

void addSeparatorCandidate(QVector<SeparatorCandidate>& candidates, const QRect& first,
                           const QRect& second) {
  if (!first.isValid() || !second.isValid()) {
    return;
  }

  const int overlapTop = std::max(first.top(), second.top());
  const int overlapBottom = std::min(first.bottom(), second.bottom());
  const int overlapLeft = std::max(first.left(), second.left());
  const int overlapRight = std::min(first.right(), second.right());

  const auto appendVerticalCandidate = [&](const QRect& leftRect, const QRect& rightRect) {
    const int gap = rightRect.left() - leftRect.right() - 1;
    if (overlapTop > overlapBottom || gap < -1 || gap > kDockSeparatorGapTolerance) {
      return;
    }

    candidates.append({
        QRect(QPoint(std::min(leftRect.right(), rightRect.left()), overlapTop),
              QPoint(std::max(leftRect.right(), rightRect.left()), overlapBottom)),
        Qt::SplitHCursor,
    });
  };

  const auto appendHorizontalCandidate = [&](const QRect& topRect, const QRect& bottomRect) {
    const int gap = bottomRect.top() - topRect.bottom() - 1;
    if (overlapLeft > overlapRight || gap < -1 || gap > kDockSeparatorGapTolerance) {
      return;
    }

    candidates.append({
        QRect(QPoint(overlapLeft, std::min(topRect.bottom(), bottomRect.top())),
              QPoint(overlapRight, std::max(topRect.bottom(), bottomRect.top()))),
        Qt::SplitVCursor,
    });
  };

  if (first.center().x() <= second.center().x()) {
    appendVerticalCandidate(first, second);
  } else {
    appendVerticalCandidate(second, first);
  }

  if (first.center().y() <= second.center().y()) {
    appendHorizontalCandidate(first, second);
  } else {
    appendHorizontalCandidate(second, first);
  }
}

int separatorDistance(const SeparatorCandidate& candidate, const QPoint& point) {
  if (candidate.cursorShape == Qt::SplitHCursor) {
    if (point.x() < candidate.rect.left()) {
      return candidate.rect.left() - point.x();
    }
    if (point.x() > candidate.rect.right()) {
      return point.x() - candidate.rect.right();
    }
    return 0;
  }

  if (point.y() < candidate.rect.top()) {
    return candidate.rect.top() - point.y();
  }
  if (point.y() > candidate.rect.bottom()) {
    return point.y() - candidate.rect.bottom();
  }
  return 0;
}
}  // namespace

bool findDockSeparatorHit(QMainWindow* window, const QPoint& windowPos, DockSeparatorHit& hit) {
  if (!window || !window->rect().contains(windowPos)) {
    return false;
  }

  QVector<QRect> panes;
  panes.reserve(8);
  for (QDockWidget* dock : window->findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
    if (dock && dock->isVisible() && !dock->isFloating()) {
      panes.append(dock->geometry());
    }
  }

  if (QWidget* centralWidget = window->centralWidget();
      centralWidget && centralWidget->isVisible()) {
    panes.append(centralWidget->geometry());
  }

  QVector<SeparatorCandidate> candidates;
  candidates.reserve(panes.size() * panes.size());
  for (qsizetype i = 0; i < panes.size(); ++i) {
    for (qsizetype j = i + 1; j < panes.size(); ++j) {
      addSeparatorCandidate(candidates, panes[i], panes[j]);
    }
  }

  const SeparatorCandidate* bestCandidate = nullptr;
  int bestDistance = std::numeric_limits<int>::max();
  for (const SeparatorCandidate& candidate : candidates) {
    if (!isDockSeparatorCursor(candidate.cursorShape) || !candidate.rect.isValid()) {
      continue;
    }

    QRect expandedRect = candidate.rect;
    if (candidate.cursorShape == Qt::SplitHCursor) {
      expandedRect.adjust(-kDockSeparatorGrabMargin, 0, kDockSeparatorGrabMargin, 0);
    } else {
      expandedRect.adjust(0, -kDockSeparatorGrabMargin, 0, kDockSeparatorGrabMargin);
    }

    if (!expandedRect.contains(windowPos)) {
      continue;
    }

    const int distance = separatorDistance(candidate, windowPos);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestCandidate = &candidate;
    }
  }

  if (!bestCandidate) {
    return false;
  }

  hit.cursorShape = bestCandidate->cursorShape;
  hit.snappedPos = windowPos;
  if (hit.cursorShape == Qt::SplitHCursor) {
    hit.snappedPos.setX(bestCandidate->rect.center().x());
    hit.snappedPos.setY(
        std::clamp(windowPos.y(), bestCandidate->rect.top(), bestCandidate->rect.bottom()));
  } else {
    hit.snappedPos.setX(
        std::clamp(windowPos.x(), bestCandidate->rect.left(), bestCandidate->rect.right()));
    hit.snappedPos.setY(bestCandidate->rect.center().y());
  }
  return true;
}
