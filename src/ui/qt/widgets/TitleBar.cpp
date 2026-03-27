/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "TitleBar.h"
#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QSizePolicy>
#include <QToolButton>
#include "Metrics.h"
#include "UIHelpers.h"

namespace {
constexpr int kTitleBarButtonWidth = 22;
constexpr int kTitleBarButtonHeight = 20;
constexpr int kTitleBarIconSize = 16;
constexpr int kTitleToLeadingControlsGap = 12;
}

TitleBar::TitleBar(const QString& title, Buttons buttons, QWidget *parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  auto *titleLayout = new QHBoxLayout(this);
  titleLayout->setContentsMargins(Margin::HCommon, 5, Margin::HCommon, 5);
  titleLayout->setSpacing(4);
  QLabel *titleLabel = new QLabel(title);
  titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
  titleLayout->addWidget(titleLabel);
  const auto makeButton = [this](const QString& toolTip) {
    auto *button = new QToolButton(this);
    configureToolButton(button, toolTip, QSize(kTitleBarButtonWidth, kTitleBarButtonHeight),
                        QSize(kTitleBarIconSize, kTitleBarIconSize));
    return button;
  };

  if (buttons.testFlag(NewButton)) {
    m_newButton = makeButton("New collection");
    titleLayout->addSpacing(kTitleToLeadingControlsGap);
    titleLayout->addWidget(m_newButton);
    connect(m_newButton, &QToolButton::clicked, this, &TitleBar::addRequested);
  }

  m_leadingContainer = new QWidget(this);
  m_leadingLayout = new QHBoxLayout(m_leadingContainer);
  m_leadingLayout->setContentsMargins(kTitleToLeadingControlsGap, 0, 0, 0);
  m_leadingLayout->setSpacing(1);
  m_leadingContainer->hide();
  titleLayout->addWidget(m_leadingContainer);
  titleLayout->addStretch(1);

  m_buttonContainer = new QWidget(this);
  auto *buttonLayout = new QHBoxLayout(m_buttonContainer);
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  buttonLayout->setSpacing(4);
  titleLayout->addWidget(m_buttonContainer);
  m_buttonOpacity = new QGraphicsOpacityEffect(m_buttonContainer);
  m_buttonOpacity->setOpacity(0.0);
  m_buttonContainer->setGraphicsEffect(m_buttonOpacity);
  m_buttonFade = new QPropertyAnimation(m_buttonOpacity, "opacity", this);
  m_buttonFade->setDuration(120);
  connect(m_buttonFade, &QPropertyAnimation::finished, this, [this]() {
    if (!m_buttonsVisible && m_buttonOpacity->opacity() == 0.0) {
      m_buttonContainer->hide();
    }
  });
  m_buttonContainer->hide();

  QFont labelFont("Arial", -1, QFont::Bold, true);
  titleLabel->setFont(labelFont);

  if (buttons.testFlag(HideButton)) {
    m_hideButton = makeButton("Hide");
    buttonLayout->addWidget(m_hideButton);
    connect(m_hideButton, &QToolButton::clicked, this, &TitleBar::hideRequested);
  }
  updateButtonStyles();

  installEventFilter(this);
  if (auto *dock = qobject_cast<QDockWidget *>(parentWidget())) {
    dock->installEventFilter(this);
    if (QWidget *dockWidget = dock->widget()) {
      dockWidget->installEventFilter(this);
    }
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *) { updateButtonsVisible(); });
  }
  updateButtonsVisible();
}

void TitleBar::addLeadingWidget(QWidget *widget) {
  if (!widget || !m_leadingLayout) {
    return;
  }
  widget->setParent(m_leadingContainer);
  m_leadingLayout->addWidget(widget);
  m_leadingContainer->show();
}

void TitleBar::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);

  if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
    updateButtonStyles();
    emit appearanceChanged();
  }
}

bool TitleBar::eventFilter(QObject *watched, QEvent *event) {
  auto *dock = qobject_cast<QDockWidget *>(parentWidget());
  if ((watched == this || watched == dock || (dock && watched == dock->widget())) &&
      (event->type() == QEvent::Enter || event->type() == QEvent::Leave || event->type() == QEvent::Show ||
       event->type() == QEvent::Hide)) {
    updateButtonsVisible();
  }
  return QWidget::eventFilter(watched, event);
}

void TitleBar::updateButtonStyles() {
  if (m_newButton) {
    refreshStencilToolButton(m_newButton, QStringLiteral(":/icons/plus.svg"), palette());
  }

  if (m_hideButton) {
    refreshStencilToolButton(m_hideButton, QStringLiteral(":/icons/minus.svg"), palette());
  }
}

void TitleBar::updateButtonsVisible() {
  if (!m_buttonContainer) {
    return;
  }

  auto *dock = qobject_cast<QDockWidget *>(parentWidget());
  QWidget *focus = QApplication::focusWidget();
  const bool focused = dock && focus && (focus == dock || dock->isAncestorOf(focus));
  const bool hovered = underMouse() || (dock && dock->underMouse()) || (dock && dock->widget() && dock->widget()->underMouse());
  const bool visible = hovered || focused;
  if (visible == m_buttonsVisible) {
    return;
  }

  m_buttonsVisible = visible;
  m_buttonFade->stop();
  m_buttonContainer->show();
  m_buttonFade->setStartValue(m_buttonOpacity->opacity());
  m_buttonFade->setEndValue(visible ? 1.0 : 0.0);
  m_buttonFade->start();
}
