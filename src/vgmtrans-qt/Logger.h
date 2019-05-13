/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QDockWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QLabel>
#include "LogItem.h"

class Logger : public QDockWidget {
  Q_OBJECT

public:
  explicit Logger(QWidget *parent = nullptr);
  void LogMessage(LogItem *message);

signals:
  void closeEvent(QCloseEvent *) override;

private:
  void CreateElements();
  QWidget *logger_wrapper;
  QTextEdit *logger_textarea;
};
