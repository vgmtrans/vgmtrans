/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/formats/CapcomSnes/CapcomSnesLayout.h"
#include "value/sequence/SequenceDialect.h"
#include "value/sequence/bytecode/BytecodeTable.h"
#include "value/base/Source.h"
#include "value/scan/ScanTypes.h"
#include "value/formats/CapcomSnes/CapcomSnesTypes.h"

#include <optional>
#include <string_view>

namespace vgmtrans::formats::capcom_snes {

// Keep the bytecode table paired with the dialect whose handler IDs it stores.
struct CapcomSnesSequenceDescriptor {
  core::SequenceDialect dialect;
  core::BytecodeDispatchTable bytecode;
};

[[nodiscard]] const CapcomSnesSequenceDescriptor& capcomSnesSequenceDescriptor(CapcomSnesEngineVersion version);
[[nodiscard]] core::SequenceDialect capcomSnesSequenceDialect(CapcomSnesEngineVersion version);
void registerCapcomSnesSequenceDialects(core::SequenceDialectRegistry& registry);

[[nodiscard]] core::TrackProgram decodeCapcomSnesSourceTrack(core::ByteReader reader,
                                                             const CapcomSnesSequenceDescriptor& descriptor,
                                                             u32 sourceTrackNumber, u32 startAddress);

[[nodiscard]] core::SequenceProgramAsset parseCapcomSnesSequence(const core::ScanInput& input,
                                                                 const CapcomSnesLayout& layout,
                                                                 core::AssetId sequenceId,
                                                                 std::optional<core::AssetId> instrumentSetId,
                                                                 std::string_view displayName);

}  // namespace vgmtrans::formats::capcom_snes
