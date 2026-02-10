/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QWidget>

#include <memory>

class PianoRollRhiRenderer;
class PianoRollView;
class QResizeEvent;
class QWindow;

class PianoRollRhiHost final : public QWidget {
public:
  explicit PianoRollRhiHost(PianoRollView* view, QWidget* parent = nullptr);
  ~PianoRollRhiHost() override;

  void requestUpdate();

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  // Shared renderer instance used by either the QRhiWidget path (Linux)
  // or the native window path (macOS/Windows).
  std::unique_ptr<PianoRollRhiRenderer> m_renderer;
  QWidget* m_surface = nullptr;
  QWindow* m_window = nullptr;
};
