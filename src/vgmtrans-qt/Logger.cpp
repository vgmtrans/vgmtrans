/*
 * VGMTrans (c) 2018
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <QGridLayout>
#include <QString>
#include "Logger.h"

Logger::Logger(QWidget *parent) : QDockWidget(parent) {
  setWindowTitle("Log");
  setAllowedAreas(Qt::AllDockWidgetAreas);

  CreateElements();

  setHidden(true);
}

void Logger::CreateElements() {
  logger_wrapper = new QWidget;
  logger_textarea = new QTextEdit(logger_wrapper);
  logger_textarea->setReadOnly(true);
  logger_textarea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  logger_textarea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  QGridLayout *logger_layout = new QGridLayout;
  logger_layout->addWidget(logger_textarea);
  logger_wrapper->setLayout(logger_layout);
  logger_layout->setMargin(0);

  setWidget(logger_wrapper);
};

void Logger::LogMessage(LogItem *message) {
  std::string color = "black";
  switch (message->GetLogLevel()) {
    case LOG_LEVEL_ERR:
      color = "red";
      break;
    case LOG_LEVEL_WARN:
      color = "yellow";
      break;
    case LOG_LEVEL_INFO:
      color = "darkgrey";
      break;
    case LOG_LEVEL_DEBUG:
      break;
  }

  logger_textarea->append(
      QStringLiteral("%1 <font color=%2>%3</font>")
          .arg(QString::fromStdString(message->GetTime().ToString()), QString::fromStdString(color),
               QString::fromStdWString(message->GetSource() + L": " + message->GetText())));
}
