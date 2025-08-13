/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Toast.h"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QSizePolicy>
#include <QVariantAnimation>
#include <QVBoxLayout>

static constexpr const char* kInfoIcon    = ":/icons/toast_info.svg";
static constexpr const char* kWarnIcon    = ":/icons/toast_warning.svg";
static constexpr const char* kErrorIcon   = ":/icons/toast_error.svg";
static constexpr const char* kSuccessIcon = ":/icons/toast_success.svg";
static constexpr const char* kCloseIcon   = ":/icons/toast_close.svg";

static const ToastTheme kThemes[] = {
  { QColor(209,231,243), QColor( 82,123,167), QColor(171,194,202), kInfoIcon    }, // Info
  { QColor(247,243,217), QColor(137,111, 60), QColor(211,210,180), kWarnIcon    }, // Warning
  { QColor(230,201,197), QColor(155, 64, 68), QColor(173,151,151), kErrorIcon   }, // Error
  { QColor(226,242,216), QColor( 98,114, 88), QColor(198,209,188), kSuccessIcon }, // Success
};

static inline QPixmap tintedIcon(const QIcon& base, const QSize& pxSize, const QColor& color) {
  QPixmap pm = base.pixmap(pxSize);
  if (!pm.isNull()) {
    const qreal dpr = pm.devicePixelRatio();
    QPainter p(&pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pm.rect(), color);
    p.end();
    pm.setDevicePixelRatio(dpr);
  }
  return pm;
}

const ToastTheme& Toast::themeFor(ToastType type) noexcept {
  const int idx = static_cast<int>(type);
  if (idx >= 0 && idx < static_cast<int>(std::size(kThemes))) return kThemes[idx];
  static const ToastTheme fallback{ QColor("#ffffff"), QColor("#222222"), QColor("#cccccc"), kInfoIcon };
  return fallback;
}

Toast::Toast(QWidget* parent)
  : QWidget(parent),
    m_bubble(new QFrame(this)),
    m_icon(new QLabel(m_bubble)),
    m_text(new QLabel(m_bubble)),
    m_close(new QPushButton(m_bubble)),
    m_opacity_effect(new QGraphicsOpacityEffect(this)),
    m_opacity_anim(new QVariantAnimation(this)) {

  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setWindowFlags(Qt::FramelessWindowHint);

  if (parent) parent->installEventFilter(this);

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0,0,0,0);
  outer->setSpacing(0);
  outer->addWidget(m_bubble);
  outer->setSizeConstraint(QLayout::SetFixedSize);

  m_bubble->setObjectName(QStringLiteral("bubble"));
  m_bubble->setAttribute(Qt::WA_StyledBackground, true);
  m_bubble->setFrameStyle(QFrame::NoFrame);
  m_bubble->setFixedWidth(kBubbleWidth);
  m_bubble->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);

  auto* row = new QHBoxLayout(m_bubble);
  row->setContentsMargins(16,12,16,12);
  row->setSpacing(12);

  m_icon->setStyleSheet(QStringLiteral("border: none;"));
  m_icon->setFixedSize(kIconPx, kIconPx);

  m_text->setObjectName(QStringLiteral("toast_text"));
  m_text->setWordWrap(true);
  m_text->setTextFormat(Qt::RichText);
  m_text->setStyleSheet(QStringLiteral("border: none;"));
  m_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  m_close->setFlat(true);
  m_close->setFocusPolicy(Qt::NoFocus);
  m_close->setAccessibleName(QStringLiteral("Dismiss"));
  m_close->setIconSize(QSize(kCloseIconPx, kCloseIconPx));
  m_close->setStyleSheet(QStringLiteral(
      "border: none; background: transparent; padding-bottom: 8px; padding-left: 8px;"));

  row->addWidget(m_icon, 0, Qt::AlignTop);
  row->addWidget(m_text, 1, Qt::AlignVCenter);
  row->addWidget(m_close, 0, Qt::AlignTop);

  setGraphicsEffect(m_opacity_effect);
  m_opacity_effect->setOpacity(0.0);

  // single opacity anim (both directions)
  m_opacity_anim->setStartValue(0.0);
  m_opacity_anim->setEndValue(1.0);
  m_opacity_anim->setDuration(kFadeMs);
  connect(m_opacity_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){
    m_opacity_effect->setOpacity(v.toDouble());
  });
  connect(m_opacity_anim, &QVariantAnimation::finished, this, [this]{
    if (qFuzzyCompare(m_opacity_effect->opacity(), 1.0)) {
      m_timer.start(m_duration_ms);
    } else {
      // fade-out complete
      if (!m_emittedDismissed) { m_emittedDismissed = true; emit dismissed(this); }
      hide();
      deleteLater();
    }
  });

  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, [this]{ startFadeOut(); });
  connect(m_close, &QPushButton::clicked, this, &Toast::onCloseClicked);
}

void Toast::applyTheme(const ToastTheme& th) {
  m_bubble->setStyleSheet(QStringLiteral(
    "QFrame#bubble {"
      "background-color:%1;"
      "border:1px solid %2;"
      "border-radius:8px;"
    "}"
    "QFrame#bubble QLabel#toast_text { color:%3; }"
  ).arg(th.bg.name(), th.border.name(), th.text.name()));

  // tint close icon
  m_close->setIcon(QIcon(tintedIcon(QIcon(QString(kCloseIcon)),
                                    QSize(kCloseIconPx, kCloseIconPx), th.text)));
}

void Toast::setTextHtml(const QString& message) {
  QFont f = m_text->font();
  f.setPixelSize(kFontPx);
  m_text->setFont(f);

  const bool isRich = Qt::mightBeRichText(message);

  const QString body = isRich
    ? message                                  // accept caller HTML
    : message.toHtmlEscaped()
             .replace(QLatin1Char('\n'), QLatin1String("<br/>")); // preserve newlines

  const QString wrapped =
    QString::fromLatin1(
      "<div style='line-height:%1; font-size:%2px; white-space:pre-wrap; word-break:break-word;'>%3</div>")
      .arg(QString::number(kLineHeight, 'f', 2))
      .arg(kFontPx)
      .arg(body);

  m_text->setTextFormat(Qt::RichText);         // we’re giving it HTML
  m_text->setText(wrapped);
}

void Toast::startFadeIn() {
  m_opacity_anim->stop();
  m_opacity_anim->setDirection(QAbstractAnimation::Forward);
  m_opacity_anim->start();
}
void Toast::startFadeOut() {
  m_opacity_anim->stop();
  m_opacity_anim->setDirection(QAbstractAnimation::Backward);
  m_opacity_anim->start();
}
void Toast::cancelAnimations() noexcept {
  m_timer.stop();
  m_opacity_anim->stop();
  m_opacity_effect->setOpacity(0.0);
}

void Toast::showMessage(const QString& message, ToastType type, int duration_ms) {
  m_duration_ms = duration_ms;
  m_emittedDismissed = false;
  cancelAnimations();

  const ToastTheme& th = themeFor(type);
  applyTheme(th);
  setTextHtml(message);

  // main icon (tinted to theme text color)
  m_icon->setPixmap(tintedIcon(QIcon(QString::fromUtf8(th.icon)),
                               QSize(kIconPx, kIconPx), th.text));

  adjustSize();

  // Initial placement; host will keep it updated via setStackOffset + margins
  if (QWidget* pw = parentWidget()) {
    move(pw->width() - width() - m_marginX, m_marginY + m_stackOffsetY);
  } else if (QScreen* screen = QGuiApplication::primaryScreen()) {
    const QRect geom = screen->availableGeometry();
    move(geom.right() - width() - m_marginX, geom.top() + m_marginY + m_stackOffsetY);
  }

  show();
  raise();
  startFadeIn();
}

void Toast::onCloseClicked() noexcept {
  if (!m_emittedDismissed) { m_emittedDismissed = true; emit dismissed(this); }
  cancelAnimations();
  hide();
  deleteLater();
}

bool Toast::eventFilter(QObject* watched, QEvent* event) {
  QWidget* p = parentWidget();
  if (watched == p && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
    if (isVisible() && p) {
      move(p->width() - width() - m_marginX, m_marginY + m_stackOffsetY);
    }
  }
  return QWidget::eventFilter(watched, event);
}
