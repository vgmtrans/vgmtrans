/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "WindowBar.h"

#include <cmath>
#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QToolButton>
#include "UIHelpers.h"

namespace {
constexpr int kTitleBarHeight = 40;
constexpr int kMacSystemButtonAreaWidth = 58;
constexpr int kMacWindowBarLeftMargin = 14;
constexpr int kMacWindowBarRightMargin = 10;
constexpr int kDockControlsLeadingGap = 28;
constexpr int kTitleBarToggleButtonWidth = 30;
constexpr int kTitleBarToggleButtonHeight = 25;
constexpr int kTitleBarToggleIconSize = 19;
constexpr int kCustomWindowIconButtonWidth = 36;
constexpr int kCustomWindowButtonWidth = 46;
constexpr int kCustomWindowIconSize = 18;
constexpr int kCustomWindowGlyphSize = 12;
constexpr int kLinuxWindowButtonSize = 32;
#if defined(Q_OS_WIN)
constexpr bool kWindowsCustomChrome = true;
#else
constexpr bool kWindowsCustomChrome = false;
#endif
#if defined(Q_OS_LINUX)
constexpr bool kLinuxCustomChrome = true;
#else
constexpr bool kLinuxCustomChrome = false;
#endif
constexpr qreal kPlaybackControlsFreeWidthFraction = 0.55;
constexpr qreal kFreeWidthThreshold = 0.3;

QIcon multiStateStencilIcon(const QString &iconPath, const QColor &normalColor,
                            const QColor &activeColor, const QColor &disabledColor,
                            const QSize &size) {
  QIcon icon;

  const auto addMode = [&](QIcon::Mode mode, const QColor &color) {
    const QIcon tintedIcon = stencilSvgIcon(iconPath, color);
    icon.addPixmap(tintedIcon.pixmap(size), mode, QIcon::Off);
  };

  addMode(QIcon::Normal, normalColor);
  addMode(QIcon::Active, activeColor);
  addMode(QIcon::Disabled, disabledColor);
  return icon;
}

}

WindowBar::WindowBar(QWidget *parent) : QWidget(parent) {
  setFixedHeight(kTitleBarHeight);
  setAutoFillBackground(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  m_layout = new QHBoxLayout(this);
  m_layout->setContentsMargins(
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
      kMacWindowBarLeftMargin, 0, kMacWindowBarRightMargin, 0
#else
      0, 0, 0, 0
#endif
  );
  m_layout->setSpacing(6);

  m_menuBarPlaceholder = new QWidget(this);
  m_menuBarPlaceholder->setFixedSize(0, 0);
  m_menuBarPlaceholder->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_menuBarWidget = m_menuBarPlaceholder;

  m_centerPlaceholder = new QWidget(this);
  m_centerPlaceholder->setFixedSize(0, 0);
  m_centerPlaceholder->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_centerWidget = m_centerPlaceholder;
  m_leftCenterSpacer = new QWidget(this);
  m_leftCenterSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_dockControlsSpacer = new QWidget(this);
  m_dockControlsSpacer->setFixedWidth(kDockControlsLeadingGap);
  m_dockControlsSpacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  m_dockControls = new QWidget(this);
  auto *dockControlsLayout = new QHBoxLayout(m_dockControls);
  dockControlsLayout->setContentsMargins(0, 8, 0, 7);
  dockControlsLayout->setSpacing(4);
  m_dockControls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  m_systemButtonArea = new QWidget(this);
  m_systemButtonArea->setFixedSize(kMacSystemButtonAreaWidth, kTitleBarHeight - 4);
  m_layout->addWidget(m_systemButtonArea, 0, Qt::AlignBottom);
  m_layout->addWidget(m_dockControlsSpacer);
  m_layout->addWidget(m_dockControls, 0, Qt::AlignVCenter);
  m_layout->addWidget(m_leftCenterSpacer);
  m_layout->addWidget(m_centerWidget, 0, Qt::AlignVCenter);
#else
  m_windowIconButton = createWindowButton(QString());
  m_windowIconButton->setObjectName(QStringLiteral("windowIconButton"));
  applyWindowButtonStyle(m_windowIconButton, false, true);
  m_layout->addWidget(m_windowIconButton, 0, Qt::AlignVCenter);
  m_layout->addWidget(m_menuBarWidget, 0, Qt::AlignVCenter);
  m_layout->addWidget(m_dockControlsSpacer);
  m_layout->addWidget(m_dockControls, 0, Qt::AlignVCenter);
  m_layout->addWidget(m_leftCenterSpacer);
  m_layout->addWidget(m_centerWidget, 0, Qt::AlignVCenter);
  m_layout->addSpacing(8);

  m_rightControls = new QWidget(this);
  auto *buttonLayout = new QHBoxLayout(m_rightControls);
  buttonLayout->setContentsMargins(0, 0, kLinuxCustomChrome ? 4 : 0, 0);
  buttonLayout->setSpacing(kWindowsCustomChrome ? 0 : 4);
  m_rightControls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  m_minimizeButton = createWindowButton("Minimize window");
  m_maximizeButton = createWindowButton("Maximize window");
  m_closeButton = createWindowButton("Close window");

  m_minimizeButton->setObjectName(QStringLiteral("minimizeButton"));
  m_maximizeButton->setObjectName(QStringLiteral("maximizeButton"));
  m_closeButton->setObjectName(QStringLiteral("closeButton"));
  applyWindowButtonStyle(m_minimizeButton);
  applyWindowButtonStyle(m_maximizeButton);
  applyWindowButtonStyle(m_closeButton, true);

  connect(m_minimizeButton, &QToolButton::clicked, this, [this]() {
    QWidget *topLevelWindow = window();
    topLevelWindow->showMinimized();
  });
  connect(m_maximizeButton, &QToolButton::clicked, this, [this]() {
    QWidget *topLevelWindow = window();
    topLevelWindow->isMaximized() ? topLevelWindow->showNormal() : topLevelWindow->showMaximized();
  });
  connect(m_closeButton, &QToolButton::clicked, this, [this]() {
    window()->close();
  });

  buttonLayout->addWidget(m_minimizeButton);
  buttonLayout->addWidget(m_maximizeButton);
  buttonLayout->addWidget(m_closeButton);

  m_layout->addWidget(m_rightControls, 0, Qt::AlignRight | Qt::AlignVCenter);
#endif

}

void WindowBar::setCenterWidget(QWidget *widget) {
  QWidget *replacement = widget ? widget : m_centerPlaceholder;
  if (replacement == m_centerWidget) {
    return;
  }

  m_layout->replaceWidget(m_centerWidget, replacement);
  if (m_centerWidget != m_centerPlaceholder) {
    m_centerWidget->setParent(nullptr);
  }

  m_centerWidget = replacement;
  if (widget) {
    widget->setParent(this);
    widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    widget->show();
  }
  updateResponsiveLayout();
}

void WindowBar::setMenuBarWidget(QWidget *widget) {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  Q_UNUSED(widget);
  return;
#else
  QWidget *replacement = widget ? widget : m_menuBarPlaceholder;
  if (replacement == m_menuBarWidget) {
    return;
  }

  m_layout->replaceWidget(m_menuBarWidget, replacement);
  if (m_menuBarWidget != m_menuBarPlaceholder) {
    m_menuBarWidget->setParent(nullptr);
  }

  m_menuBarWidget = replacement;
  if (widget) {
    widget->setParent(this);
    widget->setContentsMargins(0, 0, 0, 0);
    widget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
#if defined(Q_OS_WIN)
    widget->setStyleSheet(QStringLiteral(
        "QMenuBar { background: transparent; border: none; }"
        "QMenuBar::item { padding: 3px 8px; margin: 0px; background: transparent; }"
        "QMenuBar::item:selected { background: palette(alternate-base); border-radius: 8px; }"
        "QMenuBar::item:pressed { background: palette(alternate-base); }"
        "QMenu { padding: 8px 0px; "
                "background-color: palette(base);"
                "border: 1px solid palette(alternate-base);"
                "border-radius: 8px;"
        "}"
        "QMenu::item { background: palette(base); padding: 6px 12px; margin: 0px; }"
        "QMenu::item:selected { background: palette(alternate-base); }"));
#else
    widget->setStyleSheet(QStringLiteral("QMenuBar { background: transparent; border: none; }"));
#endif
    widget->show();
  }
  updateResponsiveLayout();
#endif
}

void WindowBar::setDockToggleButtons(const QList<ToggleButtonSpec> &buttons) {
  auto *dockControlsLayout = qobject_cast<QHBoxLayout *>(m_dockControls->layout());

  while (QLayoutItem *item = dockControlsLayout->takeAt(0)) {
    if (QWidget *widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }
  m_dockToggleButtons.clear();

  for (const auto &spec : buttons) {
    if (!spec.action) {
      continue;
    }

    auto *button = new QToolButton(m_dockControls);
    button->setDefaultAction(spec.action);
    configureToolButton(button, spec.action->toolTip(),
                        QSize(kTitleBarToggleButtonWidth, kTitleBarToggleButtonHeight),
                        QSize(kTitleBarToggleIconSize, kTitleBarToggleIconSize));
    dockControlsLayout->addWidget(button);
    connect(spec.action, &QAction::changed, this, [this]() { refreshDockToggleButtons(); });

    m_dockToggleButtons.append({button, spec.iconPath});
  }

  refreshDockToggleButtons();
  updateResponsiveLayout();
}

void WindowBar::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);

  if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
    refreshDockToggleButtons();
    applyWindowButtonStyle(m_windowIconButton, false, true);
    applyWindowButtonStyle(m_minimizeButton);
    applyWindowButtonStyle(m_maximizeButton);
    applyWindowButtonStyle(m_closeButton, true);
    syncWindowButtons();
  }
}

bool WindowBar::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_trackedWindow &&
      (event->type() == QEvent::WindowStateChange || event->type() == QEvent::Show ||
       event->type() == QEvent::WindowIconChange)) {
    syncWindowButtons();
  }
  return QWidget::eventFilter(watched, event);
}

void WindowBar::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  attachToTopLevelWindow();
  syncWindowButtons();
  updateResponsiveLayout();
}

void WindowBar::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updateResponsiveLayout();
}

void WindowBar::attachToTopLevelWindow() {
  QWidget *topLevelWindow = window();
  if (m_trackedWindow == topLevelWindow) {
    return;
  }

  if (m_trackedWindow) {
    m_trackedWindow->removeEventFilter(this);
  }

  m_trackedWindow = topLevelWindow;
  if (m_trackedWindow) {
    m_trackedWindow->installEventFilter(this);
  }
}

void WindowBar::updateResponsiveLayout() {
  if (m_centerWidget == m_centerPlaceholder) {
    return;
  }

  const auto visibleWidth = [](QWidget *widget) {
    return widget && !widget->isHidden() ? widget->sizeHint().width() : 0;
  };

  const QMargins margins = m_layout->contentsMargins();
  const int centerMinimumWidth = std::max(0, m_centerWidget->minimumSizeHint().width());
  const int dockControlsWidth =
      m_dockToggleButtons.isEmpty() ? 0 : m_dockControlsSpacer->sizeHint().width() + m_dockControls->sizeHint().width();
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  const int baseFixedWidth = margins.left() + margins.right() + visibleWidth(m_systemButtonArea);
#else
  const int baseFixedWidth =
      margins.left() + margins.right() + visibleWidth(m_windowIconButton) + visibleWidth(m_menuBarWidget) +
      visibleWidth(m_rightControls) + 8;
#endif
  const int availableWidth = std::max(0, width() - baseFixedWidth);
  const int availableWidthWithDockControls = std::max(0, availableWidth - dockControlsWidth);
  const int desiredCenterWidthWithDockControls =
      static_cast<int>(std::lround(availableWidthWithDockControls * kPlaybackControlsFreeWidthFraction));
  const int unusedFreeWidthWithDockControls =
      std::max(0, availableWidthWithDockControls - desiredCenterWidthWithDockControls);
  const bool showDockControls =
      dockControlsWidth > 0 &&
      (static_cast<qreal>(unusedFreeWidthWithDockControls) / std::max(1, availableWidth)) >= kFreeWidthThreshold;

  m_dockControlsSpacer->setVisible(showDockControls);
  m_dockControls->setVisible(showDockControls);

  const int availableCenterWidth = std::max(0, availableWidth - (showDockControls ? dockControlsWidth : 0));
  const int desiredCenterWidth =
      static_cast<int>(std::lround(availableCenterWidth * kPlaybackControlsFreeWidthFraction));
  m_centerWidget->setFixedWidth(
      std::min(availableCenterWidth, std::max(centerMinimumWidth, desiredCenterWidth)));
  m_layout->activate();
}

void WindowBar::applyWindowButtonStyle(QToolButton *button, bool closeButton, bool iconButton) const {
  if (!button) {
    return;
  }

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
  if (iconButton) {
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setFixedSize(kCustomWindowIconButtonWidth, kTitleBarHeight);
    button->setIconSize(QSize(kCustomWindowIconSize, kCustomWindowIconSize));
    button->setStyleSheet(QStringLiteral(
        "QToolButton {"
        " border: none;"
        " background: transparent;"
        " padding: 0px;"
        " margin: 0px;"
        "}"));
    return;
  }

  const bool darkPalette = isDarkPalette(palette());
  QColor hoverFill = darkPalette ? QColor(255, 255, 255, 38) : QColor(0, 0, 0, 24);
  QColor pressedFill = darkPalette ? QColor(255, 255, 255, 52) : QColor(0, 0, 0, 34);
  QColor closeHoverFill(QStringLiteral("#e81123"));
  QColor closePressedFill(QStringLiteral("#c50f1f"));

  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setFixedSize(kLinuxCustomChrome ? kLinuxWindowButtonSize : kCustomWindowButtonWidth,
                       kLinuxCustomChrome ? kLinuxWindowButtonSize : kTitleBarHeight);
  button->setIconSize(QSize(kCustomWindowGlyphSize, kCustomWindowGlyphSize));
  button->setStyleSheet(QStringLiteral(
      "QToolButton {"
      " border: none;"
      " background: transparent;"
      " border-radius: %1px;"
      " padding: 0px;"
      " margin: 0px;"
      "}"
      "QToolButton:hover { background: %2; }"
      "QToolButton:pressed { background: %3; }")
                            .arg(kLinuxCustomChrome ? kLinuxWindowButtonSize / 2 : 0)
                            .arg(cssColor(closeButton && kWindowsCustomChrome ? closeHoverFill : hoverFill))
                            .arg(cssColor(closeButton && kWindowsCustomChrome ? closePressedFill : pressedFill)));
#else
  Q_UNUSED(closeButton);
  Q_UNUSED(iconButton);
#endif
}

QToolButton *WindowBar::createWindowButton(const QString& toolTip) {
  auto *button = new QToolButton(this);
  configureToolButton(button, toolTip);

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
  button->setFixedHeight(kTitleBarHeight);
#else
  const int buttonSize = style()->pixelMetric(QStyle::PM_TitleBarButtonSize, nullptr, this);
  const int iconSize = style()->pixelMetric(QStyle::PM_TitleBarButtonIconSize, nullptr, this);
  if (buttonSize > 0) {
    button->setFixedSize(buttonSize, buttonSize);
  }
  if (iconSize > 0) {
    button->setIconSize(QSize(iconSize, iconSize));
  }
#endif

  return button;
}

void WindowBar::refreshDockToggleButtons() {
  for (const auto &entry : m_dockToggleButtons) {
    refreshStencilToolButton(entry.button, entry.iconPath, palette(), true);
  }
}

void WindowBar::syncWindowButtons() {
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
  const bool maximized = window()->isMaximized();

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
  const bool darkPalette = isDarkPalette(palette());
  const QColor windowColor = palette().color(QPalette::Window);
  const QColor buttonColor =
      blendColors(palette().color(QPalette::Text), windowColor, darkPalette ? 0.88 : 0.72);
  const QColor disabledColor =
      blendColors(palette().color(QPalette::Disabled, QPalette::Text), windowColor, darkPalette ? 0.6 : 0.48);
  m_windowIconButton->setIcon(window()->windowIcon());
  m_minimizeButton->setIcon(multiStateStencilIcon(
      QStringLiteral(":/window-bar/minimize.svg"), buttonColor, buttonColor, disabledColor,
      QSize(kCustomWindowGlyphSize, kCustomWindowGlyphSize)));
  m_maximizeButton->setIcon(multiStateStencilIcon(
      maximized ? QStringLiteral(":/window-bar/restore.svg") : QStringLiteral(":/window-bar/maximize.svg"),
      buttonColor, buttonColor, disabledColor, QSize(kCustomWindowGlyphSize, kCustomWindowGlyphSize)));
  m_maximizeButton->setToolTip(maximized ? "Restore window" : "Maximize window");
  m_closeButton->setIcon(multiStateStencilIcon(
      QStringLiteral(":/window-bar/close.svg"), buttonColor,
      kWindowsCustomChrome ? QColor(Qt::white) : buttonColor, disabledColor,
      QSize(kCustomWindowGlyphSize, kCustomWindowGlyphSize)));
#else
  m_minimizeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMinButton, nullptr, this));
  m_maximizeButton->setIcon(style()->standardIcon(
      maximized ? QStyle::SP_TitleBarNormalButton : QStyle::SP_TitleBarMaxButton, nullptr, this));
  m_maximizeButton->setToolTip(maximized ? "Restore window" : "Maximize window");
  m_closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton, nullptr, this));
#endif
#endif
}
