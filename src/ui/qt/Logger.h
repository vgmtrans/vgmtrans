/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QDockWidget>
#include <QString>
#include <QVector>

class QPlainTextEdit;
class QTimer;
class QToolButton;
class TitleBar;

namespace vgmtrans::ui {
class WorkspaceController;
}

class Logger final : public QDockWidget {
  Q_OBJECT

public:
  explicit Logger(vgmtrans::ui::WorkspaceController& workspace,
                  QWidget* parent = nullptr);
  static QString getLogText();
  [[nodiscard]] int level() const noexcept { return m_level; }
  void installTitleBarControls(TitleBar* titleBar);

public slots:
  void exportLog();
  void clearLog();
  void setLevel(int level);

private:
  void syncDiagnostics();
  void rebuildText();
  void flushPending();
  void refreshTitleBarControls();

  struct PendingMessage {
    QString text;
    int level{};
  };

  vgmtrans::ui::WorkspaceController& m_workspace;
  TitleBar* m_titleBar{};
  QPlainTextEdit* m_textArea{};
  QToolButton* m_filterButton{};
  QToolButton* m_clearButton{};
  QToolButton* m_exportButton{};
  qsizetype m_suppressedDiagnostics{};
  qsizetype m_observedDiagnostics{};
  int m_level = 2;
  QTimer* m_flushTimer{};
  QVector<PendingMessage> m_pendingMessages;
};
