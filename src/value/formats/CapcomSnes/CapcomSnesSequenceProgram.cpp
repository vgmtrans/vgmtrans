/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesSequenceProgram.h"

#include "value/formats/CapcomSnes/CapcomSnesSequenceDialect.h"

#include <fmt/format.h>

#include <optional>
#include <string>
#include <utility>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

SequenceProgramAsset parseCapcomSnesSequenceProgram(const ScanInput& input, const CapcomSnesLayout& layout,
                                                    AssetId sequenceId, std::optional<AssetId> instrumentSetId,
                                                    std::string_view displayName) {
  const u32 headerSize = (layout.priorityInHeader ? 1 : 0) + kCapcomSnesMaxTracks * 2;
  ItemTree items;
  ItemTreeBuilder itemBuilder(items, input.ids);
  const auto root = itemBuilder.add(std::nullopt, ItemKind::Sequence, "capcom-snes.sequence-header", "Sequence Header",
                                    input.reader.range(layout.sequenceHeaderAddress, headerSize));

  const SequenceDialect dialect = capcomSnesSequenceDialect(layout.version);
  SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .behavior = dialect.defaultBehavior,
  };

  const CommandHandler* programHandler = dialect.handlerForKind("capcom-snes.program");
  const u32 pointerBase = layout.sequenceHeaderAddress + (layout.priorityInHeader ? 1 : 0);
  // Capcom stores track pointers in reverse channel order. The normalized source track
  // number below matches the driver's playback order.
  for (int trackIndex = static_cast<int>(kCapcomSnesMaxTracks) - 1; trackIndex >= 0; --trackIndex) {
    const auto pointerOffset = pointerBase + static_cast<u32>(trackIndex) * 2;
    const u16 trackAddress = input.reader.be16(pointerOffset);
    if (trackAddress == 0) {
      continue;
    }

    const auto trackItem =
        itemBuilder.add(root, ItemKind::Track, "capcom-snes.track-pointer", "Track Pointer",
                        input.reader.range(pointerOffset, 2), fmt::format("Track starts at ${:04X}", trackAddress));
    auto track = decodeCapcomSnesSourceTrack(input.reader, dialect,
                                             static_cast<u32>(kCapcomSnesMaxTracks - 1 - trackIndex), trackAddress);

    for (const auto& command : track.commands) {
      static_cast<void>(addSourceCommandItem(itemBuilder, trackItem, dialect, track, command));
      if (programHandler != nullptr) {
        addBankedProgramReference(program, track, command, programHandler->kind, "raw", instrumentSetId);
      }
    }

    program.tracks.push_back(std::move(track));
  }

  return SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = sequenceId,
              .format = "CapcomSnes",
              .name = std::string(displayName),
              .range = input.reader.range(layout.sequenceHeaderAddress, headerSize),
              .items = std::move(items),
          },
      .program = std::move(program),
  };
}

}  // namespace vgmtrans::formats::capcom_snes
