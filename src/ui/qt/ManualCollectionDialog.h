/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <QDialog>

#include <optional>

class QLineEdit;
class QListWidget;
class QButtonGroup;

namespace vgmtrans::ui {
class WorkspaceController;
}

class ManualCollectionDialog final : public QDialog {
  Q_OBJECT

public:
  explicit ManualCollectionDialog(vgmtrans::ui::WorkspaceController& workspace, QWidget* parent = nullptr);

  [[nodiscard]] std::optional<vgmtrans::core::CollectionId> createdCollection() const noexcept {
    return m_created_collection;
  }
  [[nodiscard]] bool mayBeSilent() const noexcept { return m_may_be_silent; }

private:
  void createCollection();

  vgmtrans::ui::WorkspaceController& m_workspace;
  QLineEdit* m_name_field{};
  QButtonGroup* m_seq_buttons{};
  QListWidget* m_instr_list{};
  QListWidget* m_samp_list{};
  std::optional<vgmtrans::core::CollectionId> m_created_collection;
  bool m_may_be_silent = false;
};
