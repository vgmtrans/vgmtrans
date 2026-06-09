/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "core/ProjectSession.h"

#include <utility>

namespace vgmtrans::core {

SourceId ProjectSession::addSource(SourceFile file, std::vector<u8> bytes) {
  sources_.discardVirtualizedTail();
  project_ = {};
  return sources_.add(std::move(file), std::move(bytes));
}

Project ProjectSession::scan() {
  project_ = scanner_.scan(sources_, formats_);
  return project_;
}

std::vector<Artifact> ProjectSession::exportCollection(
    CollectionId id,
    const ExportRequest& request) const {
  return exporter_.exportCollection(project_, sources_, id, request, profiles_);
}

}  // namespace vgmtrans::core
