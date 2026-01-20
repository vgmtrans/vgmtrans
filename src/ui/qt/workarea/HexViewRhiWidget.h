/*
* VGMTrans (c) 2002-2024
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QRhiWidget>
#include <QPointF>
#include <memory>

class QEvent;
class QResizeEvent;
class HexView;
class HexViewRhiRenderer;

class HexViewRhiWidget final : public QRhiWidget {
  Q_OBJECT

public:
  explicit HexViewRhiWidget(HexView* view, QWidget* parent = nullptr);
  ~HexViewRhiWidget() override;

  void markBaseDirty();
  void markSelectionDirty();
  void invalidateCache();
  void requestUpdate();

protected:
  void initialize(QRhiCommandBuffer* cb) override;
  void render(QRhiCommandBuffer* cb) override;
  void releaseResources() override;
  bool event(QEvent* e) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  void drainPendingMouseMove();
  void drainPendingWheel();

  HexView* m_view = nullptr;
  std::unique_ptr<HexViewRhiRenderer> m_renderer;

  bool m_dragging = false;
  bool m_pendingMouseMove = false;

  // Mouse Move state
  QPointF m_pendingGlobalPos;
  Qt::MouseButtons m_pendingButtons = Qt::NoButton;
  Qt::KeyboardModifiers m_pendingMods = Qt::NoModifier;

  // Coalesced wheel state
  bool m_pendingWheel = false;
  QPointF m_wheelGlobalPos;
  QPoint m_wheelPixelDelta;   // summed
  QPoint m_wheelAngleDelta;   // summed
  Qt::KeyboardModifiers m_wheelMods = Qt::NoModifier;
  Qt::MouseButtons m_wheelButtons = Qt::NoButton;
  Qt::ScrollPhase m_wheelPhase = Qt::NoScrollPhase;

  bool m_scrolling = false;
  int m_pumpFrames = 0;        // generic "keep drawing for N frames"
};
