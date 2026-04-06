#pragma once

#include <QPoint>
#include <Qt>

class QMainWindow;

struct DockSeparatorHit {
  QPoint snappedPos{};
  Qt::CursorShape cursorShape{Qt::ArrowCursor};
};

bool findDockSeparatorHit(QMainWindow* window, const QPoint& windowPos, DockSeparatorHit& hit);
