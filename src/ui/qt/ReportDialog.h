/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QListWidget;
class QTreeWidget;
class QLabel;

class ReportDialog : public QDialog {
  Q_OBJECT

public:
  explicit ReportDialog(QWidget* parent = nullptr);

protected:
  void keyPressEvent(QKeyEvent* event) override;

private:
  void updateUrlStatus();
  QString getMarkdownReport() const;
  void submitReport();

  QTimer* m_update_timer;
  QLineEdit* m_title_edit;
  QPlainTextEdit* m_desc_edit;
  QPlainTextEdit* m_steps_edit;
  QPlainTextEdit* m_expected_edit;
  QListWidget* m_raw_list;
  QTreeWidget* m_vgm_tree;
  QLabel* m_status_label;
  QLabel* m_warning_label;
  QLabel* m_feedback_label;
  QPushButton* m_submit_button;
  QPushButton* m_copy_button;
  int m_chars_remaining;
};
