/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Logger.h"

#include <QComboBox>
#include <QColor>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGridLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QPalette>
#include <LogItem.h>
#include "QtVGMRoot.h"

namespace {

constexpr int FLUSH_INTERVAL_MS = 500;
constexpr int FLUSH_MESSAGE_THRESHOLD = 5000;

QString levelPrefix(LogLevel level) {
  switch (level) {
  case LOG_LEVEL_ERR:
    return QStringLiteral("[ERR]");
  case LOG_LEVEL_WARN:
    return QStringLiteral("[WRN]");
  case LOG_LEVEL_INFO:
    return QStringLiteral("[INF]");
  case LOG_LEVEL_DEBUG:
  default:
    return QStringLiteral("[DBG]");
  }
}

QColor levelColor(LogLevel level) {
  switch (level) {
  case LOG_LEVEL_ERR:
    return QColor(QStringLiteral("red"));
  case LOG_LEVEL_WARN:
    return QColor(QStringLiteral("orange"));
  case LOG_LEVEL_INFO:
    return QColor(QStringLiteral("cyan"));
  case LOG_LEVEL_DEBUG:
  default:
    return QColor(QStringLiteral("mediumpurple"));
  }
}

} // namespace

Logger::Logger(QWidget *parent)
    : QDockWidget("Log", parent), m_level(LOG_LEVEL_INFO), m_flushTimer(new QTimer(this)) {
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
  m_flushTimer->setSingleShot(true);
  connect(m_flushTimer, &QTimer::timeout, this, &Logger::flushPending);

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
  connect(logger_clear, &QPushButton::pressed, this, &Logger::clearLog);
  connect(logger_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
          [this](int level) { m_level = level; });
  connect(logger_save, &QPushButton::pressed, this, &Logger::exportLog);
  connect(&qtVGMRoot, &QtVGMRoot::UI_log, this, &Logger::push);
}

void Logger::exportLog() {
  flushPending();
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

void Logger::clearLog() {
  m_flushTimer->stop();
  m_pendingMessages.clear();
  logger_textarea->clear();
}

void Logger::push(const LogItem *item) {
  if (item->logLevel() > m_level) {
    return;
  }

  // If the source string is empty, don't print it, otherwise encapsulate it in brackets
  QString message;
  if (!item->source().empty()) {
    message.append('[');
    message.append(QString::fromStdString(item->source()));
    message.append("] ");
  }
  message.append(QString::fromStdString(item->text()));

  m_pendingMessages.append({message, item->logLevel()});

  if (m_pendingMessages.size() >= FLUSH_MESSAGE_THRESHOLD) {
    flushPending();
    return;
  }

  m_flushTimer->start(FLUSH_INTERVAL_MS);
}

void Logger::flushPending() {
  m_flushTimer->stop();

  if (m_pendingMessages.isEmpty()) {
    return;
  }

  QTextCursor cursor = logger_textarea->textCursor();
  cursor.movePosition(QTextCursor::End);
  cursor.beginEditBlock();

  logger_textarea->setUpdatesEnabled(false);

  QTextCharFormat text_format;
  text_format.setForeground(logger_textarea->palette().color(QPalette::Text));

  for (const PendingMessage &entry : m_pendingMessages) {
    QTextCharFormat prefix_format;
    prefix_format.setForeground(levelColor(entry.level));
    cursor.insertText(levelPrefix(entry.level), prefix_format);
    cursor.insertText(QStringLiteral(" "), text_format);
    cursor.insertText(entry.text, text_format);
    cursor.insertBlock();
  }

  cursor.endEditBlock();
  logger_textarea->setTextCursor(cursor);
  logger_textarea->setUpdatesEnabled(true);
  logger_textarea->ensureCursorVisible();

  m_pendingMessages.clear();
}