/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/session/Session.h"

#include <QObject>

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace vgmtrans::ui {

struct OpenFailure {
  std::filesystem::path path;
  std::string message;
};

struct OpenResult {
  std::vector<std::filesystem::path> opened;
  std::vector<OpenFailure> failures;
};

// The only mutable boundary between Qt and the value core. Views consume an
// immutable snapshot and keep stable IDs; they never retain pointers into Session.
class WorkspaceController final : public QObject {
  Q_OBJECT

public:
  using SessionConfigurator = std::function<void(core::Session&)>;

  explicit WorkspaceController(QObject* parent = nullptr);
  explicit WorkspaceController(SessionConfigurator configure, QObject* parent = nullptr);

  [[nodiscard]] const core::SessionSnapshot& snapshot() const noexcept { return snapshot_; }
  [[nodiscard]] std::span<const u8> sourceBytes(core::SourceId id) const;

  [[nodiscard]] OpenResult openPaths(std::span<const std::filesystem::path> paths);
  [[nodiscard]] size_t removeSources(std::span<const core::SourceId> sources);
  [[nodiscard]] std::vector<core::Artifact> exportCollection(core::CollectionId id,
                                                             const core::ExportRequest& request) const;

signals:
  void snapshotAboutToChange();
  void snapshotChanged();

private:
  void publish(core::SessionSnapshot snapshot);

  core::Session session_;
  core::SessionSnapshot snapshot_;
};

}  // namespace vgmtrans::ui
