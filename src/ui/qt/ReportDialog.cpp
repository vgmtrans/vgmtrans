/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ReportDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <QSysInfo>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QClipboard>
#include <QTimer>
#include <version.h>
#include "LogManager.h"
#include "Root.h"
#include "RawFile.h"
#include "VGMFile.h"
#include "util/Helpers.h"
#include "Logger.h"

ReportDialog::ReportDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Report a Bug"));
  setMinimumWidth(750);

  setWindowTitle(tr("Report a Bug"));
  resize(800, 800);

  auto* layout = new QVBoxLayout(this);
  layout->setSpacing(15);
  layout->setContentsMargins(20, 20, 20, 20);

  auto* tracker_label = new QLabel(tr("Visit the <a href=\"https://github.com/vgmtrans/vgmtrans/issues\">bug tracker</a> to check for existing issues."), this);
  tracker_label->setOpenExternalLinks(true);
  tracker_label->setStyleSheet("font-size: small;");
  layout->addWidget(tracker_label);

  auto* info_label = new QLabel(tr("This dialog will open GitHub in your default browser. You must be logged in to report a bug.\n"
                                   "Version, system details, and selected files info will be automatically attached. The report page on GitHub may prompt for additional information.\n"
                                   "Fields marked with * are required."), this);
  info_label->setWordWrap(true);
  info_label->setStyleSheet("font-size: small; color: gray;");
  layout->addWidget(info_label);

  auto* form_layout = new QFormLayout();
  form_layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
  form_layout->setLabelAlignment(Qt::AlignRight);

  m_title_edit = new QLineEdit(this);
  m_title_edit->setPlaceholderText(tr("Type a very short description of the problem here"));
  m_title_edit->setMaxLength(60);
  form_layout->addRow(tr("Summary*:"), m_title_edit);

  m_desc_edit = new QPlainTextEdit(this);
  m_desc_edit->setPlaceholderText(tr("Instead of:\n\"MIDI is broken\"\n\nWrite:\n\"Track 3 is missing percussion after 00:12.\nThe original game playback includes drums at that timestamp.\""));
  m_desc_edit->setMinimumHeight(100);
  form_layout->addRow(tr("Description*:"), m_desc_edit);

  m_steps_edit = new QPlainTextEdit(this);
  m_steps_edit->setPlaceholderText(tr("1.\n2.\n3."));
  m_steps_edit->setMinimumHeight(100);
  form_layout->addRow(tr("Steps to Reproduce*:"), m_steps_edit);

  m_expected_edit = new QPlainTextEdit(this);
  m_expected_edit->setPlaceholderText(tr("Actual:\nTrack 2 is silent after 00:15.\n\nExpected:\nTrack 2 should contain the melody heard in-game."));
  m_expected_edit->setMinimumHeight(100);
  form_layout->addRow(tr("Expected Behavior*:"), m_expected_edit);

  layout->addLayout(form_layout);

  auto* asset_layout = new QHBoxLayout();

  // Raw Files Box
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

  // VGM Files Box
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

  layout->addLayout(asset_layout);

  auto* url_status_layout = new QHBoxLayout();
  m_status_label = new QLabel(this);
  url_status_layout->addWidget(m_status_label);
  
  m_warning_label = new QLabel(tr("What a long report! Redirecting to a blank issue. Please use 'Copy as Markdown' and paste the contents."), this);
  m_warning_label->setStyleSheet("color: orange; font-style: italic;");
  m_warning_label->hide();
  url_status_layout->addWidget(m_warning_label);

  m_feedback_label = new QLabel(this);
  m_feedback_label->setStyleSheet("color: #55aaff;");
  url_status_layout->addWidget(m_feedback_label);
  url_status_layout->addStretch();
  layout->addLayout(url_status_layout);

  auto* button_box = new QDialogButtonBox(QDialogButtonBox::Close, this);
  auto* submit_button = new QPushButton(tr("Go to GitHub"), this);
  submit_button->setDefault(true);
  button_box->addButton(submit_button, QDialogButtonBox::AcceptRole);
  
  m_copy_button = new QPushButton(tr("Copy as Markdown"), this);
  button_box->addButton(m_copy_button, QDialogButtonBox::ActionRole);

  m_submit_button = button_box->button(QDialogButtonBox::Ok);
  if (!m_submit_button) {
    m_submit_button = submit_button;
  }

  layout->addWidget(button_box);

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

  connect(m_copy_button, &QPushButton::clicked, [this]() {
    QGuiApplication::clipboard()->setText(getMarkdownReport());
    m_feedback_label->setText(tr("Report copied to clipboard!"));
  });

  connect(button_box, &QDialogButtonBox::accepted, this, &ReportDialog::submitReport);
  connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

  updateUrlStatus();
}

void ReportDialog::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    m_submit_button->setFocus();
    event->ignore();
    return;
  }
  QDialog::keyPressEvent(event);
}

void ReportDialog::updateUrlStatus() {
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
        QByteArray data = QByteArray::fromRawData(raw_file->data(), raw_file->size());
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

  QString asset_list = assets.join(", ");
  query.addQueryItem("affected_files", asset_list);
  url.setQuery(query);

  int length = url.toEncoded().length();
  m_chars_remaining = 2048 - length;
  m_status_label->setText(tr("%1 characters remaining").arg(m_chars_remaining));
  
  if (m_chars_remaining < 0) {
    m_status_label->setStyleSheet("color: #ffaa00;");
    m_warning_label->show();
  } else {
    m_status_label->setStyleSheet("color: #55ff55;");
    m_warning_label->hide();
  }

  bool valid = !m_title_edit->text().trimmed().isEmpty() &&
               !m_desc_edit->toPlainText().trimmed().isEmpty() &&
               !m_steps_edit->toPlainText().trimmed().isEmpty() &&
               !m_expected_edit->toPlainText().trimmed().isEmpty();

  m_submit_button->setEnabled(valid);
  m_copy_button->setEnabled(valid);

  if (!valid) {
    m_feedback_label->clear();
  }
}

QString ReportDialog::getMarkdownReport() const {
  QString md = QString("# Bug Report\n\n## System Details\n- VGMTrans version: %1 (%2, %3)\n- Operating system: %4\n\n")
                   .arg(VGMTRANS_VERSION, VGMTRANS_REVISION, VGMTRANS_BRANCH, QSysInfo::prettyProductName());
  
  md += "## Description\n" + m_desc_edit->toPlainText().trimmed() + "\n\n";
  md += "## Steps to Reproduce\n" + m_steps_edit->toPlainText().trimmed() + "\n\n";
  md += "## Expected Behavior\n" + m_expected_edit->toPlainText().trimmed() + "\n\n";
  md += "## Affected Assets\n";

  bool any_asset = false;
  for (int i = 0; i < m_raw_list->count(); ++i) {
    if (auto* item = m_raw_list->item(i); item->checkState() == Qt::Checked) {
      any_asset = true;
      QString sha = item->data(Qt::UserRole + 1).toString();
      auto* raw_file = static_cast<RawFile*>(item->data(Qt::UserRole).value<void*>());

      if (sha.isEmpty()) {
        QByteArray data = QByteArray::fromRawData(raw_file->data(), static_cast<int>(raw_file->size()));
        sha = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
        item->setData(Qt::UserRole + 1, sha);
      }
      md += QString("- Raw File: %1 (SHA-256: %2)\n").arg(QString::fromStdString(raw_file->name()), sha);
    }
  }

  QTreeWidgetItemIterator it(m_vgm_tree);
  while (*it) {
    if ((*it)->checkState(0) == Qt::Checked) {
      any_asset = true;
      md += QString("- VGM File: %1 [%2]\n").arg((*it)->text(0), (*it)->text(1));
    }
    ++it;
  }

  if (!any_asset) {
    md += "None selected.\n";
  }

  md += "\n## Logs\n```\n" + Logger::getLogText() + "\n```\n";

  return md;
}

void ReportDialog::submitReport() {
  updateUrlStatus(); // Final update

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

  QString asset_list = assets.join(", ");
  query.addQueryItem("affected_files", asset_list);

  url.setQuery(query);

  L_DEBUG("submitReport: Passing human-readable URL to qtOpenUrl: {}", url.toString().toStdString());
  if (url.toEncoded().length() > 2048) {
    qtOpenUrl(QUrl("https://github.com/vgmtrans/vgmtrans/issues/new"));
  } else {
    qtOpenUrl(url);
  }

  m_feedback_label->setText(tr("GitHub opened in browser. You can now close this window."));
}
