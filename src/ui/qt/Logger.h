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
class QComboBox;
class QPushButton;
class QTimer;
class LogItem;
enum LogLevel : int;

class Logger : public QDockWidget {
  Q_OBJECT

public:
  explicit Logger(QWidget *parent = nullptr);

  void push(const LogItem *item);

  signals:
    void closeEvent(QCloseEvent *) override;

private:
  void createElements();
  void connectElements();
  void exportLog();
  void clearLog();
  void flushPending();

  struct PendingMessage {
    QString text;
    LogLevel level;
  };

  QWidget *logger_wrapper;
  QPlainTextEdit *logger_textarea;

  QComboBox *logger_filter;
  QPushButton *logger_clear;
  QPushButton *logger_save;

  int m_level;
  QTimer *m_flushTimer;
  QVector<PendingMessage> m_pendingMessages;
};