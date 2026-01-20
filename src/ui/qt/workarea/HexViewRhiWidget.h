/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QRhiWidget>
#include <QPointF>
#include <memory>

#include "HexViewRhiTarget.h"

class QEvent;
class QResizeEvent;
class HexView;
class HexViewRhiRenderer;

class HexViewRhiWidget final : public QRhiWidget, public HexViewRhiTarget {
  Q_OBJECT

public:
  explicit HexViewRhiWidget(HexView* view, QWidget* parent = nullptr);
  ~HexViewRhiWidget() override;

  void markBaseDirty() override;
  void markSelectionDirty() override;
  void invalidateCache() override;
  void requestUpdate() override;

protected:
  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void releaseResources() override;
  bool event(QEvent* e) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  HexView* m_view = nullptr;
  std::unique_ptr<HexViewRhiRenderer> m_renderer;
  bool m_dragging = false;
};
