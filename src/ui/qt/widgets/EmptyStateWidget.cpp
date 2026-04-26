/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "EmptyStateWidget.h"

#include <QColor>
#include <QBoxLayout>
#include <QEvent>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStackedLayout>
#include <QVBoxLayout>

#include "util/ColorHelpers.h"
#include "util/UIHelpers.h"

static constexpr int kEmptyStateHorizontalInset = 24;
static constexpr int kEmptyStateVerticalInset = 24;
static constexpr int kEmptyStateContentSpacing = 6;

EmptyStateWidget::EmptyStateWidget(const InstructionHint &headingHint,
                                   QWidget *contentWidget,
                                   QWidget *parent)
    : QWidget(parent),
      m_contentWidget(contentWidget),
      m_headingHint(headingHint) {
  if (!m_contentWidget) {
    m_contentWidget = new QWidget();
  }

  m_stack = new QStackedLayout(this);
  m_stack->setContentsMargins(0, 0, 0, 0);
  m_stack->setSpacing(0);
  m_stack->setStackingMode(QStackedLayout::StackAll);

  m_contentWidget->setParent(this);
  m_stack->addWidget(m_contentWidget);

  m_emptyView = new QWidget(this);
  m_emptyView->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  m_emptyView->setFocusPolicy(Qt::NoFocus);
  auto *emptyLayout = new QVBoxLayout(m_emptyView);
  emptyLayout->setContentsMargins(kEmptyStateHorizontalInset,
                                  kEmptyStateVerticalInset,
                                  kEmptyStateHorizontalInset,
                                  kEmptyStateVerticalInset);
  emptyLayout->setSpacing(0);
  emptyLayout->addStretch();

  m_emptyContent = new QWidget(m_emptyView);
  m_emptyContent->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  m_emptyContentLayout = new QBoxLayout(QBoxLayout::TopToBottom, m_emptyContent);
  m_emptyContentLayout->setContentsMargins(0, 0, 0, 0);
  m_emptyContentLayout->setSpacing(kEmptyStateContentSpacing);
  m_emptyContentLayout->setAlignment(Qt::AlignCenter);

  m_iconLabel = new QLabel(m_emptyContent);
  m_iconLabel->setAlignment(Qt::AlignCenter);
  m_iconLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  m_textContainer = new QWidget(m_emptyContent);
  m_textContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  auto *textLayout = new QVBoxLayout(m_textContainer);
  textLayout->setContentsMargins(0, 0, 0, 0);
  textLayout->setSpacing(kEmptyStateContentSpacing);

  m_headingLabel = new QLabel(m_textContainer);
  m_headingLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
  m_headingLabel->setWordWrap(true);
  m_headingLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);

  m_bodyLabel = new QLabel(m_textContainer);
  m_bodyLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
  m_bodyLabel->setWordWrap(true);
  m_bodyLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);

  textLayout->addWidget(m_headingLabel);
  textLayout->addWidget(m_bodyLabel);

  m_emptyContentLayout->addWidget(m_iconLabel);
  m_emptyContentLayout->addWidget(m_textContainer);

  emptyLayout->addWidget(m_emptyContent);
  emptyLayout->addStretch();

  m_stack->addWidget(m_emptyView);
  m_emptyView->hide();

  updateLayoutMode();
  refreshEmptyStatePresentation();
  setEmptyStateShown(false);
}

void EmptyStateWidget::setInstructionHint(const InstructionHint &headingHint) {
  m_headingHint = headingHint;
  refreshEmptyStatePresentation();
}

void EmptyStateWidget::setBodyText(const QString &body) {
  m_emptyBody = body;
  refreshEmptyStatePresentation();
}

void EmptyStateWidget::setEmptyStateContent(const QString &iconPath,
                                            const QString &heading,
                                            const QString &body) {
  m_headingHint.iconPath = iconPath;
  m_headingHint.text = heading;
  m_emptyBody = body;
  refreshEmptyStatePresentation();
}

void EmptyStateWidget::setCompactLayoutHeightThreshold(int threshold) {
  if (threshold < 0) {
    threshold = 0;
  }

  if (m_compactLayoutHeightThreshold == threshold) {
    return;
  }

  m_compactLayoutHeightThreshold = threshold;
  updateLayoutMode();
  refreshEmptyStatePresentation();
}

void EmptyStateWidget::setEmptyStateShown(bool shown) {
  if (m_emptyStateShown == shown) {
    return;
  }

  m_emptyStateShown = shown;
  m_emptyView->setVisible(m_emptyStateShown);
  m_stack->setCurrentWidget(m_emptyStateShown ? m_emptyView : m_contentWidget);
}

void EmptyStateWidget::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);

  if (!event) {
    return;
  }

  if (event->type() == QEvent::PaletteChange ||
      event->type() == QEvent::FontChange ||
      event->type() == QEvent::StyleChange) {
    refreshEmptyStatePresentation();
  }
}

void EmptyStateWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updateLayoutMode();
}

void EmptyStateWidget::updateLayoutMode() {
  if (!m_emptyContentLayout || !m_headingLabel || !m_bodyLabel || !m_textContainer) {
    return;
  }

  const bool compact = m_compactLayoutHeightThreshold > 0
      && height() < m_compactLayoutHeightThreshold;
  if (m_compactLayoutActive == compact) {
    return;
  }

  m_compactLayoutActive = compact;
  m_emptyContentLayout->setDirection(compact ? QBoxLayout::LeftToRight
                                             : QBoxLayout::TopToBottom);

  if (compact) {
    m_textContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_headingLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_bodyLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    m_headingLabel->setAlignment(Qt::AlignCenter);
    m_bodyLabel->setAlignment(Qt::AlignCenter);
    m_headingLabel->setWordWrap(false);
  } else {
    m_textContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_headingLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    m_bodyLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);

    m_headingLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_bodyLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_headingLabel->setWordWrap(true);
  }

  m_textContainer->updateGeometry();
  m_headingLabel->updateGeometry();
  m_bodyLabel->updateGeometry();
  m_emptyContent->updateGeometry();
}

void EmptyStateWidget::refreshEmptyStatePresentation() {
  InstructionHint headingHint = m_headingHint;
  if (headingHint.fontFamily.isEmpty()) {
    headingHint.fontFamily = font().family();
  }

  const InstructionMetrics headingMetrics = computeInstructionMetrics(headingHint, font());

  const QPalette palette = this->palette();
  const QPalette::ColorGroup colorGroup = palette.currentColorGroup();
  const QPalette::ColorRole headingForegroundRole = m_headingLabel->foregroundRole();
  const QPalette::ColorRole headingBackgroundRole = m_headingLabel->backgroundRole();
  const QPalette::ColorRole bodyForegroundRole = m_bodyLabel->foregroundRole();
  const QPalette::ColorRole bodyBackgroundRole = m_bodyLabel->backgroundRole();

  const QColor headingColor = blendColors(palette.color(colorGroup, headingForegroundRole),
                                          palette.color(colorGroup, headingBackgroundRole), 0.62);
  const QColor bodyColor = blendColors(palette.color(colorGroup, bodyForegroundRole),
                                       palette.color(colorGroup, bodyBackgroundRole), 0.48);
  const QColor iconColor = blendColors(palette.color(colorGroup, headingForegroundRole),
                                       palette.color(colorGroup, headingBackgroundRole), 0.40);

  QPalette headingPalette = m_headingLabel->palette();
  headingPalette.setColor(headingForegroundRole, headingColor);
  m_headingLabel->setPalette(headingPalette);

  QPalette bodyPalette = m_bodyLabel->palette();
  bodyPalette.setColor(bodyForegroundRole, bodyColor);
  m_bodyLabel->setPalette(bodyPalette);

  m_headingLabel->setFont(headingMetrics.font);
  m_headingLabel->setText(headingHint.text);

  const qreal devicePixelRatio = devicePixelRatioF();
  const QSize iconSize(headingMetrics.iconSide, headingMetrics.iconSide);
  const QPixmap icon = tintedIconPixmap(QIcon(headingHint.iconPath), iconSize,
                                        iconColor, devicePixelRatio);

  const bool hasIcon = !icon.isNull();
  m_iconLabel->setVisible(hasIcon);
  if (hasIcon) {
    m_iconLabel->setPixmap(icon);
  } else {
    m_iconLabel->clear();
  }

  const bool hasHeading = !headingHint.text.isEmpty();
  const bool hasBody = !m_emptyBody.isEmpty();
  m_headingLabel->setVisible(hasHeading);
  m_bodyLabel->setVisible(hasBody);

  if (hasBody) {
    const QFont bodyFont = prepareInstructionFont(font(), 1.20, QFont::Normal,
                                                  12, font().family());
    m_bodyLabel->setFont(bodyFont);
    m_bodyLabel->setText(m_emptyBody);
  } else {
    m_bodyLabel->clear();
  }
}
