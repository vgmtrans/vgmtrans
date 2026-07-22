/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "application/WorkspaceController.h"

#include "value/formats/ValueFormats.h"

#include <exception>
#include <unordered_set>
#include <utility>

namespace vgmtrans::ui {

WorkspaceController::WorkspaceController(QObject* parent)
    : WorkspaceController([](core::Session& session) { formats::registerValueFormats(session); }, parent) {
}

WorkspaceController::WorkspaceController(SessionConfigurator configure, QObject* parent)
    : QObject(parent), snapshot_(session_.snapshot()) {
  configure(session_);
  snapshot_ = session_.snapshot();
}

std::span<const u8> WorkspaceController::sourceBytes(core::SourceId id) const {
  return session_.sources().bytes(id);
}

OpenResult WorkspaceController::openPaths(std::span<const std::filesystem::path> paths) {
  OpenResult result;
  for (const auto& path : paths) {
    try {
      session_.addSourceFromPath(path);
      result.opened.push_back(path);
    } catch (const std::exception& error) {
      result.failures.push_back(OpenFailure{
          .path = path,
          .message = error.what(),
      });
    }
  }

  if (!result.opened.empty()) {
    publish(session_.scanPendingSources());
  }
  return result;
}

size_t WorkspaceController::removeSources(std::span<const core::SourceId> sources) {
  std::unordered_set<u32> selected;
  selected.reserve(sources.size());
  for (const auto id : sources) {
    if (id.valid()) {
      selected.insert(id.value);
    }
  }

  std::vector<core::SourceId> roots;
  roots.reserve(selected.size());
  for (const u32 value : selected) {
    const core::SourceId id{value};
    const auto* source = snapshot_.source(id);
    if (source == nullptr) {
      continue;
    }

    bool selectedAncestor = false;
    auto parent = source->parent;
    while (parent) {
      if (selected.contains(parent->value)) {
        selectedAncestor = true;
        break;
      }
      const auto* parentSource = snapshot_.source(*parent);
      parent = parentSource != nullptr ? parentSource->parent : std::nullopt;
    }
    if (!selectedAncestor) {
      roots.push_back(id);
    }
  }

  size_t removed = 0;
  for (const auto id : roots) {
    if (session_.sources().contains(id)) {
      static_cast<void>(session_.removeSource(id));
      ++removed;
    }
  }
  if (removed != 0) {
    publish(session_.snapshot());
  }
  return removed;
}

size_t WorkspaceController::removeAssets(std::span<const core::AssetId> assets) {
  std::unordered_set<u32> selected;
  selected.reserve(assets.size());
  for (const auto id : assets) {
    if (id.valid() && snapshot_.asset(id) != nullptr) {
      selected.insert(id.value);
    }
  }
  if (selected.empty()) {
    return 0;
  }

  std::vector<core::AssetId> existing;
  existing.reserve(selected.size());
  for (const u32 value : selected) {
    existing.push_back(core::AssetId{value});
  }
  publish(session_.removeAssets(existing));
  return existing.size();
}

std::vector<core::Artifact> WorkspaceController::exportCollection(core::CollectionId id,
                                                                  const core::ExportRequest& request) const {
  return session_.exportCollection(id, request);
}

void WorkspaceController::publish(core::SessionSnapshot snapshot) {
  emit snapshotAboutToChange();
  snapshot_ = std::move(snapshot);
  emit snapshotChanged();
}

}  // namespace vgmtrans::ui
