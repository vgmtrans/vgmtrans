/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/ValueFormats.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace vgmtrans::tests {

struct ValueFormatCorpus {
  std::string_view format;
  std::optional<core::ExportRequest> exports;
  std::optional<std::filesystem::path> outputDirectory;
};

inline int scanValueFormatArchive(const std::filesystem::path& path, const ValueFormatCorpus& corpus) {
  using namespace core;

  Session session;
  formats::registerValueFormats(session);
  session.scanSource(session.addSourceFromPath(path));
  const SessionSnapshot snapshot = session.snapshot();

  const auto isRam = [](const SourceFile& source) { return source.derived() && source.name.ends_with(" - ram"); };
  const auto ramSources = static_cast<unsigned>(std::ranges::count_if(snapshot.sources(), isRam));
  std::vector<AssetId> sequences;
  unsigned renderFailures = 0;
  for (const Asset& asset : snapshot.assets()) {
    const auto* sequence = std::get_if<SequenceProgramAsset>(&asset);
    if (sequence == nullptr || metadata(asset).format != corpus.format) {
      continue;
    }
    sequences.push_back(metadata(asset).id);
    const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence->program);
    renderFailures += performance.tracks.empty() || !performance.diagnostics.empty();
  }

  unsigned exportFailures = 0;
  if (corpus.exports) {
    if (corpus.outputDirectory) {
      std::filesystem::create_directories(*corpus.outputDirectory);
    }
    for (const Collection& collection : snapshot.collections()) {
      if (!collection.members.sequence ||
          std::ranges::find(sequences, *collection.members.sequence) == sequences.end()) {
        continue;
      }
      const auto artifacts = session.exportCollection(collection.id, *corpus.exports);
      exportFailures += artifacts.size() != corpus.exports->kinds.size();
      for (const Artifact& artifact : artifacts) {
        exportFailures += artifact.bytes.empty() || !artifact.diagnostics.empty();
        if (corpus.outputDirectory) {
          std::ofstream output(*corpus.outputDirectory / artifact.filename, std::ios::binary);
          output.write(reinterpret_cast<const char*>(artifact.bytes.data()),
                       static_cast<std::streamsize>(artifact.bytes.size()));
          exportFailures += !output;
        }
      }
    }
  }

  std::cout << "sources " << snapshot.sources().size() << ", assets " << snapshot.assets().size() << ", collections "
            << snapshot.collections().size() << ", " << corpus.format << " sequences " << sequences.size() << '/'
            << ramSources << ", render failures " << renderFailures << ", export failures " << exportFailures << '\n';
  for (const Diagnostic& diagnostic : snapshot.diagnostics()) {
    std::cerr << "diagnostic: " << diagnostic.message << '\n';
  }
  return sequences.size() == ramSources && ramSources != 0 && renderFailures == 0 && exportFailures == 0 ? 0 : 2;
}

}  // namespace vgmtrans::tests
