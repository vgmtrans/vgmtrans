/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ReportDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QUrl>
#include <QUrlQuery>
#include <QSysInfo>
#include <QCryptographicHash>
#include <QTimer>
#include <version.h>
#include "Root.h"
#include "RawFile.h"
#include "VGMFile.h"
#include "util/Helpers.h"

ReportDialog::ReportDialog(QWidget* parent) : QWidget(parent) {
  setWindowFlags(Qt::Window);
  setWindowTitle(tr("Report a Bug"));

  auto* layout = new QVBoxLayout(this);
  layout->setSpacing(15);
  layout->setContentsMargins(20, 20, 20, 20);

#ifdef Q_OS_MAC
  /* I know that this is a Qt sin, but the default 
  macOS theme is very inconsistent in squared vs rounded elements */
  this->setStyleSheet(
      "QPlainTextEdit, QLineEdit {"
      "  background-color: palette(base);"
      "  border: 1px solid palette(mid);"
      "  border-radius: 5px;"
      "  padding: 4px;"
      "}"
  );
#endif

  auto* info_label = new QLabel(this);
  info_label->setText(tr(
    "Use this form to auto-fill a bug report on our GitHub  <a href=\"https://github.com/vgmtrans/vgmtrans/issues\">issue tracker</a>.<br/>"
    "<b>You must be logged in to a GitHub account.</b>"));
  info_label->setTextFormat(Qt::RichText);
  info_label->setOpenExternalLinks(true);
  info_label->setTextInteractionFlags(Qt::TextBrowserInteraction);
  info_label->setWordWrap(true);
  layout->addWidget(info_label);

  auto* title_layout = new QVBoxLayout();
  title_layout->setSpacing(5);
  m_title_edit = new QLineEdit(this);
  m_title_edit->setPlaceholderText(tr("Type a very short description of the problem here"));
  m_title_edit->setMaxLength(60);
  auto* summary_label = new QLabel(tr("&Summary <span style=\"color:red\">*</span>"), this);
  summary_label->setTextFormat(Qt::RichText);
  summary_label->setBuddy(m_title_edit);
  title_layout->addWidget(summary_label);
  title_layout->addWidget(m_title_edit);
  layout->addLayout(title_layout);

  auto* main_scroll = new QScrollArea(this);
  main_scroll->setWidgetResizable(true);
  main_scroll->setFrameShape(QFrame::NoFrame);
  main_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

  auto* scroll_widget = new QWidget(this);
  auto* scroll_layout = new QVBoxLayout(scroll_widget);
  scroll_layout->setContentsMargins(0, 0, 0, 0);
  scroll_layout->setSpacing(20);

  auto* details_layout = new QVBoxLayout();
  details_layout->setSpacing(4);
  details_layout->setContentsMargins(0, 10, 0, 10);

  m_desc_edit = new QPlainTextEdit(this);
  m_desc_edit->setFixedHeight(100);
  m_desc_edit->setTabChangesFocus(true);
  m_desc_edit->setPlaceholderText(tr("Instead of:\n\"MIDI is broken\"\nWrite:\n\"Track 3 is missing percussion after 00:12.\nThe original game playback includes drums at that timestamp.\"\n\nIf this also involves exported files, it's easier to understand if you mention where, e.g. \"broken in FL Studio 25\"."));
  auto* desc_label = new QLabel(tr("&Description <span style=\"color:red\">*</span>"), this);
  desc_label->setTextFormat(Qt::RichText);
  desc_label->setBuddy(m_desc_edit);
  details_layout->addWidget(desc_label);
  details_layout->addWidget(m_desc_edit);
  details_layout->addSpacing(10);

  m_steps_edit = new QPlainTextEdit(this);
  m_steps_edit->setFixedHeight(100);
  m_steps_edit->setTabChangesFocus(true);
  m_steps_edit->setPlaceholderText(tr("1.\n2.\n3."));
  auto* steps_label = new QLabel(tr("St&eps to Reproduce <span style=\"color:red\">*</span>"), this);
  steps_label->setTextFormat(Qt::RichText);
  steps_label->setBuddy(m_steps_edit);
  details_layout->addWidget(steps_label);
  details_layout->addWidget(m_steps_edit);
  details_layout->addSpacing(10);

  m_expected_edit = new QPlainTextEdit(this);
  m_expected_edit->setFixedHeight(100);
  m_expected_edit->setTabChangesFocus(true);
  m_expected_edit->setPlaceholderText(tr("Actual:\nTrack 2 is silent after 00:15.\n\nExpected:\nTrack 2 should contain the melody heard in-game."));
  auto* expected_label = new QLabel(tr("E&xpected Behavior <span style=\"color:red\">*</span>"), this);
  expected_label->setTextFormat(Qt::RichText);
  expected_label->setBuddy(m_expected_edit);
  details_layout->addWidget(expected_label);
  details_layout->addWidget(m_expected_edit);
  scroll_layout->addLayout(details_layout);
  
  auto* files_label = new QLabel(tr("Affected Files"), this);
  scroll_layout->addWidget(files_label);

  auto* files_layout = new QVBoxLayout();
  auto* asset_layout = new QHBoxLayout();

  auto* raw_group = new QGroupBox(tr("Raw Files"), this);
  auto* raw_vbox = new QVBoxLayout(raw_group);
  m_raw_list = new QListWidget(raw_group);
  m_raw_list->setSelectionMode(QAbstractItemView::NoSelection);
  for (const auto& raw_file : pRoot->rawFiles()) {
    auto* item = new QListWidgetItem(QString::fromStdString(raw_file->name()), m_raw_list);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
    item->setData(Qt::UserRole, QVariant::fromValue(static_cast<void*>(raw_file)));
  }
  m_raw_list->setMinimumHeight(200);
  raw_vbox->addWidget(m_raw_list);
  asset_layout->addWidget(raw_group);

  auto* vgm_group = new QGroupBox(tr("VGM Files"), this);
  auto* vgm_vbox = new QVBoxLayout(vgm_group);
  
  auto* search_edit = new QLineEdit(vgm_group);
  search_edit->setPlaceholderText(tr("Search files..."));
  vgm_vbox->addWidget(search_edit);

  m_vgm_tree = new QTreeWidget(vgm_group);
  m_vgm_tree->setColumnCount(2);
  m_vgm_tree->setHeaderLabels({tr("Name"), tr("Type")});
  m_vgm_tree->setColumnWidth(0, 250);
  m_vgm_tree->setSelectionMode(QAbstractItemView::NoSelection);

  for (const auto& vgm_file_var : pRoot->vgmFiles()) {
    if (auto* vgm_file = variantToVGMFile(vgm_file_var)) {
      auto* item = new QTreeWidgetItem(m_vgm_tree);
      item->setText(0, QString::fromStdString(vgm_file->name()));
      item->setText(1, QString::fromStdString(vgm_file->formatName()));
      item->setIcon(0, iconForFile(vgm_file_var));
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(0, Qt::Unchecked);
      item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void*>(vgm_file)));
    }
  }

  connect(search_edit, &QLineEdit::textChanged, [this](const QString& text) {
    for (int i = 0; i < m_vgm_tree->topLevelItemCount(); ++i) {
      auto* item = m_vgm_tree->topLevelItem(i);
      bool match = item->text(0).contains(text, Qt::CaseInsensitive) || 
                   item->text(1).contains(text, Qt::CaseInsensitive);
      item->setHidden(!match);
    }
  });

  m_vgm_tree->setMinimumHeight(200);
  vgm_vbox->addWidget(m_vgm_tree);
  asset_layout->addWidget(vgm_group);

  files_layout->addLayout(asset_layout);
  scroll_layout->addLayout(files_layout);
  main_scroll->setWidget(scroll_widget);

  layout->addWidget(main_scroll);

  auto* url_status_layout = new QHBoxLayout();

  m_feedback_label = new QLabel(this);
  m_feedback_label->setStyleSheet(QString("color: %1;").arg(palette().color(QPalette::Link).name()));
  url_status_layout->addWidget(m_feedback_label);
  url_status_layout->addStretch();
  layout->addLayout(url_status_layout);

  auto* button_layout = new QHBoxLayout();
  button_layout->addStretch();


  auto* close_button = new QPushButton(tr("Close"), this);
  button_layout->addWidget(close_button);

  m_submit_button = new QPushButton(tr("Autofill on Github"), this);
  m_submit_button->setDefault(true);
  button_layout->addWidget(m_submit_button);

  layout->addLayout(button_layout);

  m_update_timer = new QTimer(this);
  m_update_timer->setSingleShot(true);
  connect(m_update_timer, &QTimer::timeout, this, &ReportDialog::updateUrlStatus);

  auto trigger_update = [this]() { m_update_timer->start(300); };
  connect(m_title_edit, &QLineEdit::textChanged, trigger_update);
  connect(m_desc_edit, &QPlainTextEdit::textChanged, trigger_update);
  connect(m_steps_edit, &QPlainTextEdit::textChanged, trigger_update);
  connect(m_expected_edit, &QPlainTextEdit::textChanged, trigger_update);
  connect(m_raw_list, &QListWidget::itemChanged, trigger_update);
  connect(m_vgm_tree, &QTreeWidget::itemChanged, [this](QTreeWidgetItem*, int) { 
    m_update_timer->start(300); 
  });



  connect(m_submit_button, &QPushButton::clicked, this, &ReportDialog::submitReport);
  connect(close_button, &QPushButton::clicked, this, &QWidget::close);

  m_title_edit->setFocus();

  QWidget::setTabOrder(m_title_edit, main_scroll);
  QWidget::setTabOrder(main_scroll, m_desc_edit);
  QWidget::setTabOrder(m_desc_edit, m_steps_edit);
  QWidget::setTabOrder(m_steps_edit, m_expected_edit);
  QWidget::setTabOrder(m_expected_edit, m_raw_list);
  QWidget::setTabOrder(m_raw_list, search_edit);
  QWidget::setTabOrder(search_edit, m_vgm_tree);
  QWidget::setTabOrder(m_vgm_tree, close_button);
  QWidget::setTabOrder(close_button, m_submit_button);

  updateUrlStatus();
}
QUrl ReportDialog::buildReportUrl() const {
  QUrl url("https://github.com/vgmtrans/vgmtrans/issues/new");
  QUrlQuery query;
  query.addQueryItem("template", "1-bug-report.yml");
  query.addQueryItem("title", m_title_edit->text().trimmed());
  query.addQueryItem("description", m_desc_edit->toPlainText().trimmed());
  query.addQueryItem("steps", m_steps_edit->toPlainText().trimmed());
  query.addQueryItem("expected", m_expected_edit->toPlainText().trimmed());

  const QString system_details = QString("VGMTrans version: %1 (%2, %3)\nOperating system: %4")
                                    .arg(VGMTRANS_VERSION, VGMTRANS_REVISION, VGMTRANS_BRANCH, QSysInfo::prettyProductName());
  query.addQueryItem("system_details", system_details);

  QStringList assets;
  for (int i = 0; i < m_raw_list->count(); ++i) {
    if (auto* item = m_raw_list->item(i); item->checkState() == Qt::Checked) {
      QString sha = item->data(Qt::UserRole + 1).toString();
      auto* raw_file = static_cast<RawFile*>(item->data(Qt::UserRole).value<void*>());

      if (sha.isEmpty()) {
        QByteArray data = QByteArray::fromRawData(raw_file->data(), static_cast<int>(raw_file->size()));
        sha = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
        item->setData(Qt::UserRole + 1, sha);
      }
      assets << QString("%1 (%2)").arg(QString::fromStdString(raw_file->name()), sha);
    }
  }

  QTreeWidgetItemIterator it(m_vgm_tree);
  while (*it) {
    if ((*it)->checkState(0) == Qt::Checked) {
      assets << QString("%1 [%2]").arg((*it)->text(0), (*it)->text(1));
    }
    ++it;
  }

  query.addQueryItem("affected_files", assets.join(", "));
  url.setQuery(query);
  return url;
}

void ReportDialog::updateUrlStatus() {
  bool valid = !m_title_edit->text().trimmed().isEmpty() &&
               !m_desc_edit->toPlainText().trimmed().isEmpty() &&
               !m_steps_edit->toPlainText().trimmed().isEmpty() &&
               !m_expected_edit->toPlainText().trimmed().isEmpty();

  m_submit_button->setEnabled(valid);

  if (!valid) {
    m_feedback_label->clear();
  }
}

void ReportDialog::submitReport() {
  qtOpenUrl(buildReportUrl());
  m_feedback_label->setText(tr("GitHub opened in browser. You can now close this window."));
}
