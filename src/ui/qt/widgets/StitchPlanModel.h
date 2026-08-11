/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <QIdentityProxyModel>

#include <optional>
#include <span>
#include <vector>

namespace vgmtrans::ui {

class StitchPlanModel final : public QIdentityProxyModel {
public:
  explicit StitchPlanModel(QObject* parent = nullptr);

  void setCollections(std::span<const core::CollectionId> collections);
  void setPlayingCollection(std::optional<core::CollectionId> collection);

  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
  void refreshDecorations();

  std::vector<core::CollectionId> collections_;
  std::optional<core::CollectionId> playingCollection_;
};

}  // namespace vgmtrans::ui
