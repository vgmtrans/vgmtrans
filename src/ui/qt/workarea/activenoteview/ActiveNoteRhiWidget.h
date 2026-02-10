/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QRhiWidget>

#include "ActiveNoteRhiRenderer.h"

class QResizeEvent;
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

private:
  ActiveNoteRhiRenderer* m_renderer = nullptr;
};
