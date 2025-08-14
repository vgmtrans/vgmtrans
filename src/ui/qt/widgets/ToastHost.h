/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QObject>
#include <QVector>

class QWidget;
class Toast;
enum class ToastType;

class ToastHost : public QObject {
  Q_OBJECT
public:
  explicit ToastHost(QWidget* parentWidget);
  ~ToastHost() override = default;

  // Create + show a toast (newest appears at the top)
  Toast* showToast(const QString& message, ToastType type, int duration_ms = 3000);

  // optional knobs
  void setMargins(int marginX, int marginY) noexcept { m_marginX = marginX; m_marginY = marginY; reflow(); }
  void setSpacing(int spacing) noexcept { m_spacing = spacing; reflow(); }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void reflow();             // computes Y offsets + X anchor for all
  void onToastDismissed(Toast* t);

private:
  QWidget* m_parent{nullptr};
  QVector<Toast*> m_toasts;  // index 0 == newest (top)
  int m_marginX{10};
  int m_marginY{10};
  int m_spacing{8};
};
