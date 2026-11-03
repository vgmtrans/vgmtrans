/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Logger.h"

#include "TitleBar.h"
#include "application/WorkspaceController.h"
#include "util/UIHelpers.h"

#include <utility>

#include <QActionGroup>
#include <QColor>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>

namespace {
constexpr int kFlushIntervalMs = 500;
constexpr int kFlushMessageThreshold = 5000;
constexpr int kErrorLevel = 0;
constexpr int kWarningLevel = 1;
constexpr int kInfoLevel = 2;
constexpr int kDebugLevel = 3;
constexpr int kAccessoryButtonWidth = 22;
constexpr int kAccessoryButtonHeight = 20;
constexpr int kAccessoryIconSize = 16;
constexpr int kFilterButtonLeftMargin = 6;
Logger* instance = nullptr;

QString filterButtonText(int level) {
  if (level == kErrorLevel) {
    return QStringLiteral(u"Errors ▾");
  }
  if (level == kWarningLevel) {
    return QStringLiteral(u"Warnings+ ▾");
  }
  if (level == kInfoLevel) {
    return QStringLiteral(u"Info+ ▾");
  }
  return QStringLiteral(u"Debug ▾");
}

QString filterMenuText(int level) {
  if (level == kErrorLevel) {
    return QStringLiteral("Errors");
  }
  if (level == kWarningLevel) {
    return QStringLiteral("Errors, warnings");
  }
  if (level == kInfoLevel) {
    return QStringLiteral("Errors, warnings, information");
  }
  return QStringLiteral("Complete debug information");
}

int diagnosticLevel(vgmtrans::core::Severity severity) {
  switch (severity) {
    case vgmtrans::core::Severity::Error:
      return kErrorLevel;
    case vgmtrans::core::Severity::Warning:
      return kWarningLevel;
    case vgmtrans::core::Severity::Info:
      return kInfoLevel;
  }
  return kInfoLevel;
}

QString levelPrefix(int level) {
  if (level == kErrorLevel) {
    return QStringLiteral("[ERR]");
  }
  if (level == kWarningLevel) {
    return QStringLiteral("[WRN]");
  }
  if (level == kInfoLevel) {
    return QStringLiteral("[INF]");
  }
  return QStringLiteral("[DBG]");
}

QColor levelColor(int level) {
  if (level == kErrorLevel) {
    return QColor(QStringLiteral("red"));
  }
  if (level == kWarningLevel) {
    return QColor(QStringLiteral("orange"));
  }
  if (level == kInfoLevel) {
    return QColor(QStringLiteral("cyan"));
  }
  return QColor(QStringLiteral("mediumpurple"));
}
}  // namespace

Logger::Logger(vgmtrans::ui::WorkspaceController& workspace, QWidget* parent)
    : QDockWidget(QStringLiteral("Log"), parent),
      m_workspace(workspace),
      m_flushTimer(new QTimer(this)) {
  instance = this;
  setAllowedAreas(Qt::AllDockWidgetAreas);
  setMinimumWidth(240);

  m_textArea = new QPlainTextEdit(this);
  m_textArea->setReadOnly(true);
  m_textArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_textArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_textArea->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  m_textArea->setFrameStyle(QFrame::NoFrame);
  setWidget(m_textArea);

  m_flushTimer->setSingleShot(true);
  connect(m_flushTimer, &QTimer::timeout, this, &Logger::flushPending);

  connect(&m_workspace, &vgmtrans::ui::WorkspaceController::snapshotChanged,
          this, &Logger::syncDiagnostics);
  syncDiagnostics();
}

QString Logger::getLogText() {
  if (instance == nullptr) {
    return {};
  }
  instance->flushPending();
  return instance->m_textArea->toPlainText();
}

void Logger::installTitleBarControls(TitleBar* titleBar) {
  if (titleBar == nullptr) {
    return;
  }
  m_titleBar = titleBar;

  m_filterButton = new QToolButton(titleBar);
  configureToolButton(m_filterButton, QStringLiteral("Log level"), {}, {}, true);
  m_filterButton->setPopupMode(QToolButton::InstantPopup);
  m_filterButton->setText(filterButtonText(m_level));
  QFont filterFont = m_filterButton->font();
  filterFont.setPointSizeF(filterFont.pointSizeF() + 0.1);
  m_filterButton->setFont(filterFont);
  m_filterButton->setMinimumWidth(
      m_filterButton->fontMetrics().horizontalAdvance(filterButtonText(kWarningLevel)));

  auto* filterMenu = new QMenu(m_filterButton);
  auto* filterActions = new QActionGroup(filterMenu);
  filterActions->setExclusive(true);
  for (int level = kErrorLevel; level <= kDebugLevel; ++level) {
    QAction* action = filterMenu->addAction(filterMenuText(level));
    action->setData(level);
    action->setCheckable(true);
    action->setChecked(level == m_level);
    filterActions->addAction(action);
  }
  connect(filterActions, &QActionGroup::triggered, this,
          [this](QAction* action) { setLevel(action->data().toInt()); });
  m_filterButton->setMenu(filterMenu);
  titleBar->addLeadingWidget(m_filterButton);

  const auto addButton = [titleBar](const QString& tooltip) {
    auto* button = new QToolButton(titleBar);
    configureToolButton(button, tooltip,
                        QSize(kAccessoryButtonWidth, kAccessoryButtonHeight),
                        QSize(kAccessoryIconSize, kAccessoryIconSize));
    titleBar->addLeadingWidget(button);
    return button;
  };
  m_clearButton = addButton(QStringLiteral("Clear All"));
  connect(m_clearButton, &QToolButton::clicked, this, &Logger::clearLog);
  auto* spacer = new QWidget(titleBar);
  spacer->setFixedWidth(6);
  titleBar->addLeadingWidget(spacer);
  m_exportButton = addButton(QStringLiteral("Export Log"));
  connect(m_exportButton, &QToolButton::clicked, this, &Logger::exportLog);

  connect(titleBar, &TitleBar::appearanceChanged, this, &Logger::refreshTitleBarControls);
  refreshTitleBarControls();
}

void Logger::exportLog() {
  flushPending();
  if (m_textArea->toPlainText().isEmpty()) {
    return;
  }
  const QString path = QFileDialog::getSaveFileName(this, tr("Export log"), {},
                                                     tr("Log files (*.log)"));
  if (path.isEmpty()) {
    return;
  }
  QSaveFile file(path);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(m_textArea->toPlainText().toUtf8());
    file.commit();
  }
}

void Logger::clearLog() {
  m_flushTimer->stop();
  m_pendingMessages.clear();
  m_suppressedDiagnostics =
      static_cast<qsizetype>(m_workspace.snapshot().diagnostics().size());
  m_observedDiagnostics = m_suppressedDiagnostics;
  m_textArea->clear();
}

void Logger::setLevel(int level) {
  m_level = level;
  if (m_filterButton != nullptr) {
    m_filterButton->setText(filterButtonText(level));
    if (QMenu* filterMenu = m_filterButton->menu()) {
      for (QAction* action : filterMenu->actions()) {
        action->setChecked(action->data().toInt() == level);
      }
    }
  }
  rebuildText();
}

void Logger::syncDiagnostics() {
  const auto& diagnostics = m_workspace.snapshot().diagnostics();
  const auto diagnosticCount = static_cast<qsizetype>(diagnostics.size());
  if (m_suppressedDiagnostics > diagnosticCount ||
      m_observedDiagnostics > diagnosticCount) {
    m_suppressedDiagnostics = 0;
    rebuildText();
    return;
  }

  for (qsizetype index = m_observedDiagnostics; index < diagnosticCount; ++index) {
    if (index < m_suppressedDiagnostics) {
      continue;
    }
    const auto& diagnostic = diagnostics[static_cast<size_t>(index)];
    const int level = diagnosticLevel(diagnostic.severity);
    if (level > m_level) {
      continue;
    }

    QString message;
    if (!diagnostic.code.empty()) {
      message.append(QLatin1Char('['));
      message.append(QString::fromStdString(diagnostic.code));
      message.append(QStringLiteral("] "));
    }
    message.append(QString::fromStdString(diagnostic.message));
    m_pendingMessages.append({std::move(message), level});
  }
  m_observedDiagnostics = diagnosticCount;

  if (m_pendingMessages.size() >= kFlushMessageThreshold) {
    flushPending();
  } else if (!m_pendingMessages.isEmpty()) {
    m_flushTimer->start(kFlushIntervalMs);
  }
}

void Logger::rebuildText() {
  m_flushTimer->stop();
  m_pendingMessages.clear();
  m_textArea->clear();
  m_observedDiagnostics = m_suppressedDiagnostics;
  syncDiagnostics();
  flushPending();
}

void Logger::flushPending() {
  m_flushTimer->stop();
  if (m_pendingMessages.isEmpty()) {
    return;
  }

  QTextCursor cursor = m_textArea->textCursor();
  cursor.movePosition(QTextCursor::End);
  cursor.beginEditBlock();
  m_textArea->setUpdatesEnabled(false);

  QTextCharFormat textFormat;
  textFormat.setForeground(m_textArea->palette().color(QPalette::Text));
  for (const PendingMessage& message : std::as_const(m_pendingMessages)) {
    QTextCharFormat prefixFormat;
    prefixFormat.setForeground(levelColor(message.level));
    cursor.insertText(levelPrefix(message.level), prefixFormat);
    cursor.insertText(QStringLiteral(" "), textFormat);
    cursor.insertText(message.text, textFormat);
    cursor.insertBlock();
  }

  cursor.endEditBlock();
  m_textArea->setTextCursor(cursor);
  m_textArea->setUpdatesEnabled(true);
  m_textArea->ensureCursorVisible();
  m_pendingMessages.clear();
}

void Logger::refreshTitleBarControls() {
  if (m_titleBar == nullptr) {
    return;
  }
  const QPalette palette = m_titleBar->palette();
  m_filterButton->setStyleSheet(toolBarTextButtonStyle(palette, kFilterButtonLeftMargin));
  refreshStencilToolButton(m_clearButton, QStringLiteral(":/icons/trash-can-outline.svg"), palette);
  refreshStencilToolButton(m_exportButton, QStringLiteral(":/icons/export.svg"), palette);
}
