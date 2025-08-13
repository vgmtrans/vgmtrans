/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Toast.h"

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QScreen>
#include <QIcon>
#include <QPainter>
#include <QColor>
#include <QPixmap>
#include <QSizePolicy>
#include <QFrame>

constexpr int FADE_DURATION_MS = 500;
constexpr int MARGIN_X = 10;
constexpr int MARGIN_Y = 10;
constexpr int ICON_SIZE = 24;
constexpr int CLOSE_ICON_SIZE = 16;
constexpr int BUBBLE_WIDTH = 400;
constexpr int FONT_PX = 13;
constexpr double LINE_HEIGHT = 1.0;

// Per-type colors + icon
ToastTheme Toast::themeFor(ToastType type) {
  switch (type) {
    case ToastType::Info:
      return { QColor(209,231,243), QColor(82,123,167), QColor(171,194,202),
               ":/icons/toast_info.svg" };
    case ToastType::Warning:
      return { QColor(247,243,217), QColor(137,111,60), QColor(211,210,180),
               ":/icons/toast_warning.svg" };
    case ToastType::Error:
      return { QColor(230,201,197), QColor(155,64,68), QColor(173,151,151),
               ":/icons/toast_error.svg" };
    case ToastType::Success:
      return { QColor(226,242,216), QColor(98,114,88), QColor(198,209,188),
               ":/icons/toast_success.svg" };
  }
  return { QColor("#ffffff"), QColor("#222222"), QColor("#cccccc"),
           ":/icons/toast_info.svg" };
}

Toast::Toast(QWidget *parent)
    : QWidget(parent),
      m_bubble(new QFrame(this)),
      m_icon(new QLabel(m_bubble)),
      m_text(new QLabel(m_bubble)),
      m_close(new QPushButton(m_bubble)),
      m_opacity_effect(new QGraphicsOpacityEffect(this)),
      m_fade_in(new QPropertyAnimation(m_opacity_effect, "opacity", this)),
      m_fade_out(new QPropertyAnimation(m_opacity_effect, "opacity", this)) {

  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setWindowFlags(Qt::FramelessWindowHint);

  if (parent) parent->installEventFilter(this);

  auto outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  outer->addWidget(m_bubble);
  outer->setSizeConstraint(QLayout::SetFixedSize);

  m_bubble->setObjectName(QStringLiteral("bubble"));
  m_bubble->setAttribute(Qt::WA_StyledBackground, true);
  m_bubble->setFrameStyle(QFrame::NoFrame);
  m_bubble->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
  m_bubble->setFixedWidth(BUBBLE_WIDTH); m_bubble->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);

  auto row = new QHBoxLayout(m_bubble);
  row->setContentsMargins(16, 12, 16, 12);
  row->setSpacing(12);

  m_icon->setStyleSheet("border: none;");
  m_icon->setFixedSize(ICON_SIZE, ICON_SIZE);
  // m_icon->setScaledContents(false);            // default false; keeps crisp SVG raster

  m_text->setObjectName("toast_text");
  m_text->setWordWrap(true);
  m_text->setTextFormat(Qt::RichText);
  m_text->setStyleSheet("border: none;");
  m_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  m_close->setFlat(true);
  m_close->setFocusPolicy(Qt::NoFocus);
  m_close->setStyleSheet("border: none; background: transparent;");
  m_close->setIconSize(QSize(CLOSE_ICON_SIZE, CLOSE_ICON_SIZE));
  // Add padding to increase the clickable area
  m_close->setStyleSheet("border: none; background: transparent; padding-bottom: 8px; padding-left: 8px;");

  row->addWidget(m_icon, 0, Qt::AlignTop);
  row->addWidget(m_text, 1, Qt::AlignVCenter);
  row->addWidget(m_close, 0, Qt::AlignTop);

  setGraphicsEffect(m_opacity_effect);
  m_fade_in->setDuration(FADE_DURATION_MS);
  m_fade_out->setDuration(FADE_DURATION_MS);
  connect(m_fade_out, &QPropertyAnimation::finished, this, &Toast::onFadeOutFinished);
  connect(m_close, &QPushButton::clicked, this, &Toast::onCloseClicked);
  connect(m_fade_in, &QPropertyAnimation::finished, [this]() { m_timer.start(m_duration_ms); });

  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, [this]() {
    m_fade_out->setStartValue(1.0);
    m_fade_out->setEndValue(0.0);
    m_fade_out->start();
  });
}

void Toast::showMessage(const QString& message, ToastType type, int duration_ms) {
  m_duration_ms = duration_ms;
  m_timer.stop();
  m_fade_in->stop();
  m_fade_out->stop();
  m_opacity_effect->setOpacity(0.0);

  const ToastTheme th = themeFor(type);

  // Bubble palette
  const QString style = QString(
      "QFrame#bubble {"
      "  background-color: rgb(%1,%2,%3);"
      "  border: 1px solid rgb(%4,%5,%6);"
      "  border-radius: 8px;"
      "}"
      "QFrame#bubble QLabel#toast_text {"
      "  color: rgb(%7,%8,%9);"
      "}")
      .arg(th.bg.red()).arg(th.bg.green()).arg(th.bg.blue())
      .arg(th.border.red()).arg(th.border.green()).arg(th.border.blue())
      .arg(th.text.red()).arg(th.text.green()).arg(th.text.blue());
  m_bubble->setStyleSheet(style);

  {
    QFont f = m_text->font();
    f.setPixelSize(FONT_PX);
    m_text->setFont(f);

    const QString html =
      QString("<div style='line-height:%1; font-size:%2px; white-space:pre-wrap; "
          "word-break:break-word;'>%3</div>")
        .arg(QString::number(LINE_HEIGHT, 'f', 2))
        .arg(FONT_PX)
        .arg(message.toHtmlEscaped().replace("\n", "<br/>"));
    m_text->setTextFormat(Qt::RichText);
    m_text->setText(html);
  }

  // Icon style. Tint to match text color
  {
    QPixmap iconPix = QIcon(th.icon).pixmap(ICON_SIZE, ICON_SIZE);
    if (!iconPix.isNull()) {
      QPainter p(&iconPix);
      p.setCompositionMode(QPainter::CompositionMode_SourceIn);
      p.fillRect(iconPix.rect(), th.text);
      p.end();
    }
    m_icon->setPixmap(iconPix);
  }

  // Close icon matches text color
  {
    QPixmap closePix = QIcon(":/icons/toast_close.svg").pixmap(CLOSE_ICON_SIZE, CLOSE_ICON_SIZE);
    if (!closePix.isNull()) {
      QPainter p(&closePix);
      p.setCompositionMode(QPainter::CompositionMode_SourceIn);
      p.fillRect(closePix.rect(), th.text);
      p.end();
    }
    m_close->setIcon(QIcon(closePix));
  }

  adjustSize();

  if (parentWidget()) {
    move(parentWidget()->width() - width() - MARGIN_X, MARGIN_Y);
  } else {
    QScreen* screen = QApplication::primaryScreen();
    const QRect geom = screen->availableGeometry();
    move(geom.right() - width() - MARGIN_X, geom.top() + MARGIN_Y);
  }

  show();
  raise();
  m_fade_in->setStartValue(0.0);
  m_fade_in->setEndValue(1.0);
  m_fade_in->start();
}

void Toast::onFadeOutFinished() {
  hide();
}

void Toast::onCloseClicked() {
  m_timer.stop();
  m_fade_in->stop();
  m_fade_out->stop();
  m_opacity_effect->setOpacity(0.0);
  hide();
}

bool Toast::eventFilter(QObject* watched, QEvent* event) {
  QWidget* p = parentWidget();
  if (watched == p && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
    if (isVisible() && p) {
      move(p->width() - width() - MARGIN_X, MARGIN_Y);
    }
  }
  return QWidget::eventFilter(watched, event);
}
