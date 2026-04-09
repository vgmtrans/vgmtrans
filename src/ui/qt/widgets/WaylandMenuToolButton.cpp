#include "WaylandMenuToolButton.h"

#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QRect>
#include <QScreen>

WaylandMenuToolButton::WaylandMenuToolButton(QWidget *parent) : QToolButton(parent) {
  setPopupMode(QToolButton::InstantPopup);
}

bool WaylandMenuToolButton::isWaylandPlatform() {
#ifdef Q_OS_LINUX
  return QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive);
#else
  return false;
#endif
}

bool WaylandMenuToolButton::shouldHandleMousePress(const QMouseEvent &event) const {
  QMenu *popupMenu = menu();
  return isWaylandPlatform() && event.button() == Qt::LeftButton && popupMenu != nullptr &&
         !popupMenu->isVisible() && hitButton(event.position().toPoint());
}

QPoint WaylandMenuToolButton::popupMenuPosition(const QMenu &menu) const {
  const QRect rect = this->rect();
  const QSize sizeHint = menu.sizeHint();
  QScreen *screen = QGuiApplication::screenAt(mapToGlobal(rect.center()));
  if (!screen) {
    screen = this->screen();
  }
  const QRect availableGeometry = screen ? screen->availableGeometry() : QRect();

  QPoint pos;
  if (isRightToLeft()) {
    if (mapToGlobal(QPoint(0, rect.bottom())).y() + sizeHint.height() <= availableGeometry.bottom()) {
      pos = mapToGlobal(rect.bottomRight());
    } else {
      pos = mapToGlobal(rect.topRight() - QPoint(0, sizeHint.height()));
    }
    pos.rx() -= sizeHint.width();
  } else {
    if (mapToGlobal(QPoint(0, rect.bottom())).y() + sizeHint.height() <= availableGeometry.bottom()) {
      pos = mapToGlobal(rect.bottomLeft());
    } else {
      pos = mapToGlobal(rect.topLeft() - QPoint(0, sizeHint.height()));
    }
  }

  // Match Qt's toolbutton placement so the workaround only changes how the menu is shown.
  if (availableGeometry.isValid()) {
    pos.rx() =
        qMax(availableGeometry.left(), qMin(pos.x(), availableGeometry.right() - sizeHint.width()));
    pos.ry() = qMax(availableGeometry.top(), qMin(pos.y() + 1, availableGeometry.bottom()));
  }

  return pos;
}

void WaylandMenuToolButton::popupMenuFromPress() {
  QMenu *popupMenu = menu();
  Q_ASSERT(popupMenu);

  // QToolButton::showMenu() routes through an internal exec() path. On Linux/Wayland these pane
  // buttons intermittently fail there, so open the menu directly while we still have the press
  // event that triggered the popup.
  m_menuOpenedFromPress = true;
  connect(popupMenu,
          &QMenu::aboutToHide,
          this,
          &WaylandMenuToolButton::onMenuAboutToHide,
          Qt::UniqueConnection);
  popupMenu->setNoReplayFor(this);
  popupMenu->popup(popupMenuPosition(*popupMenu));
}

void WaylandMenuToolButton::onMenuAboutToHide() {
  m_menuOpenedFromPress = false;
}

void WaylandMenuToolButton::mousePressEvent(QMouseEvent *event) {
  if (shouldHandleMousePress(*event)) {
    event->accept();
    popupMenuFromPress();
    return;
  }

  QToolButton::mousePressEvent(event);
}

void WaylandMenuToolButton::mouseReleaseEvent(QMouseEvent *event) {
  // Swallow the release that opened the menu so QToolButton does not process the same click twice.
  if (isWaylandPlatform() && m_menuOpenedFromPress && event->button() == Qt::LeftButton) {
    event->accept();
    return;
  }

  QToolButton::mouseReleaseEvent(event);
}
