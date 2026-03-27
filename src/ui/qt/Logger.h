/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <QDockWidget>
#include <QVector>
#include <QString>

class QPlainTextEdit;
class QTimer;
class QToolButton;
class LogItem;
class TitleBar;
enum LogLevel : int;

class Logger : public QDockWidget {
  Q_OBJECT

public:
  explicit Logger(QWidget *parent = nullptr);
  static QString getLogText();
  int level() const { return m_level; }
  void installTitleBarControls(TitleBar *titleBar);

  void push(const LogItem *item);

public slots:
  void exportLog();
  void clearLog();
  void setLevel(int level);

private:
  void createElements();
  void connectElements();
  void flushPending();
  void refreshTitleBarControls();

  struct PendingMessage {
    QString text;
    LogLevel level;
  };

  TitleBar *m_titleBar{};
  QPlainTextEdit *logger_textarea;
  QToolButton *m_filterButton{};
  QToolButton *m_clearButton{};
  QToolButton *m_exportButton{};

  int m_level;
  QTimer *m_flushTimer;
  QVector<PendingMessage> m_pendingMessages;
};
