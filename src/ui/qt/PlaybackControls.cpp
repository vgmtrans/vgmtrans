/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PlaybackControls.h"

#include <QEvent>
#include <QLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QWhatsThis>

#include "services/NotificationCenter.h"
#include "SequencePlayer.h"
#include "SeekBar.h"
#include "UIHelpers.h"

namespace {
constexpr int kTransportControlHeight = 32;
constexpr int kTransportButtonSize = 32;
constexpr int kTransportIconSize = 28;
constexpr int kSeekBarVisibleWidthThreshold = 125;
constexpr int kInactiveTransportIconAlpha = 120;
const QColor kDarkPlayColor(0x2f, 0xbf, 0x71);
const QColor kLightPlayColor(0x24, 0x96, 0x59);
const QColor kDarkStopColor(0xd8, 0x6b, 0x6b);
const QColor kLightStopColor(0xb8, 0x4f, 0x4f);

QIcon gradientTransportIcon(const QString &iconPath, QColor baseColor) {
  const int alpha = baseColor.alpha();
  QColor startColor = blendColors(baseColor, QColor(Qt::white), 0.8);
  QColor endColor = blendColors(baseColor, QColor(Qt::black), 0.7);
  startColor.setAlpha(alpha);
  endColor.setAlpha(alpha);
  return gradientStencilSvgIcon(iconPath, startColor, endColor);
}
}

PlaybackControls::PlaybackControls(QWidget *parent) : QWidget(parent) {
  auto *barLayout = new QHBoxLayout();
  barLayout->setContentsMargins(0, 0, 0, 0);
  setLayout(barLayout);
  setupControls();
}

void PlaybackControls::setupControls() {
  auto *barLayout = static_cast<QHBoxLayout *>(layout());
  const bool darkPalette = isDarkPalette(palette());
  const QColor playColor = darkPalette ? kDarkPlayColor : kLightPlayColor;
  const QColor stopColor = darkPalette ? kDarkStopColor : kLightStopColor;
  const QString buttonStyle = toolBarButtonStyle(palette());

  auto *buttonGroup = new QWidget(this);
  auto *buttonGroupLayout = new QHBoxLayout(buttonGroup);
  buttonGroupLayout->setContentsMargins(0, 0, 0, 0);
  buttonGroupLayout->setSpacing(barLayout->spacing());

  const auto makeButton = [&buttonStyle, buttonGroup](const QString &iconPath, const QString &toolTip,
                                               const QColor &color) {
    auto *button = new QToolButton(buttonGroup);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::ArrowCursor);
    button->setFixedSize(kTransportButtonSize, kTransportButtonSize);
    button->setIconSize(QSize(kTransportIconSize, kTransportIconSize));
    button->setStyleSheet(buttonStyle);
    button->setIcon(gradientTransportIcon(iconPath, color));
    button->setToolTip(toolTip);
    return button;
  };

  m_play = makeButton(QStringLiteral(":/icons/play.svg"), QStringLiteral("Play selected collection (Space)"), playColor);
  m_play->setEnabled(false);
  m_play->setWhatsThis("Select a collection in the panel above and click this \u25b6 button or press 'Space' to play it.\n"
                       "Clicking the button again will pause playback or play a different collection "
                       "if you have changed the selection.");
  connect(m_play, &QToolButton::pressed, this, &PlaybackControls::playToggle);
  buttonGroupLayout->addWidget(m_play);

  m_stop = makeButton(QStringLiteral(":/icons/stop.svg"), QStringLiteral("Stop playback (Esc)"), stopColor);
  m_stop->setEnabled(false);
  connect(m_stop, &QToolButton::pressed, this, &PlaybackControls::stopPressed);
  buttonGroupLayout->addWidget(m_stop);

  buttonGroup->setFixedWidth(buttonGroup->sizeHint().width());
  barLayout->addWidget(buttonGroup);

  m_slider = new SeekBar();
  /* Needed to make sure the slider is properly rendered */
  m_slider->setRange(0, 1);
  m_slider->setValue(0);
  m_slider->setFixedHeight(kTransportControlHeight);
  m_slider->setMinimumWidth(0);
  QSizePolicy sliderPolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  sliderPolicy.setRetainSizeWhenHidden(true);
  m_slider->setSizePolicy(sliderPolicy);
  m_slider->setEnabled(false);
  m_slider->setToolTip("Seek");
  connect(m_slider, &SeekBar::sliderMoved, [this](int value) {
    seekingTo(value, PositionChangeOrigin::SeekBar);
  });
  connect(m_slider, &SeekBar::sliderReleased, [this]() {
    seekingTo(m_slider->value(), PositionChangeOrigin::SeekBar);
  });
  barLayout->addWidget(m_slider, 1);

  connect(NotificationCenter::the(), &NotificationCenter::vgmCollSelected, this,
          [this](VGMColl *coll, QWidget *) {
            m_hasSelectedCollection = coll != nullptr;
            playerStatusChanged(SequencePlayer::the().playing());
          });
  connect(&SequencePlayer::the(), &SequencePlayer::statusChange, this, &PlaybackControls::playerStatusChanged);
  connect(&SequencePlayer::the(), &SequencePlayer::playbackPositionChanged, this,
          &PlaybackControls::playbackRangeUpdate);
  updateSeekBarVisibility();
  playerStatusChanged(SequencePlayer::the().playing());
}

void PlaybackControls::showPlayInfo() {
  QWhatsThis::showText(m_play->mapToGlobal(m_play->pos()), m_play->whatsThis(), this);
  m_play->clearFocus();
}

void PlaybackControls::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
    const QString buttonStyle = toolBarButtonStyle(palette());
    m_play->setStyleSheet(buttonStyle);
    m_stop->setStyleSheet(buttonStyle);
    playerStatusChanged(SequencePlayer::the().playing());
  }
}

void PlaybackControls::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updateSeekBarVisibility();
}

void PlaybackControls::playbackRangeUpdate(int cur, int max, PositionChangeOrigin origin) {
  const int previousMaximum = m_slider->maximum();
  const bool rangeChanged = max != previousMaximum;
  const bool forceImmediateUpdate = cur == m_slider->minimum() || rangeChanged;

  if (rangeChanged) {
    m_slider->setRange(0, max);
  }

  if (m_slider->isSliderDown()) {
    m_skipNextPlaybackSliderUpdate = false;
    return;
  }

  if (origin == PositionChangeOrigin::Playback && !forceImmediateUpdate) {
    m_skipNextPlaybackSliderUpdate = !m_skipNextPlaybackSliderUpdate;
    if (m_skipNextPlaybackSliderUpdate) {
      return;
    }
  } else {
    m_skipNextPlaybackSliderUpdate = false;
  }

  m_slider->setValue(cur);
}

void PlaybackControls::playerStatusChanged(bool playing) {
  m_skipNextPlaybackSliderUpdate = false;
  const bool hasActive = SequencePlayer::the().activeCollection() != nullptr;
  const bool canPlay = m_hasSelectedCollection || hasActive;
  const bool darkPalette = isDarkPalette(palette());

  m_play->setEnabled(canPlay);

  QColor playColor = darkPalette ? kDarkPlayColor : kLightPlayColor;
  if (!(playing || canPlay)) {
    playColor.setAlpha(kInactiveTransportIconAlpha);
  }
  m_play->setIcon(gradientTransportIcon(playing ? QStringLiteral(":/icons/pause.svg")
                                                : QStringLiteral(":/icons/play.svg"),
                                        playColor));

  QColor stopColor = darkPalette ? kDarkStopColor : kLightStopColor;
  m_stop->setEnabled(hasActive);
  if (!m_stop->isEnabled()) {
    stopColor.setAlpha(kInactiveTransportIconAlpha);
  }
  m_stop->setIcon(gradientTransportIcon(QStringLiteral(":/icons/stop.svg"), stopColor));
  m_slider->setEnabled(hasActive);
}

void PlaybackControls::updateSeekBarVisibility() {
  if (m_slider) {
    m_slider->setVisible(contentsRect().width() >= kSeekBarVisibleWidthThreshold);
  }
}
