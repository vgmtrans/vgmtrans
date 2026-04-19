/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#include "EmptyStateWidget.h"

#include <QColor>
#include <QEvent>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QSizePolicy>
#include <QStackedLayout>
#include <QVBoxLayout>

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

  m_iconLabel = new QLabel(m_emptyView);
  m_iconLabel->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
  m_iconLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  m_headingLabel = new QLabel(m_emptyView);
  m_headingLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
  m_headingLabel->setWordWrap(true);
  m_headingLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);

  m_bodyLabel = new QLabel(m_emptyView);
  m_bodyLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
  m_bodyLabel->setWordWrap(true);
  m_bodyLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);

  emptyLayout->addWidget(m_iconLabel, 0, Qt::AlignHCenter);
  emptyLayout->addWidget(m_headingLabel);
  emptyLayout->addWidget(m_bodyLabel);
  emptyLayout->addStretch();

  m_stack->addWidget(m_emptyView);
  m_emptyView->hide();

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

void EmptyStateWidget::refreshEmptyStatePresentation() {
  InstructionHint headingHint = m_headingHint;
  if (headingHint.fontFamily.isEmpty()) {
    headingHint.fontFamily = font().family();
  }

  const InstructionMetrics headingMetrics = computeInstructionMetrics(headingHint, font());

  QColor headingColor = palette().color(QPalette::WindowText);
  QColor bodyColor = headingColor;
  QColor iconColor = headingColor;
  headingColor.setAlphaF(0.62);
  bodyColor.setAlphaF(0.48);
  iconColor.setAlphaF(0.40);

  QPalette headingPalette = m_headingLabel->palette();
  headingPalette.setColor(QPalette::WindowText, headingColor);
  m_headingLabel->setPalette(headingPalette);

  QPalette bodyPalette = m_bodyLabel->palette();
  bodyPalette.setColor(QPalette::WindowText, bodyColor);
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

  m_iconLabel->setContentsMargins(0, 0, 0, hasIcon ? headingMetrics.spacing : 0);
  m_bodyLabel->setContentsMargins(0,
                                  (hasHeading && hasBody) ? kEmptyStateContentSpacing : 0,
                                  0,
                                  0);
}
