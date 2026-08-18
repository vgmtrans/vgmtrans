/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ManualCollectionDialog.h"

#include "application/WorkspaceController.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>

#include <exception>
#include <vector>

namespace {

template <typename Asset>
QListWidget* makeAssetList(const vgmtrans::core::SessionSnapshot& snapshot, QButtonGroup* buttons, QWidget* parent) {
  auto* list = new QListWidget(parent);
  list->setSelectionMode(QAbstractItemView::NoSelection);
  for (const auto& value : snapshot.assets()) {
    const auto* asset = std::get_if<Asset>(&value);
    if (asset == nullptr) {
      continue;
    }

    auto* item = new QListWidgetItem(QString::fromStdString(asset->metadata.name), list);
    item->setData(Qt::UserRole, asset->metadata.id.value);
    item->setToolTip(QString::fromStdString(asset->metadata.format));
    if (buttons == nullptr) {
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(Qt::Unchecked);
    } else {
      auto* radio = new QRadioButton(item->text(), list);
      radio->setProperty("assetId", asset->metadata.id.value);
      radio->setToolTip(item->toolTip());
      buttons->addButton(radio);
      item->setText({});
      list->setItemWidget(item, radio);
    }
  }
  return list;
}

std::vector<vgmtrans::core::AssetId> checkedAssets(const QListWidget& list) {
  std::vector<vgmtrans::core::AssetId> assets;
  for (int row = 0; row < list.count(); ++row) {
    const auto* item = list.item(row);
    if (item->checkState() == Qt::Checked) {
      assets.push_back(vgmtrans::core::AssetId{item->data(Qt::UserRole).toUInt()});
    }
  }
  return assets;
}

bool needsExternalSamples(const vgmtrans::core::SessionSnapshot& snapshot,
                          const std::vector<vgmtrans::core::AssetId>& soundBanks) {
  for (const auto id : soundBanks) {
    const auto* set = snapshot.asset<vgmtrans::core::SoundBankAsset>(id);
    if (set == nullptr) {
      continue;
    }
    for (const auto& instrument : set->instruments) {
      for (const auto& region : instrument.regions) {
        if (!region.sample.empty() && region.sample.owner() != set->metadata.id) {
          return true;
        }
      }
    }
  }
  return false;
}

}  // namespace

ManualCollectionDialog::ManualCollectionDialog(vgmtrans::ui::WorkspaceController& workspace, QWidget* parent)
    : QDialog(parent), m_workspace(workspace) {
  setWindowTitle(tr("Manual collection creation"));
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setWindowModality(Qt::WindowModal);
  resize(480, 600);

  auto* nameLabel = new QLabel(tr("Collection &name"), this);
  m_name_field = new QLineEdit(tr("User-defined collection"), this);
  m_name_field->selectAll();
  nameLabel->setBuddy(m_name_field);

  auto* sequenceLabel = new QLabel(tr("Music &sequence"), this);
  m_seq_buttons = new QButtonGroup(this);
  auto* sequenceList = makeAssetList<vgmtrans::core::SequenceProgramAsset>(workspace.snapshot(), m_seq_buttons, this);
  sequenceLabel->setBuddy(sequenceList);

  auto* instrumentLabel = new QLabel(tr("&Sound banks"), this);
  m_instr_list = makeAssetList<vgmtrans::core::SoundBankAsset>(workspace.snapshot(), nullptr, this);
  instrumentLabel->setBuddy(m_instr_list);

  auto* sampleLabel = new QLabel(tr("Sample &pools"), this);
  m_samp_list = makeAssetList<vgmtrans::core::SamplePoolAsset>(workspace.snapshot(), nullptr, this);
  sampleLabel->setBuddy(m_samp_list);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  auto* create = buttons->addButton(tr("Create collection"), QDialogButtonBox::AcceptRole);
  create->setDefault(true);

  auto* layout = new QGridLayout(this);
  layout->addWidget(nameLabel, 0, 0);
  layout->addWidget(m_name_field, 0, 1);
  layout->addWidget(sequenceLabel, 1, 0, 1, 2);
  layout->addWidget(sequenceList, 2, 0, 1, 2);
  layout->addWidget(instrumentLabel, 3, 0, 1, 2);
  layout->addWidget(m_instr_list, 4, 0, 1, 2);
  layout->addWidget(sampleLabel, 5, 0, 1, 2);
  layout->addWidget(m_samp_list, 6, 0, 1, 2);
  layout->addWidget(buttons, 7, 0, 1, 2);

  connect(m_name_field, &QLineEdit::textChanged, create,
          [create](const QString& text) { create->setEnabled(!text.trimmed().isEmpty()); });
  connect(create, &QPushButton::clicked, this, &ManualCollectionDialog::createCollection);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ManualCollectionDialog::createCollection() {
  const auto* sequence = m_seq_buttons->checkedButton();
  if (sequence == nullptr) {
    QMessageBox::critical(this, tr("Error creating collection"), tr("A music sequence must be selected"));
    return;
  }

  vgmtrans::core::CollectionMembers members{
      .sequence = vgmtrans::core::AssetId{sequence->property("assetId").toUInt()},
      .soundBanks = checkedAssets(*m_instr_list),
      .samplePools = checkedAssets(*m_samp_list),
  };
  if (members.soundBanks.empty()) {
    QMessageBox::critical(this, tr("Error creating collection"), tr("At least one sound bank must be selected"));
    return;
  }

  m_may_be_silent = members.samplePools.empty() && needsExternalSamples(m_workspace.snapshot(), members.soundBanks);
  try {
    m_created_collection =
        m_workspace.createUserCollection(m_name_field->text().trimmed().toStdString(), std::move(members));
  } catch (const std::exception& error) {
    QMessageBox::critical(this, tr("Error creating collection"), QString::fromUtf8(error.what()));
    return;
  }
  accept();
}
