/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <QWidget>
#include <QTimer>

class QLabel;
class QPushButton;
class QPropertyAnimation;
class QGraphicsOpacityEffect;
class QFrame;
class QVariantAnimation;
enum class ToastType;

struct ToastTheme {
  QColor bg, text, border;
  const char* icon; // qrc path (utf-8 literal)
};

class Toast : public QWidget {
  Q_OBJECT
public:
  explicit Toast(QWidget* parent = nullptr);
  void showMessage(const QString& message, ToastType type, int duration_ms = 3000);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  void onCloseClicked() noexcept;

private:
  static const ToastTheme& themeFor(ToastType type) noexcept;
  void applyTheme(const ToastTheme& th);
  void setTextHtml(const QString& message, const QColor& textColor);
  void startFadeIn();
  void startFadeOut();
  void cancelAnimations() noexcept;

private:
  QFrame* m_bubble{nullptr};
  QLabel* m_icon{nullptr};
  QLabel* m_text{nullptr};
  QPushButton* m_close{nullptr};

  QTimer m_timer;
  QGraphicsOpacityEffect* m_opacity_effect{nullptr};
  QVariantAnimation* m_opacity_anim{nullptr}; // single anim for both directions

  int m_duration_ms{3000};

  // Config
  static constexpr int kFadeMs = 500;
  static constexpr int kMarginX = 10;
  static constexpr int kMarginY = 10;
  static constexpr int kIconPx = 24;
  static constexpr int kCloseIconPx = 16;
  static constexpr int kBubbleWidth = 400;
  static constexpr int kFontPx = 13;
  static constexpr double kLineHeight = 1.0;
};
