/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "application/WorkspaceController.h"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QString>

#include <optional>
#include <vector>

namespace vgmtrans::ui {

inline constexpr int IdRole = Qt::UserRole + 1;
inline constexpr int IsCollectionRole = Qt::UserRole + 2;
inline constexpr int IsLastItemRole = Qt::UserRole + 3;

class SourceTableModel final : public QAbstractTableModel {
public:
  explicit SourceTableModel(WorkspaceController& workspace, QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
  WorkspaceController& workspace_;
};

class AssetTableModel final : public QAbstractTableModel {
public:
  explicit AssetTableModel(WorkspaceController& workspace, QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
  WorkspaceController& workspace_;
};

class CollectionTableModel final : public QAbstractTableModel {
public:
  explicit CollectionTableModel(WorkspaceController& workspace, QObject* parent = nullptr);

  void setPlayingCollection(std::optional<core::CollectionId> collection);
  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
  WorkspaceController& workspace_;
  std::optional<core::CollectionId> playingCollection_;
};

class CollectionFilterProxyModel final : public QSortFilterProxyModel {
public:
  explicit CollectionFilterProxyModel(WorkspaceController& workspace,
                                      QObject* parent = nullptr);

protected:
  [[nodiscard]] bool filterAcceptsRow(
      int sourceRow, const QModelIndex& sourceParent) const override;

private:
  WorkspaceController& workspace_;
};

class CollectionContentsModel final : public QAbstractTableModel {
public:
  explicit CollectionContentsModel(WorkspaceController& workspace, QObject* parent = nullptr);

  void setCollection(std::optional<core::CollectionId> collection);
  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
  struct Entry {
    QString role;
    core::AssetId asset;
  };

  void rebuild();

  WorkspaceController& workspace_;
  std::optional<core::CollectionId> collection_;
  std::vector<Entry> entries_;
  bool resetting_ = false;
};

class DiagnosticTableModel final : public QAbstractTableModel {
public:
  explicit DiagnosticTableModel(WorkspaceController& workspace, QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
  WorkspaceController& workspace_;
};

}  // namespace vgmtrans::ui
