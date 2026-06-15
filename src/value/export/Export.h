/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ExportTypes.h"

namespace vgmtrans::core {

class SequenceDialectRegistry;
struct SessionSnapshot;
class SourceStore;

[[nodiscard]] std::vector<Artifact> exportCollection(const SessionSnapshot& snapshot, const SourceStore& sources,
                                                     CollectionId collection, const ExportRequest& request,
                                                     const SequenceDialectRegistry& dialects);

[[nodiscard]] std::vector<CollectionExport> exportAllCollections(const SessionSnapshot& snapshot,
                                                                 const SourceStore& sources,
                                                                 const ExportRequest& request,
                                                                 const SequenceDialectRegistry& dialects);

}  // namespace vgmtrans::core
