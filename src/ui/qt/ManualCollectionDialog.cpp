/**
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "ManualCollectionDialog.h"

#include <QMessageBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QListWidget>
#include <QComboBox>
#include <QRadioButton>
#include <QCheckBox>
#include <vector>

#include "QtVGMRoot.h"
#include "Format.h"

#include <VGMSeq.h>
#include <VGMColl.h>
#include <VGMFile.h>
#include <VGMInstrSet.h>
#include <VGMSampColl.h>

ManualCollectionDialog::ManualCollectionDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle("Manual collection creation");
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint & Qt::Sheet);
  setWindowModality(Qt::WindowModal);

  auto name_label = new QLabel("Collection &name");
  m_name_field = new QLineEdit();
  m_name_field->setText("User-defined collection");
  name_label->setBuddy(m_name_field);

  auto seq_label = new QLabel("Music &sequence");
  m_seq_list = makeSequenceList();
  seq_label->setBuddy(m_seq_list);

  auto instr_label = new QLabel("&Instrument set");
  m_instr_list = makeInstrumentSetList();
  instr_label->setBuddy(m_instr_list);

  auto samp_label = new QLabel("Sample &collections");
  m_samp_list = makeSampleCollectionList();
  samp_label->setBuddy(m_samp_list);

  auto attempt = new QPushButton("Create collection");
  attempt->setAutoDefault(true);

  auto cancel = new QPushButton("Cancel");

  auto l = new QGridLayout();
  l->addWidget(name_label, 0, 0);
  l->addWidget(m_name_field, 0, 1);
  l->addWidget(seq_label, 1, 0, 1, -1);
  l->addWidget(m_seq_list, 2, 0, 1, -1);
  l->addWidget(instr_label, 3, 0, 1, -1);
  l->addWidget(m_instr_list, 4, 0, 1, -1);
  l->addWidget(samp_label, 5, 0, 1, -1);
  l->addWidget(m_samp_list, 6, 0, 1, -1);
  l->addWidget(cancel, 7, 0);
  l->addWidget(attempt, 7, 1);
  setLayout(l);

  connect(m_name_field, &QLineEdit::textChanged,
          [=](const QString &text) { attempt->setEnabled(!text.isEmpty()); });
  connect(attempt, &QPushButton::pressed, this, &ManualCollectionDialog::createCollection);
  connect(cancel, &QPushButton::pressed, this, &ManualCollectionDialog::close);
}

QListWidget *ManualCollectionDialog::makeSequenceList() {
  std::vector<VGMFile *> seqs;
  const auto& files = qtVGMRoot.vgmFiles();
  for (const auto& fileVariant : files) {
    if (std::holds_alternative<VGMSeq*>(fileVariant)) {
      if (VGMSeq* seq = std::get<VGMSeq*>(fileVariant)) {
        seqs.push_back(seq);
      }
    }
  }

  auto widget = new QListWidget();
  for (auto seq : seqs) {
    auto seq_item = new QListWidgetItem(widget);
    seq_item->setData(Qt::UserRole, QVariant::fromValue(static_cast<void*>(seq)));
    widget->setItemWidget(seq_item, new QRadioButton(QString::fromStdString(seq->name())));
  }

  return widget;
}

QListWidget *ManualCollectionDialog::makeInstrumentSetList() {
  std::vector<VGMFile *> instrSets;
  const auto& files = qtVGMRoot.vgmFiles();
  for (const auto& fileVariant : files) {
    if (std::holds_alternative<VGMInstrSet*>(fileVariant)) {
      if (VGMInstrSet* instrSet = std::get<VGMInstrSet*>(fileVariant)) {
        instrSets.push_back(instrSet);
      }
    }
  }

  auto widget = new QListWidget();
  for (auto instrSet : instrSets) {
    auto instrset_item = new QListWidgetItem(widget);
    instrset_item->setData(Qt::UserRole, QVariant::fromValue(static_cast<void*>(instrSet)));
    widget->setItemWidget(instrset_item, new QCheckBox(QString::fromStdString(instrSet->name())));
  }

  return widget;
}

QListWidget *ManualCollectionDialog::makeSampleCollectionList() {
  std::vector<VGMFile *> sampColls;
  const auto& files = qtVGMRoot.vgmFiles();
  for (const auto& fileVariant : files) {
    if (std::holds_alternative<VGMSampColl*>(fileVariant)) {
      if (VGMSampColl* sampColl = std::get<VGMSampColl*>(fileVariant)) {
        sampColls.push_back(sampColl);
      }
    }
  }

  auto widget = new QListWidget();
  for (auto sampColl : sampColls) {
    auto sampcoll_item = new QListWidgetItem(widget);
    sampcoll_item->setData(Qt::UserRole, QVariant::fromValue(static_cast<void*>(sampColl)));
    widget->setItemWidget(sampcoll_item, new QCheckBox(QString::fromStdString(sampColl->name())));
  }

  return widget;
}

void ManualCollectionDialog::createCollection() {
  VGMSeq *chosen_seq = nullptr;
  for (int i = 0; i < m_seq_list->count(); i++) {
    auto item = m_seq_list->item(i);
    auto radio = qobject_cast<QRadioButton *>(m_seq_list->itemWidget(item));
    if (radio->isChecked()) {
      chosen_seq = static_cast<VGMSeq *>(item->data(Qt::UserRole).value<void *>());
      break;
    }
  }

  if (!chosen_seq) {
    QMessageBox::critical(this, "Error creating collection", "A music sequence must be selected");
    return;
  }

  // Get the VGMColl class for the format of the chosen sequence
  auto coll = chosen_seq->format()->newCollection();
  coll->setName(m_name_field->text().toStdString());
  coll->useSeq(chosen_seq);

  for (int i = 0; i < m_instr_list->count(); i++) {
    auto item = m_instr_list->item(i);
    auto radio = qobject_cast<QCheckBox *>(m_instr_list->itemWidget(item));
    if (radio->checkState() == (Qt::Checked)) {
      auto chosen_set = static_cast<VGMInstrSet *>(item->data(Qt::UserRole).value<void *>());
      coll->addInstrSet(chosen_set);
    }
  }
  if (coll->instrSets().empty()) {
    QMessageBox::critical(this, "Error creating collection",
                          "At least an instrument set must be selected");
    return;
  }

  for (int i = 0; i < m_samp_list->count(); i++) {
    auto item = m_samp_list->item(i);
    auto radio = qobject_cast<QCheckBox *>(m_samp_list->itemWidget(item));
    if (radio->checkState() == (Qt::Checked)) {
      auto sampcoll = static_cast<VGMSampColl *>(item->data(Qt::UserRole).value<void *>());
      coll->addSampColl(sampcoll);
    }
  }
  if (coll->sampColls().empty()) {
    QMessageBox::warning(this, windowTitle(),
                          "No sample collections were selected\nThe instrument bank will be silent...");
  }

  qtVGMRoot.addVGMColl(coll);
  close();
}
