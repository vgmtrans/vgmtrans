/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsSequenceProgram.h"

#include "value/formats/NDS/NdsSequenceDialect.h"

#include <fmt/format.h>

#include <string>
#include <utility>

namespace vgmtrans::formats::nds {

using namespace core;

SequenceProgramAsset parseNdsSequenceProgram(const ScanInput& input, AssetId id, NdsSequenceRange range,
                                             const std::string& name, std::optional<AssetId> instrumentSet) {
  const SequenceDialect dialect = ndsSequenceDialect();
  SequenceProgramAsset asset{
      .metadata =
          AssetMetadata{
              .id = id,
              .format = "NDS",
              .name = name,
              .range = input.reader.range(range.offset, range.size),
          },
      .program =
          SequenceProgram{
              .dialect = dialect.id,
              .timebase = dialect.timebase,
              .sourceBaseAddress = Address{range.offset + 0x1c},
              .behavior = dialect.defaultBehavior,
          },
  };

  const CommandHandler* programHandler = dialect.handlerForKind("nds.program");
  ItemTreeBuilder items(asset.metadata.items, input.ids);
  const auto root =
      items.add(std::nullopt, ItemKind::Sequence, "sseq", name, input.reader.range(range.offset, range.size));

  u32 trackIndex = 0;
  for (const u32 start : ndsSequenceTrackStarts(input.reader, range.offset, range.sequenceEnd)) {
    auto track = decodeNdsSequenceTrack(input.reader, dialect, range.offset, range.sequenceEnd, start, trackIndex++,
                                        range.linearizeMalformedControlFlow);
    const auto trackItem = items.add(root, ItemKind::Track, "track", fmt::format("Track {}", track.sourceTrackNumber),
                                     input.reader.range(start, 0));
    for (const auto& command : track.commands) {
      static_cast<void>(addSourceCommandItem(items, trackItem, dialect, track, command));
      if (programHandler != nullptr) {
        addBankedProgramReference(asset.program, track, command, programHandler->kind, "raw", instrumentSet);
      }
    }
    asset.program.tracks.push_back(std::move(track));
  }

  return asset;
}

}  // namespace vgmtrans::formats::nds
