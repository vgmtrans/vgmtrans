/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <QUrl>

#include <functional>

namespace QtUi {

// Shared drag/drop behavior for embedded RHI windows so all view types route
// overlays and dropped URLs through the same semantics.
inline bool handleRhiWindowDragDropEvent(
    QEvent* event,
    const std::function<void()>& showOverlay,
    const std::function<void()>& hideOverlay,
    const std::function<void(const QList<QUrl>&)>& dropUrls) {
  if (!event) {
    return false;
  }

  switch (event->type()) {
    case QEvent::DragEnter: {
      auto* dragEvent = static_cast<QDragEnterEvent*>(event);
      if (showOverlay) {
        showOverlay();
      }
      dragEvent->acceptProposedAction();
      return true;
    }
    case QEvent::DragMove: {
      auto* dragEvent = static_cast<QDragMoveEvent*>(event);
      if (showOverlay) {
        showOverlay();
      }
      dragEvent->acceptProposedAction();
      return true;
    }
    case QEvent::DragLeave:
      if (hideOverlay) {
        hideOverlay();
      }
      event->accept();
      return true;
    case QEvent::Drop: {
      auto* dropEvent = static_cast<QDropEvent*>(event);
      if (dropUrls) {
        dropUrls(dropEvent->mimeData()->urls());
      }
      dropEvent->acceptProposedAction();
      return true;
    }
    default:
      return false;
  }
}

}  // namespace QtUi

