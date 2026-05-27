/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include "HexViewRhiRenderer.h"

#include <memory>

#include <QWidget>

class HexView;
class QResizeEvent;
class QWindow;

class HexViewRhiHost final : public QWidget {
  Q_OBJECT

public:
  explicit HexViewRhiHost(HexView* view, QWidget* parent = nullptr);
  ~HexViewRhiHost() override;

  void markBaseDirty();
  void markSelectionDirty();
  void markPlaybackDirty();
  void invalidateCache();
  void requestUpdate();

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  std::unique_ptr<HexViewRhiRenderer> m_renderer;
  QWidget* m_surface = nullptr;
  QWindow* m_window = nullptr;
};
