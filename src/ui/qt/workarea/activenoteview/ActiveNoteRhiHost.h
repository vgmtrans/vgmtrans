/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QWidget>

#include <memory>

class ActiveNoteRhiRenderer;
class ActiveNoteView;
class QResizeEvent;
class QWindow;

class ActiveNoteRhiHost final : public QWidget {
public:
  explicit ActiveNoteRhiHost(ActiveNoteView* view, QWidget* parent = nullptr);
  ~ActiveNoteRhiHost() override;

  void requestUpdate();

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  std::unique_ptr<ActiveNoteRhiRenderer> m_renderer;
  QWidget* m_surface = nullptr;
  QWindow* m_window = nullptr;
};
