/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SequenceControlBar.h"

#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace {
constexpr int kStripWidth = 126;
constexpr int kStripHeight = 102;
constexpr int kKnobSize = 36;
constexpr int kScrollStepPixels = kStripWidth / 2;
constexpr int kDefaultTempoBpm = 120;
constexpr int kMinTempoBpm = 20;
constexpr int kMaxTempoBpm = 360;
constexpr int kMinChannelValue = 0;
constexpr int kMaxChannelValue = 127;

class MixerKnob final : public QWidget {
public:
  explicit MixerKnob(QWidget* parent = nullptr) : QWidget(parent) {
    setFixedSize(kKnobSize, kKnobSize);
    setCursor(Qt::SizeVerCursor);
  }

  [[nodiscard]] int value() const { return m_value; }

  void setOnValueChanged(std::function<void(int)> callback) {
    m_onValueChanged = std::move(callback);
  }

  void setValue(int newValue) {
    const int clamped = std::clamp(newValue, m_minimum, m_maximum);
    if (m_value == clamped) {
      return;
    }

    m_value = clamped;
    update();
    if (m_onValueChanged) {
      m_onValueChanged(m_value);
    }
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds = rect().adjusted(1.0, 1.0, -1.0, -1.0);
    const QPointF center = bounds.center();
    const qreal radius = std::min(bounds.width(), bounds.height()) * 0.5;

    painter.save();
    if (!isEnabled()) {
      painter.setOpacity(0.42);
    }

    // Outer bezel.
    QRadialGradient bezelGradient(center, radius);
    bezelGradient.setColorAt(0.0, QColor(242, 244, 248));
    bezelGradient.setColorAt(0.42, QColor(170, 174, 182));
    bezelGradient.setColorAt(1.0, QColor(58, 62, 70));
    painter.setPen(Qt::NoPen);
    painter.setBrush(bezelGradient);
    painter.drawEllipse(bounds);

    // Knob face.
    const QRectF face = bounds.adjusted(3.0, 3.0, -3.0, -3.0);
    QRadialGradient faceGradient(face.center() + QPointF(-2.5, -3.0), face.width() * 0.58);
    faceGradient.setColorAt(0.0, QColor(250, 250, 250));
    faceGradient.setColorAt(0.35, QColor(191, 195, 204));
    faceGradient.setColorAt(1.0, QColor(94, 99, 108));
    painter.setBrush(faceGradient);
    painter.drawEllipse(face);

    // Specular highlight.
    QPainterPath highlight;
    highlight.addEllipse(face.adjusted(4.0, 3.0, -8.0, -11.0));
    painter.setBrush(QColor(255, 255, 255, 46));
    painter.drawPath(highlight);

    // Indicator line.
    const qreal valueRatio = static_cast<qreal>(m_value - m_minimum) /
                             static_cast<qreal>(m_maximum - m_minimum);
    const qreal angleDegrees = -140.0 + (valueRatio * 280.0);
    const qreal angleRadians = qDegreesToRadians(angleDegrees);
    const QPointF indicatorStart = center +
                                   QPointF(std::cos(angleRadians), std::sin(angleRadians)) *
                                       (radius * 0.24);
    const QPointF indicatorEnd = center +
                                 QPointF(std::cos(angleRadians), std::sin(angleRadians)) *
                                     (radius * 0.72);

    QPen indicatorPen(QColor(255, 202, 108), 2.1, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(indicatorPen);
    painter.drawLine(indicatorStart, indicatorEnd);

    painter.restore();
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton) {
      QWidget::mousePressEvent(event);
      return;
    }

    m_dragging = true;
    m_dragOrigin = event->globalPosition();
    m_dragStartValue = m_value;
    event->accept();
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (!m_dragging) {
      QWidget::mouseMoveEvent(event);
      return;
    }

    const qreal deltaY = event->globalPosition().y() - m_dragOrigin.y();
    const qreal sensitivity = (event->modifiers() & Qt::ShiftModifier) ? 0.22 : 0.78;
    const int candidate = m_dragStartValue - static_cast<int>(std::round(deltaY * sensitivity));
    setValue(candidate);
    event->accept();
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && m_dragging) {
      m_dragging = false;
      event->accept();
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

private:
  int m_minimum = kMinChannelValue;
  int m_maximum = kMaxChannelValue;
  int m_value = 64;
  bool m_dragging = false;
  QPointF m_dragOrigin;
  int m_dragStartValue = 64;
  std::function<void(int)> m_onValueChanged;
};

}  // namespace

struct SequenceControlBar::StripWidgets {
  int id = -1;
  int midiChannel = -1;
  QFrame* frame = nullptr;
  QLabel* title = nullptr;
  QLabel* subtitle = nullptr;
  QToolButton* muteButton = nullptr;
  QToolButton* soloButton = nullptr;
  QLabel* panLabel = nullptr;
  QLabel* volumeLabel = nullptr;
  MixerKnob* panKnob = nullptr;
  MixerKnob* volumeKnob = nullptr;
};

SequenceControlBar::SequenceControlBar(QWidget* parent)
    : QWidget(parent) {
  setObjectName(QStringLiteral("SequenceControlBar"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setMinimumHeight(112);

  auto* rootLayout = new QHBoxLayout(this);
  rootLayout->setContentsMargins(8, 6, 8, 6);
  rootLayout->setSpacing(8);

  auto* tempoFrame = new QFrame(this);
  tempoFrame->setObjectName(QStringLiteral("TempoBlock"));
  tempoFrame->setFixedWidth(134);

  auto* tempoLayout = new QVBoxLayout(tempoFrame);
  tempoLayout->setContentsMargins(8, 7, 8, 7);
  tempoLayout->setSpacing(4);

  auto* tempoLabel = new QLabel(QStringLiteral("Tempo"), tempoFrame);
  tempoLabel->setObjectName(QStringLiteral("TempoTitle"));
  tempoLayout->addWidget(tempoLabel);

  m_tempoSpin = new QDoubleSpinBox(tempoFrame);
  m_tempoSpin->setDecimals(2);
  m_tempoSpin->setRange(kMinTempoBpm, kMaxTempoBpm);
  m_tempoSpin->setSingleStep(0.25);
  m_tempoSpin->setKeyboardTracking(false);
  m_tempoSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  m_tempoSpin->setSuffix(QStringLiteral(" BPM"));
  m_tempoSpin->setValue(kDefaultTempoBpm);
  m_tempoSpin->setAlignment(Qt::AlignCenter);
  tempoLayout->addWidget(m_tempoSpin);

  rootLayout->addWidget(tempoFrame, 0);

  m_stripScroll = new QScrollArea(this);
  m_stripScroll->setObjectName(QStringLiteral("StripScroll"));
  m_stripScroll->setWidgetResizable(true);
  m_stripScroll->setFrameShape(QFrame::NoFrame);
  m_stripScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_stripScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  m_stripContainer = new QWidget(m_stripScroll);
  m_stripContainer->setObjectName(QStringLiteral("StripContainer"));
  m_stripLayout = new QHBoxLayout(m_stripContainer);
  m_stripLayout->setContentsMargins(0, 0, 0, 0);
  m_stripLayout->setSpacing(6);
  m_stripLayout->addStretch(1);
  m_stripScroll->setWidget(m_stripContainer);
  m_stripScroll->viewport()->installEventFilter(this);
  m_stripContainer->installEventFilter(this);

  rootLayout->addWidget(m_stripScroll, 1);

  m_scrollControls = new QWidget(this);
  auto* scrollLayout = new QHBoxLayout(m_scrollControls);
  scrollLayout->setContentsMargins(0, 0, 0, 0);
  scrollLayout->setSpacing(4);

  m_scrollLeft = new QToolButton(m_scrollControls);
  m_scrollLeft->setObjectName(QStringLiteral("StripScrollButton"));
  m_scrollLeft->setText(QStringLiteral("<"));
  m_scrollLeft->setAutoRaise(true);
  m_scrollLeft->setFixedSize(22, 22);
  scrollLayout->addWidget(m_scrollLeft);

  m_scrollRight = new QToolButton(m_scrollControls);
  m_scrollRight->setObjectName(QStringLiteral("StripScrollButton"));
  m_scrollRight->setText(QStringLiteral(">"));
  m_scrollRight->setAutoRaise(true);
  m_scrollRight->setFixedSize(22, 22);
  scrollLayout->addWidget(m_scrollRight);

  rootLayout->addWidget(m_scrollControls, 0, Qt::AlignVCenter);

  connect(m_tempoSpin,
          qOverload<double>(&QDoubleSpinBox::valueChanged),
          this,
          [this](double value) {
            if (!m_updatingUi) {
              emit tempoChanged(value);
            }
          });

  connect(m_scrollLeft, &QToolButton::clicked, this, [this]() { scrollBlocks(-kScrollStepPixels); });
  connect(m_scrollRight, &QToolButton::clicked, this, [this]() { scrollBlocks(kScrollStepPixels); });

  connect(m_stripScroll->horizontalScrollBar(),
          &QScrollBar::valueChanged,
          this,
          [this]() { refreshScrollControls(); });

  refreshStyleSheet();
  refreshScrollControls();
}

SequenceControlBar::~SequenceControlBar() = default;

double SequenceControlBar::tempoBpm() const {
  return m_tempoSpin ? m_tempoSpin->value() : kDefaultTempoBpm;
}

void SequenceControlBar::setTempoBpm(double bpm) {
  if (!m_tempoSpin) {
    return;
  }

  const double clamped = std::clamp(bpm, static_cast<double>(kMinTempoBpm), static_cast<double>(kMaxTempoBpm));
  if (std::abs(m_tempoSpin->value() - clamped) < 0.001) {
    return;
  }

  m_updatingUi = true;
  m_tempoSpin->setValue(clamped);
  m_updatingUi = false;
}

void SequenceControlBar::setStrips(const std::vector<StripConfig>& strips) {
  rebuildStrips(strips);
  refreshStripInteractivity();
  refreshScrollControls();
}

std::vector<int> SequenceControlBar::stripIds() const {
  std::vector<int> ids;
  ids.reserve(m_strips.size());
  for (const auto& strip : m_strips) {
    ids.push_back(strip->id);
  }
  return ids;
}

void SequenceControlBar::setStripPan(int stripId, int pan) {
  if (auto* strip = findStrip(stripId); strip && strip->panKnob) {
    m_updatingUi = true;
    strip->panKnob->setValue(std::clamp(pan, kMinChannelValue, kMaxChannelValue));
    m_updatingUi = false;
  }
}

void SequenceControlBar::setStripVolume(int stripId, int volume) {
  if (auto* strip = findStrip(stripId); strip && strip->volumeKnob) {
    m_updatingUi = true;
    strip->volumeKnob->setValue(std::clamp(volume, kMinChannelValue, kMaxChannelValue));
    m_updatingUi = false;
  }
}

void SequenceControlBar::setStripMuted(int stripId, bool muted) {
  if (auto* strip = findStrip(stripId); strip && strip->muteButton) {
    m_updatingUi = true;
    strip->muteButton->setChecked(muted);
    m_updatingUi = false;
    refreshStripInteractivity();
  }
}

void SequenceControlBar::setStripSolo(int stripId, bool solo) {
  if (auto* strip = findStrip(stripId); strip && strip->soloButton) {
    m_updatingUi = true;
    strip->soloButton->setChecked(solo);
    m_updatingUi = false;
    refreshStripInteractivity();
  }
}

bool SequenceControlBar::stripMuted(int stripId) const {
  if (const auto* strip = findStrip(stripId); strip && strip->muteButton) {
    return strip->muteButton->isChecked();
  }
  return false;
}

bool SequenceControlBar::stripSolo(int stripId) const {
  if (const auto* strip = findStrip(stripId); strip && strip->soloButton) {
    return strip->soloButton->isChecked();
  }
  return false;
}

void SequenceControlBar::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  refreshScrollControls();
}

bool SequenceControlBar::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_stripContainer || watched == m_stripScroll->viewport()) {
    switch (event->type()) {
      case QEvent::Resize:
      case QEvent::LayoutRequest:
      case QEvent::Show:
        refreshScrollControls();
        break;
      default:
        break;
    }
  }

  return QWidget::eventFilter(watched, event);
}

SequenceControlBar::StripWidgets* SequenceControlBar::findStrip(int stripId) {
  auto it = std::find_if(m_strips.begin(), m_strips.end(), [stripId](const auto& strip) {
    return strip && strip->id == stripId;
  });
  return (it == m_strips.end() || !(*it)) ? nullptr : it->get();
}

const SequenceControlBar::StripWidgets* SequenceControlBar::findStrip(int stripId) const {
  auto it = std::find_if(m_strips.begin(), m_strips.end(), [stripId](const auto& strip) {
    return strip && strip->id == stripId;
  });
  return (it == m_strips.end() || !(*it)) ? nullptr : it->get();
}

void SequenceControlBar::rebuildStrips(const std::vector<StripConfig>& strips) {
  if (!m_stripLayout || !m_stripContainer) {
    return;
  }

  m_updatingUi = true;

  while (QLayoutItem* item = m_stripLayout->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }

  m_strips.clear();
  m_strips.reserve(strips.size());

  for (const auto& config : strips) {
    auto strip = std::make_unique<StripWidgets>();
    strip->id = config.id;
    strip->midiChannel = config.midiChannel;

    strip->frame = new QFrame(m_stripContainer);
    strip->frame->setObjectName(QStringLiteral("MixerStrip"));
    strip->frame->setProperty("dimmed", false);
    strip->frame->setProperty("solo", false);
    strip->frame->setFixedSize(kStripWidth, kStripHeight);

    auto* stripLayout = new QVBoxLayout(strip->frame);
    stripLayout->setContentsMargins(7, 6, 7, 6);
    stripLayout->setSpacing(3);

    strip->title = new QLabel(config.title, strip->frame);
    strip->title->setObjectName(QStringLiteral("StripTitle"));
    strip->title->setAlignment(Qt::AlignCenter);
    stripLayout->addWidget(strip->title);

    strip->subtitle = new QLabel(config.subtitle, strip->frame);
    strip->subtitle->setObjectName(QStringLiteral("StripSubtitle"));
    strip->subtitle->setAlignment(Qt::AlignCenter);
    stripLayout->addWidget(strip->subtitle);

    auto* buttonsLayout = new QHBoxLayout();
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    buttonsLayout->setSpacing(4);

    strip->muteButton = new QToolButton(strip->frame);
    strip->muteButton->setObjectName(QStringLiteral("StripToggle"));
    strip->muteButton->setCheckable(true);
    strip->muteButton->setText(QStringLiteral("M"));
    strip->muteButton->setFixedSize(22, 18);
    strip->muteButton->setToolTip(QStringLiteral("Mute"));
    buttonsLayout->addWidget(strip->muteButton);

    strip->soloButton = new QToolButton(strip->frame);
    strip->soloButton->setObjectName(QStringLiteral("StripToggle"));
    strip->soloButton->setCheckable(true);
    strip->soloButton->setText(QStringLiteral("S"));
    strip->soloButton->setFixedSize(22, 18);
    strip->soloButton->setToolTip(QStringLiteral("Solo"));
    buttonsLayout->addWidget(strip->soloButton);

    buttonsLayout->addStretch(1);
    stripLayout->addLayout(buttonsLayout);

    auto* knobRow = new QHBoxLayout();
    knobRow->setContentsMargins(0, 0, 0, 0);
    knobRow->setSpacing(8);

    auto* panColumn = new QVBoxLayout();
    panColumn->setContentsMargins(0, 0, 0, 0);
    panColumn->setSpacing(2);
    strip->panKnob = new MixerKnob(strip->frame);
    strip->panKnob->setValue(std::clamp(config.pan, kMinChannelValue, kMaxChannelValue));
    panColumn->addWidget(strip->panKnob, 0, Qt::AlignHCenter);
    strip->panLabel = new QLabel(QStringLiteral("PAN"), strip->frame);
    strip->panLabel->setObjectName(QStringLiteral("StripValueLabel"));
    strip->panLabel->setAlignment(Qt::AlignCenter);
    panColumn->addWidget(strip->panLabel);
    knobRow->addLayout(panColumn, 1);

    auto* volColumn = new QVBoxLayout();
    volColumn->setContentsMargins(0, 0, 0, 0);
    volColumn->setSpacing(2);
    strip->volumeKnob = new MixerKnob(strip->frame);
    strip->volumeKnob->setValue(std::clamp(config.volume, kMinChannelValue, kMaxChannelValue));
    volColumn->addWidget(strip->volumeKnob, 0, Qt::AlignHCenter);
    strip->volumeLabel = new QLabel(QStringLiteral("VOL"), strip->frame);
    strip->volumeLabel->setObjectName(QStringLiteral("StripValueLabel"));
    strip->volumeLabel->setAlignment(Qt::AlignCenter);
    volColumn->addWidget(strip->volumeLabel);
    knobRow->addLayout(volColumn, 1);

    stripLayout->addLayout(knobRow, 1);

    m_stripLayout->addWidget(strip->frame, 0, Qt::AlignVCenter);

    connect(strip->muteButton, &QToolButton::toggled, this, [this, id = strip->id](bool checked) {
      if (m_updatingUi) {
        return;
      }

      if (checked) {
        if (auto* selfStrip = findStrip(id); selfStrip && selfStrip->soloButton->isChecked()) {
          m_updatingUi = true;
          selfStrip->soloButton->setChecked(false);
          m_updatingUi = false;
        }
      }

      refreshStripInteractivity();
      emit stripMuteChanged(id, checked);
    });

    connect(strip->soloButton, &QToolButton::toggled, this, [this, id = strip->id](bool checked) {
      if (m_updatingUi) {
        return;
      }

      if (checked) {
        m_updatingUi = true;
        for (auto& other : m_strips) {
          if (other && other->id != id && other->soloButton->isChecked()) {
            other->soloButton->setChecked(false);
          }
        }
        m_updatingUi = false;
      }

      refreshStripInteractivity();
      emit stripSoloChanged(id, checked);
    });

    strip->panKnob->setOnValueChanged([this, id = strip->id](int value) {
      if (!m_updatingUi) {
        emit stripPanChanged(id, value);
      }
    });

    strip->volumeKnob->setOnValueChanged([this, id = strip->id](int value) {
      if (!m_updatingUi) {
        emit stripVolumeChanged(id, value);
      }
    });

    m_strips.push_back(std::move(strip));
  }

  m_stripLayout->addStretch(1);
  m_updatingUi = false;
}

void SequenceControlBar::refreshStripInteractivity() {
  const bool anySolo = std::any_of(m_strips.begin(), m_strips.end(), [](const auto& strip) {
    return strip && strip->soloButton && strip->soloButton->isChecked();
  });

  for (auto& stripPtr : m_strips) {
    if (!stripPtr) {
      continue;
    }
    auto& strip = *stripPtr;
    const bool muted = strip.muteButton && strip.muteButton->isChecked();
    const bool soloed = strip.soloButton && strip.soloButton->isChecked();
    const bool blockedBySolo = anySolo && !soloed;
    const bool controlsDisabled = muted || blockedBySolo;

    if (strip.frame) {
      strip.frame->setProperty("dimmed", controlsDisabled);
      strip.frame->setProperty("solo", soloed);
      style()->unpolish(strip.frame);
      style()->polish(strip.frame);
      strip.frame->update();
    }

    if (strip.muteButton) {
      strip.muteButton->setEnabled(!blockedBySolo);
    }
    if (strip.soloButton) {
      strip.soloButton->setEnabled(!muted && !blockedBySolo);
    }
    if (strip.panKnob) {
      strip.panKnob->setEnabled(!controlsDisabled);
    }
    if (strip.volumeKnob) {
      strip.volumeKnob->setEnabled(!controlsDisabled);
    }
    if (strip.panLabel) {
      strip.panLabel->setEnabled(!controlsDisabled);
    }
    if (strip.volumeLabel) {
      strip.volumeLabel->setEnabled(!controlsDisabled);
    }
  }
}

void SequenceControlBar::refreshScrollControls() {
  if (!m_stripScroll || !m_scrollControls) {
    return;
  }

  auto* hbar = m_stripScroll->horizontalScrollBar();
  if (!hbar) {
    m_scrollControls->setVisible(false);
    return;
  }

  const bool hasOverflow = hbar->maximum() > 0;
  m_scrollControls->setVisible(hasOverflow);

  if (!hasOverflow) {
    hbar->setValue(0);
    return;
  }

  m_scrollLeft->setEnabled(hbar->value() > hbar->minimum());
  m_scrollRight->setEnabled(hbar->value() < hbar->maximum());
}

void SequenceControlBar::scrollBlocks(int deltaPixels) {
  if (!m_stripScroll) {
    return;
  }

  auto* hbar = m_stripScroll->horizontalScrollBar();
  if (!hbar) {
    return;
  }

  hbar->setValue(hbar->value() + deltaPixels);
  refreshScrollControls();
}

void SequenceControlBar::refreshStyleSheet() {
  const QColor base = palette().color(QPalette::Window);
  const QColor text = palette().color(QPalette::WindowText);

  const QColor barBg = base.darker(112);
  const QColor blockBg = base.darker(124);
  const QColor blockBorder = base.lighter(108);

  QColor subtleText = text;
  subtleText.setAlpha(160);

  QColor buttonOff = QColor(79, 86, 98);
  QColor buttonOn = QColor(230, 176, 84);
  QColor buttonText = QColor(240, 242, 246);

  const QString style = QStringLiteral(
      "QWidget#SequenceControlBar {"
      " border-top: 1px solid rgba(255,255,255,0.08);"
      " border-bottom: 1px solid rgba(0,0,0,0.32);"
      " background: %1;"
      "}"
      "QFrame#TempoBlock {"
      " border: 1px solid rgba(%2,%3,%4,210);"
      " border-radius: 7px;"
      " background: %5;"
      "}"
      "QLabel#TempoTitle {"
      " font-size: 10px;"
      " font-weight: 600;"
      " letter-spacing: 0.5px;"
      " color: rgba(%6,%7,%8,%9);"
      "}"
      "QDoubleSpinBox {"
      " border: 1px solid rgba(255,255,255,0.09);"
      " border-radius: 6px;"
      " padding: 3px 6px;"
      " color: palette(text);"
      " background: rgba(0,0,0,0.25);"
      " selection-background-color: rgba(255,255,255,0.2);"
      "}"
      "QFrame#MixerStrip {"
      " border: 1px solid rgba(%2,%3,%4,224);"
      " border-radius: 7px;"
      " background: %10;"
      "}"
      "QFrame#MixerStrip[dimmed=\"true\"] {"
      " background: rgba(0,0,0,0.42);"
      " border: 1px solid rgba(255,255,255,0.08);"
      "}"
      "QFrame#MixerStrip[solo=\"true\"] {"
      " border: 1px solid rgba(%11,%12,%13,220);"
      "}"
      "QLabel#StripTitle {"
      " font-size: 9px;"
      " font-weight: 600;"
      " letter-spacing: 0.4px;"
      " color: palette(text);"
      "}"
      "QLabel#StripSubtitle {"
      " font-size: 8px;"
      " color: rgba(%6,%7,%8,%9);"
      "}"
      "QLabel#StripValueLabel {"
      " font-size: 7px;"
      " font-weight: 600;"
      " letter-spacing: 0.5px;"
      " color: rgba(%6,%7,%8,%9);"
      "}"
      "QToolButton#StripToggle {"
      " border: 1px solid rgba(255,255,255,0.14);"
      " border-radius: 5px;"
      " background: rgba(%14,%15,%16,200);"
      " color: rgba(%17,%18,%19,220);"
      " font-size: 9px;"
      " font-weight: 700;"
      "}"
      "QToolButton#StripToggle:checked {"
      " background: rgba(%20,%21,%22,230);"
      " color: rgba(%17,%18,%19,255);"
      " border: 1px solid rgba(255,255,255,0.28);"
      "}"
      "QToolButton#StripToggle:disabled {"
      " background: rgba(0,0,0,0.28);"
      " color: rgba(255,255,255,0.22);"
      " border: 1px solid rgba(255,255,255,0.08);"
      "}"
      "QToolButton#StripScrollButton {"
      " border: none;"
      " border-radius: 7px;"
      " background: transparent;"
      " color: palette(text);"
      " font-weight: 600;"
      "}"
      "QToolButton#StripScrollButton:hover {"
      " background: rgba(255,255,255,0.14);"
      "}"
      "QToolButton#StripScrollButton:pressed {"
      " background: rgba(255,255,255,0.2);"
      "}"
      "QToolButton#StripScrollButton:disabled {"
      " color: rgba(255,255,255,0.26);"
      " background: transparent;"
      "}")
                           .arg(barBg.name())
                           .arg(blockBorder.red())
                           .arg(blockBorder.green())
                           .arg(blockBorder.blue())
                           .arg(blockBg.name())
                           .arg(subtleText.red())
                           .arg(subtleText.green())
                           .arg(subtleText.blue())
                           .arg(subtleText.alpha())
                           .arg(base.darker(132).name())
                           .arg(buttonOn.red())
                           .arg(buttonOn.green())
                           .arg(buttonOn.blue())
                           .arg(buttonOff.red())
                           .arg(buttonOff.green())
                           .arg(buttonOff.blue())
                           .arg(buttonText.red())
                           .arg(buttonText.green())
                           .arg(buttonText.blue())
                           .arg(buttonOn.red())
                           .arg(buttonOn.green())
                           .arg(buttonOn.blue());

  if (styleSheet() != style) {
    setStyleSheet(style);
  }
}
