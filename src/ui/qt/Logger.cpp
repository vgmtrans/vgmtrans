/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Logger.h"

#include <QGridLayout>
#include <QComboBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QFileDialog>
#include <LogItem.h>
#include "QtVGMRoot.h"

Logger::Logger(QWidget *parent) : QDockWidget("Log", parent), m_level(LOG_LEVEL_INFO) {
  setAllowedAreas(Qt::AllDockWidgetAreas);

  createElements();
  connectElements();
}

void Logger::createElements() {
  logger_wrapper = new QWidget;

  logger_textarea = new QPlainTextEdit(logger_wrapper);
  logger_textarea->setReadOnly(true);
  logger_textarea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  logger_textarea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  logger_textarea->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

  logger_filter = new QComboBox(logger_wrapper);
  logger_filter->setEditable(false);
  logger_filter->addItems({"Errors", "Errors, warnings", "Errors, warnings, information",
                           "Complete debug information"});
  logger_filter->setCurrentIndex(m_level);

  logger_clear = new QPushButton("Clear", logger_wrapper);
  logger_save = new QPushButton("Export log", logger_wrapper);

  QGridLayout *logger_layout = new QGridLayout;
  logger_layout->addWidget(logger_filter, 0, 0);
  logger_layout->addWidget(logger_clear, 0, 1);
  logger_layout->addWidget(logger_save, 0, 2);
  logger_layout->addWidget(logger_textarea, 1, 0, 1, -1);

  logger_wrapper->setLayout(logger_layout);

  setWidget(logger_wrapper);
};

void Logger::connectElements() {
  connect(logger_clear, &QPushButton::pressed, logger_textarea, &QPlainTextEdit::clear);
  connect(logger_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
          [this](int level) { m_level = level; });
  connect(logger_save, &QPushButton::pressed, this, &Logger::exportLog);
  connect(&qtVGMRoot, &QtVGMRoot::UI_log, this, &Logger::push);
}

void Logger::exportLog() {
  if (logger_textarea->toPlainText().isEmpty()) {
    return;
  }

  auto path = QFileDialog::getSaveFileName(this, "Export log", "", "Log files (*.log)");
  if (path.isEmpty()) {
    return;
  }

  QSaveFile log(path);
  log.open(QIODevice::WriteOnly);

  QByteArray out_buf;
  out_buf.append(logger_textarea->toPlainText().toUtf8());
  log.write(out_buf);
  log.commit();
}

void Logger::push(const LogItem *item) const {
  static constexpr const char *log_colors[]{"red", "orange", "darkgrey", "black"};

  if (item->logLevel() > m_level) {
    return;
  }

  // If the source string is empty, don't print it, otherwise encapsulate it in brackets
  auto source = item->source().empty() ? ""
    : "[" + QString::fromStdString(item->source()) + "]";
  logger_textarea->appendHtml(QStringLiteral("<font color=%2>%1 %3</font>")
    .arg(source, QString(log_colors[static_cast<int>(item->logLevel())]),
      QString::fromStdString(item->text())));
}
