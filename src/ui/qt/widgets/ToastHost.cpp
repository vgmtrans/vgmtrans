/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ToastHost.h"
#include "Toast.h"

#include <QEvent>
#include <QWidget>

ToastHost::ToastHost(QWidget* ownerWidget, QWidget* anchorWidget)
  : QObject(ownerWidget), m_owner(ownerWidget), m_anchor(anchorWidget ? anchorWidget : ownerWidget) {
  if (m_owner)
    m_owner->installEventFilter(this);
  if (m_anchor && m_anchor != m_owner)
    m_anchor->installEventFilter(this);
}

Toast* ToastHost::showToast(const QString& message, ToastType type, int duration_ms) {
  if (!m_owner)
    return nullptr;

  // Newest toast goes at index 0 (top)
  auto* t = new Toast(m_owner, m_anchor);
  t->setMargins(m_marginX, m_marginY);
  t->showMessage(message, type, duration_ms);

  connect(t, &Toast::dismissed, this, &ToastHost::onToastDismissed);

  m_toasts.prepend(t);
  reflow();
  return t;
}

void ToastHost::onToastDismissed(Toast* t) {
  const int idx = m_toasts.indexOf(t);
  if (idx >= 0)
    m_toasts.removeAt(idx);
  // Toast deletes itself; just reflow others
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
  if (!m_owner)
    return;

  int y = 0; // stack offset from top, grows downward
  for (Toast* t : m_toasts) {
    if (!t)
      continue;
    t->setMargins(m_marginX, m_marginY);
    t->setStackOffset(y);
    t->updatePlacement();
    t->raise();
    y += t->height() + m_spacing;
  }
}
