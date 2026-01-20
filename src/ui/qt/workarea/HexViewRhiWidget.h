/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QRhiWidget>

class QEvent;
class QResizeEvent;
class HexView;
class HexViewRhiRenderer;

class HexViewRhiWidget final : public QRhiWidget {
  Q_OBJECT

public:
  explicit HexViewRhiWidget(HexView* view, HexViewRhiRenderer* renderer,
                            QWidget* parent = nullptr);
  ~HexViewRhiWidget() override;

protected:
  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void releaseResources() override;
  bool event(QEvent* e) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  HexView* m_view = nullptr;
  HexViewRhiRenderer* m_renderer = nullptr;
  bool m_dragging = false;
};
