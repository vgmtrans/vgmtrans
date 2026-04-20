/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ToastHost.h"
#include "Toast.h"

#include <QEvent>
#include <QWidget>

ToastHost::ToastHost(QWidget* ownerWidget, QWidget* anchorWidget, Mode mode)
  : QObject(ownerWidget),
    m_owner(ownerWidget),
    m_anchor(anchorWidget ? anchorWidget : ownerWidget),
    m_mode(mode) {
  if (m_anchor)
    m_anchor->installEventFilter(this);
  if (m_mode == Mode::ToolWindow && m_owner && m_owner != m_anchor)
    m_owner->installEventFilter(this);
}

Toast* ToastHost::showToast(const QString& message, ToastType type, int duration_ms) {
  // Newest toast goes at index 0 (top)
  auto* t = new Toast(m_mode == Mode::ToolWindow ? m_owner : m_anchor,
                      m_mode == Mode::ToolWindow);
  m_toasts.prepend(t);
  t->setMargins(m_marginX, m_marginY);
  connect(t, &Toast::dismissed, this, &ToastHost::onToastDismissed);
  t->showMessage(message, type, m_anchor, duration_ms);
  reflow();
  return t;
}

void ToastHost::onToastDismissed(Toast* t) {
  m_toasts.removeOne(t);
  reflow();
}

bool ToastHost::eventFilter(QObject* watched, QEvent* event) {
  if ((watched == m_owner || watched == m_anchor) &&
      (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
    reflow();
  }
  return QObject::eventFilter(watched, event);
}

void ToastHost::reflow() {
  int y = 0; // stack offset from top, grows downward
  for (Toast* t : m_toasts) {
    t->setMargins(m_marginX, m_marginY);
    t->setStackOffset(y);
    t->updatePlacement(m_anchor);
    t->raise();
    y += t->height() + m_spacing;
  }
}
