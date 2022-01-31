/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <QWidget>
#include <QStaticText>
#include <QTimer>

/* Loosely based on https://stackoverflow.com/a/10655396 */
class MarqueeLabel : public QWidget {
  Q_OBJECT
public:
  explicit MarqueeLabel(QWidget *parent = nullptr);

  QString text() const;
  void setText(QString text);

  QString separator() const;
  void setSeparator(QString separator);

  QSize sizeHint() const override;
  void hideEvent(QHideEvent *event) override;
  void showEvent(QShowEvent *event) override;

protected:
  void paintEvent(QPaintEvent *) override;
  void resizeEvent(QResizeEvent *) override;

private:
  void updateText();

  QString m_label_text{};
  QString m_separator{};
  QStaticText m_static_text{};

  int m_text_width;
  QSize m_renderlabel_size;
  int m_margin_left;

  bool m_scroll_enabled{false};
  int m_scroll_pos{0};

  QImage m_alpha_channel;
  QImage m_render_buffer;

  QTimer m_scroll_timer;
  QTimer m_delayscroll_timer;
  bool m_delaying = true;

  void onTimeout();
};
