/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QRhiWidget>

#include "PianoRollRhiRenderer.h"

class QMouseEvent;
class QNativeGestureEvent;
class QResizeEvent;
class QWheelEvent;
class QEvent;
class PianoRollView;

class PianoRollRhiWidget final : public QRhiWidget {
public:
  explicit PianoRollRhiWidget(PianoRollView* view, QWidget* parent = nullptr);
  ~PianoRollRhiWidget() override;

protected:
  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void releaseResources() override;
  void resizeEvent(QResizeEvent* event) override;
  bool event(QEvent* event) override;

  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

private:
  PianoRollView* m_view = nullptr;
  PianoRollRhiRenderer m_renderer;
};
