/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QRhiWidget>

#include "ActiveNoteRhiRenderer.h"

class QResizeEvent;
class QMouseEvent;
class ActiveNoteView;
class ActiveNoteRhiRenderer;

class ActiveNoteRhiWidget final : public QRhiWidget {
public:
  explicit ActiveNoteRhiWidget(ActiveNoteView* view,
                               ActiveNoteRhiRenderer* renderer,
                               QWidget* parent = nullptr);
  ~ActiveNoteRhiWidget() override;

protected:
  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void releaseResources() override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

private:
  ActiveNoteView* m_view = nullptr;
  ActiveNoteRhiRenderer* m_renderer = nullptr;
};
