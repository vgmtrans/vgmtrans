/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QWidget>
#include <QUrl>

class QLineEdit;
class QPlainTextEdit;
class QListWidget;
class QTreeWidget;
class QLabel;
class QPushButton;

class ReportDialog : public QWidget {
  Q_OBJECT

public:
  explicit ReportDialog(QWidget* parent = nullptr);

private:
  void updateUrlStatus();
  QUrl buildReportUrl() const;
  void submitReport();

  QTimer* m_update_timer;
  QLineEdit* m_title_edit;
  QPlainTextEdit* m_desc_edit;
  QPlainTextEdit* m_steps_edit;
  QPlainTextEdit* m_expected_edit;
  QListWidget* m_raw_list;
  QTreeWidget* m_vgm_tree;
  QLabel* m_feedback_label;
  QPushButton* m_submit_button;
};
