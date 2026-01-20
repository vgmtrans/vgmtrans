/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

#include <QWidget>

class HexView;
class HexViewRhiTarget;
class QResizeEvent;

class HexViewRhiHost final : public QWidget {
  Q_OBJECT

public:
  explicit HexViewRhiHost(HexView* view, QWidget* parent = nullptr);
  ~HexViewRhiHost() override = default;

  void markBaseDirty();
  void markSelectionDirty();
  void invalidateCache();
  void requestUpdate();

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  HexViewRhiTarget* m_target = nullptr;
  QWidget* m_surface = nullptr;
};
