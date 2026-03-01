/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SequenceControlBar.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

#include "util/Metrics.h"

namespace {
constexpr int kBarHeight = (Size::VTab * 3) / 2;
constexpr int kBlockWidth = 60;
constexpr int kBlockHeight = kBarHeight;
constexpr int kScrollButtonWidth = 22;
constexpr int kKnobSize = 20;
constexpr int kScrollStepPixels = kBlockWidth;
constexpr int kTempoControlWidth = 78;
constexpr int kDefaultTempoBpm = 120;
constexpr int kMinTempoBpm = 20;
constexpr int kMaxTempoBpm = 360;
constexpr int kMinChannelValue = 0;
constexpr int kMaxChannelValue = 127;
constexpr int kKnobCenterValue = 64;
constexpr qreal kKnobStartAngleDegrees = 130.0;   // left edge of the dead-zone seam
constexpr qreal kKnobCenterAngleDegrees = 270.0;  // straight up
constexpr qreal kKnobEndAngleDegrees = 410.0;     // right edge of the dead-zone seam

qreal knobPointerAngleDegrees(int value) {
  const int v = std::clamp(value, kMinChannelValue, kMaxChannelValue);
  if (v <= kKnobCenterValue) {
    const qreal t = static_cast<qreal>(v) / static_cast<qreal>(kKnobCenterValue);
    return kKnobStartAngleDegrees +
           (kKnobCenterAngleDegrees - kKnobStartAngleDegrees) * t;
  }

  const qreal t =
      static_cast<qreal>(v - kKnobCenterValue) /
      static_cast<qreal>(kMaxChannelValue - kKnobCenterValue);
  return kKnobCenterAngleDegrees +
         (kKnobEndAngleDegrees - kKnobCenterAngleDegrees) * t;
}

QColor sequenceControlBarBackgroundColor(const QWidget* context) {
  if (context && context->window()) {
    if (auto* tabBar = context->window()->findChild<QTabBar*>()) {
      QColor tabColor = tabBar->palette().color(QPalette::Window);
      if (!tabColor.isValid()) {
        tabColor = tabBar->palette().color(QPalette::Midlight);
      }
      if (!tabColor.isValid()) {
        tabColor = tabBar->palette().color(QPalette::Button);
      }
      if (tabColor.isValid()) {
        return tabColor;
      }
    }
  }

  const QPalette palette = context ? context->palette() : QPalette();
  QColor color = palette.color(QPalette::Button);
  if (!color.isValid()) {
    color = palette.color(QPalette::Window);
  }
  if (!color.isValid()) {
    color = QColor(112, 118, 126);
  }
  return color;
}

class BlockScrollButton final : public QToolButton {
public:
  enum class Direction { Left, Right };

  explicit BlockScrollButton(Direction direction, QWidget* parent = nullptr)
      : QToolButton(parent), m_direction(direction) {
    setObjectName(QStringLiteral("BlockScrollButton"));
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(kScrollButtonWidth, kBlockHeight);
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor background(0, 0, 0, 0);
    if (isEnabled()) {
      if (isDown()) {
        background = QColor(255, 255, 255, 40);
      } else if (underMouse()) {
        background = QColor(255, 255, 255, 26);
      }
    } else {
      background = QColor(255, 255, 255, 10);
    }

    if (background.alpha() > 0) {
      painter.fillRect(rect(), background);
    }

    QColor glyph = palette().color(QPalette::WindowText);
    glyph.setAlpha(isEnabled() ? 210 : 88);
    QPen chevronPen(glyph, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(chevronPen);

    const qreal midX = rect().center().x();
    const qreal midY = rect().center().y();
    const qreal halfW = std::max<qreal>(3.4, width() * 0.18 * 0.8);
    const qreal halfH = std::max<qreal>(4.6, height() * 0.20 * 0.8);

    QPainterPath chevron;
    if (m_direction == Direction::Left) {
      chevron.moveTo(midX + halfW, midY - halfH);
      chevron.lineTo(midX - halfW, midY);
      chevron.lineTo(midX + halfW, midY + halfH);
    } else {
      chevron.moveTo(midX - halfW, midY - halfH);
      chevron.lineTo(midX + halfW, midY);
      chevron.lineTo(midX - halfW, midY + halfH);
    }
    painter.drawPath(chevron);
  }

private:
  Direction m_direction;
};

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

  void setBubblePrefix(QString prefix) {
    m_bubblePrefix = std::move(prefix);
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

    const bool lightMode = qGray(palette().color(QPalette::Window).rgb()) > 140;
    const QRectF bounds = rect().adjusted(1.0, 1.0, -1.0, -1.0);
    const QPointF center = bounds.center();
    const qreal radius = std::min(bounds.width(), bounds.height()) * 0.5;

    painter.save();
    if (!isEnabled()) {
      painter.setOpacity(0.42);
    }

    // Matte outer ring.
    painter.setPen(Qt::NoPen);
    painter.setBrush(lightMode ? QColor(116, 123, 134) : QColor(34, 38, 45));
    painter.drawEllipse(bounds);

    // Knob face: darker with subtle gradient shading.
    const QRectF face = bounds.adjusted(2.2, 2.2, -2.2, -2.2);
    QLinearGradient faceGradient(face.topLeft(), face.bottomRight());
    if (lightMode) {
      faceGradient.setColorAt(0.0, QColor(228, 233, 240));
      faceGradient.setColorAt(0.55, QColor(196, 204, 214));
      faceGradient.setColorAt(1.0, QColor(158, 168, 181));
    } else {
      faceGradient.setColorAt(0.0, QColor(116, 124, 138));
      faceGradient.setColorAt(0.55, QColor(82, 90, 104));
      faceGradient.setColorAt(1.0, QColor(54, 60, 72));
    }
    painter.setBrush(faceGradient);
    painter.drawEllipse(face);

    // Small top-left highlight to mimic hardware knob sheen.
    painter.setBrush(lightMode ? QColor(255, 255, 255, 78) : QColor(255, 255, 255, 28));
    painter.drawEllipse(face.adjusted(3.6, 2.8, -8.8, -9.4));

    // Indicator line.
    const qreal angleDegrees = knobPointerAngleDegrees(m_value);
    const qreal angleRadians = qDegreesToRadians(angleDegrees);
    const QPointF indicatorStart = center +
                                   QPointF(std::cos(angleRadians), std::sin(angleRadians)) *
                                       (radius * 0.24);
    const QPointF indicatorEnd = center +
                                 QPointF(std::cos(angleRadians), std::sin(angleRadians)) *
                                     (radius * 0.72);

    QPen indicatorPen(lightMode ? QColor(44, 50, 60) : QColor(255, 202, 108),
                      1.35,
                      Qt::SolidLine,
                      Qt::RoundCap);
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
    showValueBubble(event->globalPosition().toPoint());
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
    showValueBubble(event->globalPosition().toPoint());
    event->accept();
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && m_dragging) {
      m_dragging = false;
      QToolTip::hideText();
      event->accept();
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void showValueBubble(const QPoint& globalPos) {
    const QString prefix = m_bubblePrefix.isEmpty() ? QStringLiteral("VAL") : m_bubblePrefix;
    const QString text = QStringLiteral("%1: %2").arg(prefix).arg(m_value);
    QToolTip::showText(globalPos + QPoint(12, -20), text, this, rect());
  }

private:
  int m_minimum = kMinChannelValue;
  int m_maximum = kMaxChannelValue;
  int m_value = 64;
  bool m_dragging = false;
  QPointF m_dragOrigin;
  int m_dragStartValue = 64;
  std::function<void(int)> m_onValueChanged;
  QString m_bubblePrefix;
};

}  // namespace

struct SequenceControlBar::BlockWidgets {
  int id = -1;
  int midiChannel = -1;
  QColor borderColor;
  QFrame* frame = nullptr;
  QToolButton* muteButton = nullptr;
  QToolButton* soloButton = nullptr;
  MixerKnob* panKnob = nullptr;
  MixerKnob* volumeKnob = nullptr;
};

SequenceControlBar::SequenceControlBar(QWidget* parent)
    : QWidget(parent) {
  setObjectName(QStringLiteral("SequenceControlBar"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFixedHeight(kBarHeight);

  auto* rootLayout = new QHBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  auto* tempoFrame = new QFrame(this);
  tempoFrame->setObjectName(QStringLiteral("TempoBlock"));
  tempoFrame->setFrameShape(QFrame::NoFrame);
  tempoFrame->setFixedWidth(kTempoControlWidth);
  tempoFrame->setFixedHeight(kBlockHeight);

  auto* tempoLayout = new QVBoxLayout(tempoFrame);
  tempoLayout->setContentsMargins(8, 5, 4, 0);
  tempoLayout->setSpacing(0);

  auto* tempoLabel = new QLabel(QStringLiteral("Tempo"), tempoFrame);
  tempoLabel->setObjectName(QStringLiteral("TempoTitle"));
  tempoLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  tempoLayout->addWidget(tempoLabel, 0, Qt::AlignLeft | Qt::AlignTop);

  m_tempoSpin = new QDoubleSpinBox(tempoFrame);
  m_tempoSpin->setDecimals(2);
  m_tempoSpin->setRange(kMinTempoBpm, kMaxTempoBpm);
  m_tempoSpin->setSingleStep(0.25);
  m_tempoSpin->setKeyboardTracking(false);
  m_tempoSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
  m_tempoSpin->setValue(kDefaultTempoBpm);
  m_tempoSpin->setAlignment(Qt::AlignCenter);
  m_tempoSpin->setObjectName(QStringLiteral("TempoSpin"));
  m_tempoSpin->setFixedHeight(20);

  auto* tempoValueLayout = new QHBoxLayout();
  tempoValueLayout->setContentsMargins(0, 4, 0, 0);
  tempoValueLayout->setSpacing(4);
  tempoValueLayout->addWidget(m_tempoSpin, 0, Qt::AlignLeft | Qt::AlignTop);

  auto* bpmLabel = new QLabel(QStringLiteral("BPM"), tempoFrame);
  bpmLabel->setObjectName(QStringLiteral("TempoUnit"));
  tempoValueLayout->addWidget(bpmLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
  tempoValueLayout->addStretch(1);

  tempoLayout->addLayout(tempoValueLayout);
  tempoLayout->addStretch(1);

  m_tempoSpin->installEventFilter(this);
  m_tempoLineEdit = m_tempoSpin->findChild<QLineEdit*>();
  if (m_tempoLineEdit) {
    m_tempoLineEdit->installEventFilter(this);
  }

  rootLayout->addWidget(tempoFrame, 0);

  m_blockScroll = new QScrollArea(this);
  m_blockScroll->setObjectName(QStringLiteral("BlockScroll"));
  m_blockScroll->setWidgetResizable(true);
  m_blockScroll->setFrameShape(QFrame::NoFrame);
  m_blockScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_blockScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_blockScroll->viewport()->setObjectName(QStringLiteral("BlockViewport"));
  m_blockScroll->viewport()->setAutoFillBackground(false);

  m_blockContainer = new QWidget(m_blockScroll);
  m_blockContainer->setObjectName(QStringLiteral("BlockContainer"));
  m_blockLayout = new QHBoxLayout(m_blockContainer);
  m_blockLayout->setContentsMargins(0, 0, 0, 0);
  m_blockLayout->setSpacing(3);
  m_blockLayout->addStretch(1);
  m_blockScroll->setWidget(m_blockContainer);
  m_blockScroll->viewport()->installEventFilter(this);
  m_blockContainer->installEventFilter(this);

  rootLayout->addWidget(m_blockScroll, 1);

  m_scrollControls = new QWidget(this);
  m_scrollControls->setObjectName(QStringLiteral("BlockScrollControls"));
  m_scrollControls->setFixedHeight(kBlockHeight);
  auto* scrollLayout = new QHBoxLayout(m_scrollControls);
  scrollLayout->setContentsMargins(0, 0, 0, 0);
  scrollLayout->setSpacing(0);

  m_scrollLeft = new BlockScrollButton(BlockScrollButton::Direction::Left, m_scrollControls);
  scrollLayout->addWidget(m_scrollLeft);

  m_scrollRight = new BlockScrollButton(BlockScrollButton::Direction::Right, m_scrollControls);
  scrollLayout->addWidget(m_scrollRight);

  rootLayout->addWidget(m_scrollControls, 0, Qt::AlignTop);

  connect(m_tempoSpin,
          qOverload<double>(&QDoubleSpinBox::valueChanged),
          this,
          [this](double value) {
            if (!m_updatingUi) {
              emit tempoChanged(value);
            }
          });

  connect(m_tempoSpin, &QDoubleSpinBox::editingFinished, this, [this]() {
    if (!m_tempoSpin || m_updatingUi) {
      return;
    }
    m_tempoSpin->clearFocus();
  });

  connect(m_scrollLeft, &QToolButton::clicked, this, [this]() { scrollBlocks(-kScrollStepPixels); });
  connect(m_scrollRight, &QToolButton::clicked, this, [this]() { scrollBlocks(kScrollStepPixels); });

  connect(m_blockScroll->horizontalScrollBar(),
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

void SequenceControlBar::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);

  QPainter painter(this);
  const QColor cover = sequenceControlBarBackgroundColor(this);
  painter.fillRect(rect(), cover);
  painter.fillRect(0, 0, width(), 2, cover);
  painter.fillRect(0, 0, 2, height(), cover);
}

void SequenceControlBar::setTempoBpm(double bpm) {
  if (!m_tempoSpin) {
    return;
  }

  if (QWidget* focused = QApplication::focusWidget();
      focused && (focused == m_tempoSpin || m_tempoSpin->isAncestorOf(focused))) {
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

void SequenceControlBar::setChannels(const std::vector<ChannelConfig>& channels) {
  rebuildChannelBlocks(channels);
  refreshBlockInteractivity();
  refreshScrollControls();
}

std::vector<int> SequenceControlBar::channelIds() const {
  std::vector<int> ids;
  ids.reserve(m_blocks.size());
  for (const auto& block : m_blocks) {
    ids.push_back(block->id);
  }
  return ids;
}

void SequenceControlBar::setChannelPan(int channelId, int pan) {
  if (auto* block = findBlock(channelId); block && block->panKnob) {
    m_updatingUi = true;
    block->panKnob->setValue(std::clamp(pan, kMinChannelValue, kMaxChannelValue));
    m_updatingUi = false;
  }
}

void SequenceControlBar::setChannelVolume(int channelId, int volume) {
  if (auto* block = findBlock(channelId); block && block->volumeKnob) {
    m_updatingUi = true;
    block->volumeKnob->setValue(std::clamp(volume, kMinChannelValue, kMaxChannelValue));
    m_updatingUi = false;
  }
}

void SequenceControlBar::setChannelMuted(int channelId, bool muted) {
  if (auto* block = findBlock(channelId); block && block->muteButton) {
    m_updatingUi = true;
    block->muteButton->setChecked(muted);
    m_updatingUi = false;
    refreshBlockInteractivity();
  }
}

void SequenceControlBar::setChannelSolo(int channelId, bool solo) {
  if (auto* block = findBlock(channelId); block && block->soloButton) {
    m_updatingUi = true;
    block->soloButton->setChecked(solo);
    m_updatingUi = false;
    refreshBlockInteractivity();
  }
}

bool SequenceControlBar::channelMuted(int channelId) const {
  if (const auto* block = findBlock(channelId); block && block->muteButton) {
    return block->muteButton->isChecked();
  }
  return false;
}

bool SequenceControlBar::channelSolo(int channelId) const {
  if (const auto* block = findBlock(channelId); block && block->soloButton) {
    return block->soloButton->isChecked();
  }
  return false;
}

void SequenceControlBar::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  refreshScrollControls();
}

bool SequenceControlBar::eventFilter(QObject* watched, QEvent* event) {
  if ((watched == m_tempoSpin || watched == m_tempoLineEdit) && m_tempoSpin) {
    switch (event->type()) {
      case QEvent::KeyPress:
        if (auto* keyEvent = static_cast<QKeyEvent*>(event);
            keyEvent &&
            (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)) {
          m_tempoSpin->interpretText();
          m_tempoSpin->clearFocus();
          if (m_tempoLineEdit) {
            m_tempoLineEdit->deselect();
          }
          return true;
        }
        break;
      case QEvent::FocusIn:
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonDblClick:
        QTimer::singleShot(0, this, [this]() {
          if (!m_tempoSpin) {
            return;
          }
          if (QWidget* focused = QApplication::focusWidget();
              focused && (focused == m_tempoSpin || m_tempoSpin->isAncestorOf(focused))) {
            m_tempoSpin->selectAll();
          }
        });
        break;
      case QEvent::FocusOut:
        if (m_tempoLineEdit) {
          m_tempoLineEdit->deselect();
        }
        break;
      default:
        break;
    }
  }

  if (watched == m_blockContainer || watched == m_blockScroll->viewport()) {
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

SequenceControlBar::BlockWidgets* SequenceControlBar::findBlock(int channelId) {
  auto it = std::find_if(m_blocks.begin(), m_blocks.end(), [channelId](const auto& block) {
    return block && block->id == channelId;
  });
  return (it == m_blocks.end() || !(*it)) ? nullptr : it->get();
}

const SequenceControlBar::BlockWidgets* SequenceControlBar::findBlock(int channelId) const {
  auto it = std::find_if(m_blocks.begin(), m_blocks.end(), [channelId](const auto& block) {
    return block && block->id == channelId;
  });
  return (it == m_blocks.end() || !(*it)) ? nullptr : it->get();
}

void SequenceControlBar::rebuildChannelBlocks(const std::vector<ChannelConfig>& channels) {
  if (!m_blockLayout || !m_blockContainer) {
    return;
  }

  m_updatingUi = true;

  while (QLayoutItem* item = m_blockLayout->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }

  m_blocks.clear();
  m_blocks.reserve(channels.size());

  for (const auto& config : channels) {
    auto block = std::make_unique<BlockWidgets>();
    block->id = config.id;
    block->midiChannel = config.midiChannel;
    block->borderColor = config.borderColor;

    block->frame = new QFrame(m_blockContainer);
    block->frame->setObjectName(QStringLiteral("MixerBlock"));
    block->frame->setProperty("dimmed", false);
    block->frame->setProperty("solo", false);
    block->frame->setFixedSize(kBlockWidth, kBlockHeight);
    if (!config.title.isEmpty()) {
      const QString tooltip =
          config.subtitle.isEmpty() ? config.title : QStringLiteral("%1  (%2)").arg(config.title, config.subtitle);
      block->frame->setToolTip(tooltip);
    }

    auto* blockLayout = new QVBoxLayout(block->frame);
    blockLayout->setContentsMargins(0, 0, 0, 0);
    blockLayout->setSpacing(0);

    auto* buttonsLayout = new QHBoxLayout();
    buttonsLayout->setContentsMargins(2, 0, 2, 0);
    buttonsLayout->setSpacing(2);
    buttonsLayout->addStretch(1);

    block->muteButton = new QToolButton(block->frame);
    block->muteButton->setObjectName(QStringLiteral("BlockToggle"));
    block->muteButton->setCheckable(true);
    block->muteButton->setText(QStringLiteral("M"));
    block->muteButton->setFixedSize(22, 17);
    block->muteButton->setToolTip(QStringLiteral("Mute"));
    buttonsLayout->addWidget(block->muteButton);

    block->soloButton = new QToolButton(block->frame);
    block->soloButton->setObjectName(QStringLiteral("BlockToggle"));
    block->soloButton->setCheckable(true);
    block->soloButton->setText(QStringLiteral("S"));
    block->soloButton->setFixedSize(22, 17);
    block->soloButton->setToolTip(QStringLiteral("Solo"));
    buttonsLayout->addWidget(block->soloButton);

    buttonsLayout->addStretch(1);
    blockLayout->addLayout(buttonsLayout);
    blockLayout->addSpacing(2);

    auto* knobRow = new QHBoxLayout();
    knobRow->setContentsMargins(2, 1, 2, 0);
    knobRow->setSpacing(2);
    knobRow->addStretch(1);
    block->panKnob = new MixerKnob(block->frame);
    block->panKnob->setBubblePrefix(QStringLiteral("PAN"));
    block->panKnob->setValue(std::clamp(config.pan, kMinChannelValue, kMaxChannelValue));
    knobRow->addWidget(block->panKnob, 0, Qt::AlignHCenter);

    block->volumeKnob = new MixerKnob(block->frame);
    block->volumeKnob->setBubblePrefix(QStringLiteral("VOL"));
    block->volumeKnob->setValue(std::clamp(config.volume, kMinChannelValue, kMaxChannelValue));
    knobRow->addWidget(block->volumeKnob, 0, Qt::AlignHCenter);
    knobRow->addStretch(1);

    blockLayout->addLayout(knobRow, 1);

    m_blockLayout->addWidget(block->frame, 0, Qt::AlignVCenter);

    connect(block->muteButton, &QToolButton::toggled, this, [this, id = block->id](bool checked) {
      if (m_updatingUi) {
        return;
      }

      if (checked) {
        if (auto* selfBlock = findBlock(id); selfBlock && selfBlock->soloButton->isChecked()) {
          m_updatingUi = true;
          selfBlock->soloButton->setChecked(false);
          m_updatingUi = false;
        }
      }

      refreshBlockInteractivity();
      emit chanMuteChanged(id, checked);
    });

    connect(block->soloButton, &QToolButton::toggled, this, [this, id = block->id](bool checked) {
      if (m_updatingUi) {
        return;
      }

      refreshBlockInteractivity();
      emit chanSoloChanged(id, checked);
    });

    block->panKnob->setOnValueChanged([this, id = block->id](int value) {
      if (!m_updatingUi) {
        emit chanPanChanged(id, value);
      }
    });

    block->volumeKnob->setOnValueChanged([this, id = block->id](int value) {
      if (!m_updatingUi) {
        emit chanVolumeChanged(id, value);
      }
    });

    applyBlockFrameStyle(*block, false, false);
    m_blocks.push_back(std::move(block));
  }

  m_blockLayout->addStretch(1);
  m_updatingUi = false;
}

void SequenceControlBar::refreshBlockInteractivity() {
  const bool anySolo = std::any_of(m_blocks.begin(), m_blocks.end(), [](const auto& block) {
    return block && block->soloButton && block->soloButton->isChecked();
  });

  for (auto& blockPtr : m_blocks) {
    if (!blockPtr) {
      continue;
    }
    auto& block = *blockPtr;
    const bool muted = block.muteButton && block.muteButton->isChecked();
    const bool soloed = block.soloButton && block.soloButton->isChecked();
    const bool blockedBySolo = anySolo && !soloed;
    const bool controlsDisabled = muted || blockedBySolo;

    if (block.frame) {
      block.frame->setProperty("dimmed", controlsDisabled);
      block.frame->setProperty("solo", soloed);
      applyBlockFrameStyle(block, controlsDisabled, soloed);
    }

    if (block.muteButton) {
      block.muteButton->setEnabled(true);
    }
    if (block.soloButton) {
      block.soloButton->setEnabled(!muted);
    }
    if (block.panKnob) {
      block.panKnob->setEnabled(!controlsDisabled);
    }
    if (block.volumeKnob) {
      block.volumeKnob->setEnabled(!controlsDisabled);
    }
  }
}

void SequenceControlBar::applyBlockFrameStyle(BlockWidgets& block, bool dimmed, bool soloed) {
  if (!block.frame) {
    return;
  }

  QColor border = block.borderColor;
  if (!border.isValid()) {
    border = QColor::fromHsv((block.id * 43) % 360, 190, 235);
  }
  if (soloed) {
    border = border.lighter(120);
  }
  border.setAlpha(dimmed ? 126 : 238);

  const QString frameStyle = QStringLiteral(
      "QFrame#MixerBlock {"
      " border: none;"
      " border-left: 3px solid rgba(%1,%2,%3,%4);"
      " border-radius: 0px;"
      " background: transparent;"
      "}")
                                 .arg(border.red())
                                 .arg(border.green())
                                 .arg(border.blue())
                                 .arg(border.alpha());
  if (block.frame->styleSheet() != frameStyle) {
    block.frame->setStyleSheet(frameStyle);
  }
}

void SequenceControlBar::refreshScrollControls() {
  if (!m_blockScroll || !m_scrollControls) {
    return;
  }

  auto* hbar = m_blockScroll->horizontalScrollBar();
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
  if (!m_blockScroll) {
    return;
  }

  auto* hbar = m_blockScroll->horizontalScrollBar();
  if (!hbar) {
    return;
  }

  hbar->setValue(hbar->value() + deltaPixels);
  refreshScrollControls();
}

void SequenceControlBar::refreshStyleSheet() {
  const QColor barBg = sequenceControlBarBackgroundColor(this);
  const QColor text = palette().color(QPalette::WindowText);
  const bool lightMode = qGray(barBg.rgb()) > 140;

  QColor subtleText = text;
  subtleText.setAlpha(160);

  QColor buttonOff = lightMode ? QColor(167, 174, 184) : QColor(79, 86, 98);
  QColor buttonOn = lightMode ? QColor(196, 153, 90) : QColor(230, 176, 84);
  QColor buttonText = lightMode ? QColor(248, 250, 253) : QColor(240, 242, 246);

  const QString style = QStringLiteral(
      "QWidget#SequenceControlBar {"
      " border: none;"
      " background: %1;"
      "}"
      "QFrame#TempoBlock {"
      " border: none;"
      " border-radius: 0px;"
      " background: transparent;"
      "}"
      "QScrollArea#BlockScroll {"
      " border: none;"
      " background: transparent;"
      "}"
      "QWidget#BlockViewport {"
      " border: none;"
      " background: transparent;"
      "}"
      "QWidget#BlockContainer {"
      " border: none;"
      " background: transparent;"
      "}"
      "QLabel#TempoTitle {"
      " font-size: 10px;"
      " font-weight: 700;"
      " color: rgba(%2,%3,%4,%5);"
      " padding-top: 0px;"
      " padding-bottom: 0px;"
      "}"
      "QLabel#TempoUnit {"
      " font-size: 9px;"
      " font-weight: 600;"
      " color: rgba(%2,%3,%4,%5);"
      "}"
      "QDoubleSpinBox#TempoSpin {"
      " border: none;"
      " border-radius: 0px;"
      " padding: 1px 0px 1px 0px;"
      " color: palette(text);"
      " background: rgba(0,0,0,0.25);"
      " selection-background-color: rgba(255,255,255,0.2);"
      " font-size: 9px;"
      "}"
      "QToolButton#BlockToggle {"
      " border: 1px solid rgba(255,255,255,0.14);"
      " border-radius: 3px;"
      " background: rgba(%6,%7,%8,200);"
      " color: rgba(%9,%10,%11,220);"
      " font-size: 9px;"
      " font-weight: 700;"
      " padding: 0px;"
      "}"
      "QToolButton#BlockToggle:checked {"
      " background: rgba(%12,%13,%14,230);"
      " color: rgba(%9,%10,%11,255);"
      " border: 1px solid rgba(255,255,255,0.28);"
      "}"
      "QToolButton#BlockToggle:disabled {"
      " background: rgba(0,0,0,0.28);"
      " color: rgba(255,255,255,0.22);"
      " border: 1px solid rgba(255,255,255,0.08);"
      "}"
      "QWidget#BlockScrollControls {"
      " border: none;"
      " background: transparent;"
      "}")
                           .arg(barBg.name())
                           .arg(subtleText.red())
                           .arg(subtleText.green())
                           .arg(subtleText.blue())
                           .arg(subtleText.alpha())
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
