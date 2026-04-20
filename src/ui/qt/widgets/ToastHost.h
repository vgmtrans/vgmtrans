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
  enum class Mode {
    ChildWidget,
    ToolWindow,
  };

  static constexpr Mode defaultMode() noexcept {
#if defined(Q_OS_LINUX)
    return Mode::ChildWidget;
#else
    return Mode::ToolWindow;
#endif
  }

  explicit ToastHost(QWidget* ownerWidget, QWidget* anchorWidget = nullptr,
                     Mode mode = Mode::ChildWidget);
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
  QWidget* m_owner{nullptr};
  QWidget* m_anchor{nullptr};
  Mode m_mode{Mode::ChildWidget};
  QVector<Toast*> m_toasts;  // index 0 == newest (top)
  int m_marginX{10};
  int m_marginY{10};
  int m_spacing{8};
};
