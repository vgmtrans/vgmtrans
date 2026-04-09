#pragma once

#include <QPointer>
#include <QToolButton>

class QMenu;
class QMouseEvent;
class QWidget;

// Uses QMenu::popup() for Wayland mouse activation to avoid QToolButton's internal exec() path.
class MenuPopupToolButton final : public QToolButton {
  Q_OBJECT

public:
  explicit MenuPopupToolButton(QWidget *parent = nullptr);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  [[nodiscard]] static bool shouldUsePopupOnPress();
  [[nodiscard]] bool shouldHandleMousePress(const QMouseEvent *event) const;
  [[nodiscard]] QPoint popupMenuPosition(const QMenu &menu) const;
  void popupMenuFromPress();
  void onMenuAboutToHide();

  QPointer<QMenu> m_popupMenu;
  bool m_menuOpenedFromPress = false;
};
