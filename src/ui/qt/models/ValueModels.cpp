/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "models/ValueModels.h"

#include "util/Colors.h"
#include "util/TintableSvgIconEngine.h"

#include <QColor>
#include <QIcon>
#include <QVariant>

#include <array>
#include <variant>

namespace vgmtrans::ui {

namespace {

const QIcon& sourceIcon() {
  static const QIcon icon(QStringLiteral(":/icons/file-outline.svg"));
  return icon;
}

const QIcon& collectionIcon() {
  static const QIcon icon(QStringLiteral(":/icons/collection.svg"));
  return icon;
}

const QIcon& playingCollectionIcon() {
  static const QIcon icon(new TintableSvgIconEngine(
      QStringLiteral(":/icons/play.svg"), UIColors::PlayingIconColor));
  return icon;
}

const QIcon& assetIcon(const core::Asset& asset) {
  static const QIcon sequence(QStringLiteral(":/icons/sequence.svg"));
  static const QIcon instrumentSet(QStringLiteral(":/icons/instrument-set.svg"));
  static const QIcon sampleCollection(QStringLiteral(":/icons/sample-collection.svg"));
  static const QIcon miscellaneous(QStringLiteral(":/icons/binary.svg"));

  if (std::holds_alternative<core::SequenceProgramAsset>(asset)) {
    return sequence;
  }
  if (std::holds_alternative<core::InstrumentSetAsset>(asset)) {
    return instrumentSet;
  }
  if (std::holds_alternative<core::SampleCollectionAsset>(asset)) {
    return sampleCollection;
  }
  return miscellaneous;
}

QString severityName(core::Severity severity) {
  switch (severity) {
    case core::Severity::Info:
      return QStringLiteral("Info");
    case core::Severity::Warning:
      return QStringLiteral("Warning");
    case core::Severity::Error:
      return QStringLiteral("Error");
  }
  return {};
}

QColor severityColor(core::Severity severity) {
  switch (severity) {
    case core::Severity::Info:
      return QColor(Qt::darkCyan);
    case core::Severity::Warning:
      return QColor(176, 104, 0);
    case core::Severity::Error:
      return QColor(Qt::red);
  }
  return {};
}

const core::SourceFile* userSourceAt(const core::SessionSnapshot& snapshot, int row) {
  if (row < 0) {
    return nullptr;
  }
  int visibleRow = 0;
  for (const auto& source : snapshot.sources()) {
    if (source.derived()) {
      continue;
    }
    if (visibleRow++ == row) {
      return &source;
    }
  }
  return nullptr;
}

bool belongsToFamily(const core::SessionSnapshot& snapshot, core::SourceId source, core::SourceId familyRoot) {
  while (source.valid()) {
    if (source == familyRoot) {
      return true;
    }
    const auto* current = snapshot.source(source);
    if (current == nullptr || !current->parent) {
      return false;
    }
    source = *current->parent;
  }
  return false;
}

template <size_t Size>
QVariant horizontalHeader(int section, int role, const std::array<const char*, Size>& headers) {
  if (role != Qt::DisplayRole || section < 0 || static_cast<size_t>(section) >= headers.size()) {
    return {};
  }
  return QString::fromUtf8(headers[static_cast<size_t>(section)]);
}

}  // namespace

SourceTableModel::SourceTableModel(WorkspaceController& workspace, QObject* parent)
    : QAbstractTableModel(parent), workspace_(workspace) {
  connect(&workspace_, &WorkspaceController::snapshotAboutToChange, this, [this] { beginResetModel(); });
  connect(&workspace_, &WorkspaceController::snapshotChanged, this, [this] { endResetModel(); });
}

int SourceTableModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  int count = 0;
  for (const auto& source : workspace_.snapshot().sources()) {
    count += !source.derived();
  }
  return count;
}

int SourceTableModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : 2;
}

QVariant SourceTableModel::data(const QModelIndex& index, int role) const {
  const auto& snapshot = workspace_.snapshot();
  const auto* source = index.isValid() ? userSourceAt(snapshot, index.row()) : nullptr;
  if (source == nullptr) {
    return {};
  }
  if (role == IdRole) {
    return source->id.value;
  }
  if (role == Qt::DecorationRole && index.column() == 0) {
    return sourceIcon();
  }
  if (role != Qt::DisplayRole) {
    return {};
  }

  if (index.column() == 0) {
    return QString::fromStdString(source->title.value_or(source->name));
  }
  if (index.column() == 1) {
    size_t count = 0;
    for (const auto& asset : snapshot.assets()) {
      count += belongsToFamily(snapshot, core::metadata(asset).range.source, source->id);
    }
    return static_cast<qulonglong>(count);
  }
  return {};
}

QVariant SourceTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }
  return horizontalHeader(section, role, std::array{"Name", "Contained files"});
}

AssetTableModel::AssetTableModel(WorkspaceController& workspace, QObject* parent)
    : QAbstractTableModel(parent), workspace_(workspace) {
  connect(&workspace_, &WorkspaceController::snapshotAboutToChange, this, [this] { beginResetModel(); });
  connect(&workspace_, &WorkspaceController::snapshotChanged, this, [this] { endResetModel(); });
}

int AssetTableModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(workspace_.snapshot().assets().size());
}

int AssetTableModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : 2;
}

QVariant AssetTableModel::data(const QModelIndex& index, int role) const {
  const auto& snapshot = workspace_.snapshot();
  if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= snapshot.assets().size()) {
    return {};
  }
  const auto& asset = snapshot.assets()[static_cast<size_t>(index.row())];
  const auto& meta = core::metadata(asset);
  if (role == IdRole) {
    return meta.id.value;
  }
  if (role == Qt::DecorationRole && index.column() == 0) {
    return assetIcon(asset);
  }
  if (role != Qt::DisplayRole) {
    return {};
  }
  if (index.column() == 0) {
    return QString::fromStdString(meta.name);
  }
  if (index.column() == 1) {
    return QString::fromStdString(meta.format);
  }
  return {};
}

QVariant AssetTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }
  return horizontalHeader(section, role, std::array{"Name", "Format"});
}

CollectionTableModel::CollectionTableModel(WorkspaceController& workspace, QObject* parent)
    : QAbstractTableModel(parent), workspace_(workspace) {
  connect(&workspace_, &WorkspaceController::snapshotAboutToChange, this, [this] { beginResetModel(); });
  connect(&workspace_, &WorkspaceController::snapshotChanged, this, [this] { endResetModel(); });
}

void CollectionTableModel::setPlayingCollection(std::optional<core::CollectionId> collection) {
  if (playingCollection_ == collection) {
    return;
  }
  playingCollection_ = collection;
  if (rowCount() != 0) {
    emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {Qt::DecorationRole});
  }
}

int CollectionTableModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(workspace_.snapshot().collections().size());
}

int CollectionTableModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : 1;
}

QVariant CollectionTableModel::data(const QModelIndex& index, int role) const {
  const auto& collections = workspace_.snapshot().collections();
  if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= collections.size()) {
    return {};
  }
  const auto& collection = collections[static_cast<size_t>(index.row())];
  if (role == IdRole) {
    return collection.id.value;
  }
  if (role == Qt::DecorationRole) {
    return playingCollection_ == collection.id ? playingCollectionIcon()
                                               : collectionIcon();
  }
  if (role == Qt::DisplayRole || role == Qt::EditRole) {
    return QString::fromStdString(collection.name);
  }
  return {};
}

QVariant CollectionTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }
  return horizontalHeader(section, role, std::array{"Name"});
}

CollectionFilterProxyModel::CollectionFilterProxyModel(
    WorkspaceController& workspace, QObject* parent)
    : QSortFilterProxyModel(parent), workspace_(workspace) {
  setFilterCaseSensitivity(Qt::CaseInsensitive);
  setFilterKeyColumn(0);
}

bool CollectionFilterProxyModel::filterAcceptsRow(
    int sourceRow, const QModelIndex& sourceParent) const {
  if (filterRegularExpression().pattern().isEmpty()) {
    return true;
  }

  if (QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent)) {
    return true;
  }
  const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
  const auto collectionId = core::CollectionId{index.data(IdRole).toUInt()};
  const auto* collection = workspace_.snapshot().collection(collectionId);
  if (collection == nullptr || !collection->sequence) {
    return false;
  }
  const auto* sequence = workspace_.snapshot().asset(*collection->sequence);
  return sequence != nullptr &&
      filterRegularExpression()
          .match(QString::fromStdString(core::metadata(*sequence).name))
          .hasMatch();
}

CollectionContentsModel::CollectionContentsModel(WorkspaceController& workspace, QObject* parent)
    : QAbstractTableModel(parent), workspace_(workspace) {
  connect(&workspace_, &WorkspaceController::snapshotAboutToChange, this, [this] {
    resetting_ = true;
    beginResetModel();
  });
  connect(&workspace_, &WorkspaceController::snapshotChanged, this, [this] {
    rebuild();
    endResetModel();
    resetting_ = false;
  });
}

void CollectionContentsModel::setCollection(std::optional<core::CollectionId> collection) {
  if (collection_ == collection) {
    return;
  }
  if (resetting_) {
    collection_ = collection;
    rebuild();
    return;
  }
  beginResetModel();
  collection_ = collection;
  rebuild();
  endResetModel();
}

int CollectionContentsModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() || !collection_ ? 0 : static_cast<int>(entries_.size() + 1);
}

int CollectionContentsModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : 1;
}

QVariant CollectionContentsModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || !collection_) {
    return {};
  }
  const auto* collection = workspace_.snapshot().collection(*collection_);
  if (collection == nullptr) {
    return {};
  }
  if (index.row() == 0) {
    if (role == IdRole) {
      return collection->id.value;
    }
    if (role == IsCollectionRole) {
      return true;
    }
    if (role == IsLastItemRole) {
      return entries_.empty();
    }
    if (role == Qt::DecorationRole) {
      return collectionIcon();
    }
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
      return QString::fromStdString(collection->name);
    }
    return {};
  }

  const size_t entryIndex = static_cast<size_t>(index.row() - 1);
  if (entryIndex >= entries_.size()) {
    return {};
  }
  const auto* asset = workspace_.snapshot().asset(entries_[entryIndex].asset);
  if (asset == nullptr) {
    return {};
  }
  const auto& meta = core::metadata(*asset);
  if (role == IdRole) {
    return meta.id.value;
  }
  if (role == IsCollectionRole) {
    return false;
  }
  if (role == IsLastItemRole) {
    return entryIndex + 1 == entries_.size();
  }
  if (role == Qt::DecorationRole) {
    return assetIcon(*asset);
  }
  if (role == Qt::DisplayRole) {
    return QString::fromStdString(meta.name);
  }
  return {};
}

QVariant CollectionContentsModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }
  return horizontalHeader(section, role, std::array{"Name"});
}

void CollectionContentsModel::rebuild() {
  entries_.clear();
  if (!collection_) {
    return;
  }
  const auto* collection = workspace_.snapshot().collection(*collection_);
  if (collection == nullptr) {
    collection_.reset();
    return;
  }
  for (const auto id : collection->miscAssets) {
    entries_.push_back({QStringLiteral("Miscellaneous"), id});
  }
  for (const auto id : collection->instrumentSets) {
    entries_.push_back({QStringLiteral("Instrument set"), id});
  }
  for (const auto id : collection->sampleCollections) {
    entries_.push_back({QStringLiteral("Sample collection"), id});
  }
  if (collection->sequence) {
    entries_.push_back({QStringLiteral("Sequence"), *collection->sequence});
  }
}

DiagnosticTableModel::DiagnosticTableModel(WorkspaceController& workspace, QObject* parent)
    : QAbstractTableModel(parent), workspace_(workspace) {
  connect(&workspace_, &WorkspaceController::snapshotAboutToChange, this, [this] { beginResetModel(); });
  connect(&workspace_, &WorkspaceController::snapshotChanged, this, [this] { endResetModel(); });
}

int DiagnosticTableModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(workspace_.snapshot().diagnostics().size());
}

int DiagnosticTableModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : 2;
}

QVariant DiagnosticTableModel::data(const QModelIndex& index, int role) const {
  const auto& snapshot = workspace_.snapshot();
  if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= snapshot.diagnostics().size()) {
    return {};
  }
  const auto& diagnostic = snapshot.diagnostics()[static_cast<size_t>(index.row())];
  if (role == Qt::ForegroundRole) {
    return severityColor(diagnostic.severity);
  }
  if (role != Qt::DisplayRole) {
    return {};
  }
  if (index.column() == 0) {
    return severityName(diagnostic.severity);
  }
  if (index.column() == 1) {
    return QString::fromStdString(diagnostic.message);
  }
  return {};
}

QVariant DiagnosticTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }
  return horizontalHeader(section, role, std::array{"Level", "Message"});
}

}  // namespace vgmtrans::ui
