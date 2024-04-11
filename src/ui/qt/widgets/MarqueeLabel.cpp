/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "MarqueeLabel.h"
#include <QPainter>
#include <QApplication>

MarqueeLabel::MarqueeLabel(QWidget *parent) : QWidget(parent) {
  m_static_text.setTextFormat(Qt::PlainText);

  setFixedHeight(fontMetrics().height());
  m_margin_left = height() / 3;

  setSeparator("   |   ");

  connect(&m_scroll_timer, &QTimer::timeout, this, &MarqueeLabel::onTimeout);
  m_scroll_timer.setInterval(50);
  connect(&m_delayscroll_timer, &QTimer::timeout, this, &MarqueeLabel::onTimeout);
  m_delayscroll_timer.setInterval(1500);
}

QString MarqueeLabel::text() const {
  return m_label_text;
}

void MarqueeLabel::setText(QString text) {
  m_label_text = text;
  updateText();
  update();
}

QString MarqueeLabel::separator() const {
  return m_separator;
}

QSize MarqueeLabel::sizeHint() const {
  return QSize(std::min(m_renderlabel_size.width() + m_margin_left, maximumWidth()),
               fontMetrics().height());
}
void MarqueeLabel::hideEvent(QHideEvent *) {
  if (m_scroll_enabled) {
    m_scroll_pos = 0;
    m_scroll_timer.stop();
    m_delayscroll_timer.stop();
  }
}

void MarqueeLabel::showEvent(QShowEvent *) {
  if (m_scroll_enabled) {
    m_delayscroll_timer.start();
    m_delaying = true;
  }
}

void MarqueeLabel::setSeparator(QString separator) {
  m_separator = separator;
  updateText();
  update();
}

void MarqueeLabel::updateText() {
  m_scroll_timer.stop();

  m_text_width = fontMetrics().horizontalAdvance(m_label_text);
  m_scroll_enabled = m_text_width > width() - m_margin_left;

  if (m_scroll_enabled) {
    m_static_text.setText(m_label_text + m_separator);
    if (!(window()->windowState() & Qt::WindowMinimized)) {
      m_scroll_pos = 0;
      m_delayscroll_timer.start();
      m_delaying = true;
    }
  } else {
    m_static_text.setText(m_label_text);
  }

  m_static_text.prepare(QTransform(), font());
  m_renderlabel_size =
      QSize(fontMetrics().horizontalAdvance(m_static_text.text()), fontMetrics().height());
}

void MarqueeLabel::paintEvent(QPaintEvent *) {
  QPainter p(this);
  QPalette palette = QApplication::palette();
  p.setPen(palette.color(QPalette::WindowText));
  if (m_scroll_enabled) {
    int x = std::min(-m_scroll_pos, 0) + m_margin_left;
    while (x < width()) {
      p.drawStaticText(QPointF(x, (height() - m_renderlabel_size.height()) / 2.0), m_static_text);
      x += m_renderlabel_size.width();
    }
  } else {
    p.drawStaticText(QPointF(m_margin_left, (height() - m_renderlabel_size.height()) / 2.0),
                     m_static_text);
  }
}

void MarqueeLabel::resizeEvent(QResizeEvent *) {
  /* Since the size has changed, we might not have to scroll if the text fits in the render area */
  if (bool should_scroll = (m_text_width > width() - m_margin_left);
      should_scroll != m_scroll_enabled) {
    updateText();
  }
}

void MarqueeLabel::onTimeout() {
  m_scroll_pos = (m_scroll_pos + 2) % m_renderlabel_size.width();

  if (m_delaying) {
    m_delaying = false;
    m_scroll_timer.start();
    m_delayscroll_timer.stop();
  }

  if (m_scroll_pos == 0) {
    m_delaying = true;
    m_scroll_timer.stop();
    m_delayscroll_timer.start();
  }

  update();
}
