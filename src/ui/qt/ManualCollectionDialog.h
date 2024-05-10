/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include <QDialog>

class QListWidget;
class QLineEdit;

class ManualCollectionDialog final : public QDialog {
public:
  explicit ManualCollectionDialog(QWidget* parent = nullptr);

private:
  static QListWidget* makeSequenceList();
  static QListWidget* makeInstrumentSetList();
  static QListWidget* makeSampleCollectionList();

  void createCollection();

  QLineEdit *m_name_field;
  QListWidget *m_seq_list;
  QListWidget *m_instr_list;
  QListWidget *m_samp_list;
};
