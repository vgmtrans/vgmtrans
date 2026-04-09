#include "MenuPopupToolButton.h"

#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QRect>
#include <QScreen>

namespace {

QRect availableScreenGeometryFor(const QWidget *widget) {
  if (!widget) {
    return {};
  }

  if (QScreen *screen = QGuiApplication::screenAt(widget->mapToGlobal(widget->rect().center()))) {
    return screen->availableGeometry();
  }
  if (QScreen *screen = widget->screen()) {
    return screen->availableGeometry();
  }
  return {};
}

} // namespace

MenuPopupToolButton::MenuPopupToolButton(QWidget *parent) : QToolButton(parent) {
  setPopupMode(QToolButton::InstantPopup);
}

bool MenuPopupToolButton::shouldUsePopupOnPress() {
  return QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive);
}

bool MenuPopupToolButton::shouldHandleMousePress(const QMouseEvent *event) const {
  return event && event->button() == Qt::LeftButton && shouldUsePopupOnPress() && isEnabled() &&
         menu() != nullptr && !menu()->isVisible() && hitButton(event->position().toPoint());
}

QPoint MenuPopupToolButton::popupMenuPosition(const QMenu &menu) const {
  const QRect rect = this->rect();
  const QSize sizeHint = menu.sizeHint();
  const QRect screen = availableScreenGeometryFor(this);

  QPoint pos;
  if (isRightToLeft()) {
    if (mapToGlobal(QPoint(0, rect.bottom())).y() + sizeHint.height() <= screen.bottom()) {
      pos = mapToGlobal(rect.bottomRight());
    } else {
      pos = mapToGlobal(rect.topRight() - QPoint(0, sizeHint.height()));
    }
    pos.rx() -= sizeHint.width();
  } else {
    if (mapToGlobal(QPoint(0, rect.bottom())).y() + sizeHint.height() <= screen.bottom()) {
      pos = mapToGlobal(rect.bottomLeft());
    } else {
      pos = mapToGlobal(rect.topLeft() - QPoint(0, sizeHint.height()));
    }
  }

  if (screen.isValid()) {
    pos.rx() = qMax(screen.left(), qMin(pos.x(), screen.right() - sizeHint.width()));
    pos.ry() = qMax(screen.top(), qMin(pos.y() + 1, screen.bottom()));
  }

  return pos;
}

void MenuPopupToolButton::popupMenuFromPress() {
  QMenu *popupMenu = menu();
  if (!popupMenu) {
    return;
  }

  if (m_popupMenu != popupMenu) {
    if (m_popupMenu) {
      disconnect(m_popupMenu, &QMenu::aboutToHide, this, &MenuPopupToolButton::onMenuAboutToHide);
    }
    m_popupMenu = popupMenu;
    connect(m_popupMenu, &QMenu::aboutToHide, this, &MenuPopupToolButton::onMenuAboutToHide);
  }

  m_menuOpenedFromPress = true;
  popupMenu->setNoReplayFor(this);
  popupMenu->popup(popupMenuPosition(*popupMenu));
}

void MenuPopupToolButton::onMenuAboutToHide() {
  m_menuOpenedFromPress = false;
}

void MenuPopupToolButton::mousePressEvent(QMouseEvent *event) {
  if (shouldHandleMousePress(event)) {
    event->accept();
    popupMenuFromPress();
    return;
  }

  QToolButton::mousePressEvent(event);
}

void MenuPopupToolButton::mouseReleaseEvent(QMouseEvent *event) {
  if (event && event->button() == Qt::LeftButton && shouldUsePopupOnPress() && m_menuOpenedFromPress) {
    event->accept();
    return;
  }

  QToolButton::mouseReleaseEvent(event);
}
