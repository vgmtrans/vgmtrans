/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/extractors/MameRomSetExtractor.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/midi/PitchTransitionMidiLowering.h"
#include "value/formats/CPS/Cps.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/SampleDecoder.h"
#include "value/validation/ScanValidation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats;
using namespace vgmtrans::formats::cps;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void bytesAt(std::vector<u8>& bytes, size_t offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

void be16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value >> 8);
  bytes[offset + 1] = static_cast<u8>(value);
}

void le16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void be32(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>(value >> 24);
  bytes[offset + 1] = static_cast<u8>(value >> 16);
  bytes[offset + 2] = static_cast<u8>(value >> 8);
  bytes[offset + 3] = static_cast<u8>(value);
}

struct Fixture {
  SourceFile source;
  std::vector<u8> bytes;
};

Fixture cps1Fixture() {
  constexpr u32 programSize = 0x2000;
  constexpr u32 sampleSize = 0x800;
  std::vector<u8> bytes(programSize + sampleSize);
  bytesAt(bytes, 0x100, {1, 0x70});
  be16(bytes, 0x102, 0x200);
  be16(bytes, 0x104, 0x400);
  be16(bytes, 0x106, 0x500);

  // One V4.25 patch: -12 transpose, triangle LFO with reset-on-select,
  // algorithm 6/feedback 2, full operator mask, and four complete operators.
  bytesAt(bytes, 0x200, {0xf4, 0xc2, 0x44, 0x22, 0x11, 0x16, 0x52, 0x78});
  for (u32 op = 0; op < 4; ++op) {
    bytesAt(bytes, 0x208 + op * 3, {static_cast<u8>(op), 0, 0x7f});
    bytes[0x214 + op] = static_cast<u8>(0x11 + op);
    bytes[0x218 + op] = 0x5f;
    bytes[0x21c + op] = 0x9f;
    bytes[0x220 + op] = 0x5f;
    bytes[0x224 + op] = 0x8f;
  }

  bytesAt(bytes, 0x400, {0x80, 1, 0, 0});
  bytesAt(bytes, 0x500, {0});
  be16(bytes, 0x501, 0x600);
  bytesAt(bytes, 0x600, {0x08, 0x00, 0x06, 0x80, 0x07, 0x7f, 0x4d, 0x17});

  const u32 sampleBase = programSize;
  bytesAt(bytes, sampleBase + 8, {0x00, 0x04, 0x00, 0x00, 0x04, 0x10, 0, 0});
  bytesAt(bytes, sampleBase + 16, {0xff, 0xff, 0xff, 0xff});
  for (u32 offset = 0; offset < 0x10; ++offset) {
    bytes[sampleBase + 0x400 + offset] = static_cast<u8>(offset);
  }

  SourceFile source{
      .id = SourceId{50},
      .name = "cps1 fixture",
      .size = bytes.size(),
      .attributes =
          {
              {std::string(mame::kMameGameAttribute), "cps1-fixture"},
              {std::string(mame::kMameFormatAttribute), "CPS1"},
              {std::string(mame::kMameFormatVersionAttribute), "CPS1_V4.25"},
          },
      .segments =
          {
              SourceSegment{.name = "audiocpu", .offset = 0, .size = programSize, .attributes = {{"tables", "0x100"}}},
              SourceSegment{.name = "oki6295", .offset = programSize, .size = sampleSize},
          },
  };
  return Fixture{.source = std::move(source), .bytes = std::move(bytes)};
}

Fixture cps1V1Fixture() {
  std::vector<u8> bytes(0x800);
  bytesAt(bytes, 0x100, {1, 0, 0});
  le16(bytes, 0x103, 0x300);
  bytes[0x10c] = 1;

  bytes[0x300] = 0;
  le16(bytes, 0x301, 0x400);
  bytesAt(bytes, 0x400, {0x07, 65, 0x08, 0xfc, 0x5f, 0x0f});

  SourceFile source{
      .id = SourceId{53},
      .name = "cps1 v1 fixture",
      .size = bytes.size(),
      .attributes =
          {
              {std::string(mame::kMameGameAttribute), "cps1-v1-fixture"},
              {std::string(mame::kMameFormatAttribute), "CPS1"},
              {std::string(mame::kMameFormatVersionAttribute), "CPS1_V1.00"},
          },
      .segments =
          {
              SourceSegment{.name = "audiocpu", .offset = 0, .size = bytes.size(), .attributes = {{"tables", "0x100"}}},
          },
  };
  return Fixture{.source = std::move(source), .bytes = std::move(bytes)};
}

Fixture earlyCps2Fixture() {
  constexpr u32 programSize = 0x3000;
  constexpr u32 sampleSize = 0x200;
  std::vector<u8> bytes(programSize + sampleSize);

  le16(bytes, 0x180, 0x400);
  bytesAt(bytes, 0x200, {0, 0, 0, 0, 0, 0, 1, 0x3c});
  bytesAt(bytes, 0x300, {63, 32, 96, 16, 24});
  bytesAt(bytes, 0x400, {0, 0, 0, 0});

  be32(bytes, 0x1000, 0x101100);
  bytesAt(bytes, 0x1100, {0});
  be16(bytes, 0x1101, 0x21);
  bytesAt(bytes, 0x1121,
          {
              0x05,
              120,  // BPM
              0x1b,
              0x20,  // vibrato
              0x1c,
              0x30,  // tremolo
              0x1d,
              0x40,  // rate
              0x1e,
              1,  // restart on note
              0x1f,
              0,  // bank
              0x08,
              0,  // program
              0x18,
              16,  // QSound center keeps both channels at full gain
              0x18,
              0,  // hard-left balance
              0x06,
              0x80,  // duration
              0x4d,  // note
              0x17,
          });
  for (u32 offset = 0; offset < 0x100; ++offset) {
    bytes[programSize + offset] = static_cast<u8>(offset);
  }

  SourceFile source{
      .id = SourceId{51},
      .name = "cps2 fixture",
      .size = bytes.size(),
      .attributes =
          {
              {std::string(mame::kMameGameAttribute), "cps2-fixture"},
              {std::string(mame::kMameFormatAttribute), "CPS2"},
              {std::string(mame::kMameFormatVersionAttribute), "CPS2_V1.40"},
          },
      .segments =
          {
              SourceSegment{
                  .name = "audiocpu",
                  .offset = 0,
                  .size = programSize,
                  .attributes =
                      {
                          {"seq_table", "0x1000"},
                          {"samp_table", "0x200"},
                          {"samp_table_length", "8"},
                          {"instr_table_ptrs", "0x180"},
                          {"num_instr_banks", "1"},
                          {"artic_table", "0x300"},
                      },
              },
              SourceSegment{.name = "qsound", .offset = programSize, .size = sampleSize},
          },
  };
  return Fixture{.source = std::move(source), .bytes = std::move(bytes)};
}

Fixture earlyCps2ZeroRateSlurFixture() {
  auto fixture = earlyCps2Fixture();
  fixture.source.name = "cps2 zero-rate slur fixture";
  fixture.source.attributes[std::string(mame::kMameGameAttribute)] = "cps2-zero-rate-slur-fixture";
  fixture.source.attributes[std::string(mame::kMameFormatVersionAttribute)] = "CPS2_V1.31";
  bytesAt(fixture.bytes, 0x1121,
          {
              0x09,
              0x02,  // octave 2
              0x8b,  // A#1, independent attack
              0x04,
              0x68,  // octave bank 1 + tie
              0x51,  // E4, first tied note
              0x0d,
              0x00,  // instantaneous portamento
              0x52,  // F4
              0x53,  // F#4
              0x02,
              0x00,  // select the third duration table
              0x94,  // G4
              0x17,
          });
  return fixture;
}

Fixture lateCps2Fixture() {
  constexpr u32 programSize = 0x3000;
  constexpr u32 sampleSize = 0x200;
  std::vector<u8> bytes(programSize + sampleSize);

  le16(bytes, 0x180, 0x400);
  bytesAt(bytes, 0x200, {0, 0, 0, 0, 0, 0, 1, 0x3c});
  bytesAt(bytes, 0x300, {63, 32, 96, 16, 24});
  bytesAt(bytes, 0x400, {0, 0, 0, 0});

  be32(bytes, 0x1000, 0x1100);
  bytesAt(bytes, 0x1100, {0});
  be16(bytes, 0x1101, 0x21);
  bytesAt(bytes, 0x1121,
          {
              0xe1, 0x40,  // LFO rate
              0xc5, 0x20,  // vibrato
              0xe2, 0x30,  // tremolo
              0xc6, 64,    // nonlinear QSound volume
              0xc8, 1,     // QSound expression lookup
              0xc7, 126,   // quantized one step from hard right
              0xdd, 2,     // additive transpose
              0xdc, 5,     // base transpose
              0xbf, 0x3c, 1, 0xff,
          });

  SourceFile source{
      .id = SourceId{54},
      .name = "late cps2 fixture",
      .size = bytes.size(),
      .attributes =
          {
              {std::string(mame::kMameGameAttribute), "late-cps2-fixture"},
              {std::string(mame::kMameFormatAttribute), "CPS2"},
              {std::string(mame::kMameFormatVersionAttribute), "CPS2_V2.10"},
          },
      .segments =
          {
              SourceSegment{
                  .name = "audiocpu",
                  .offset = 0,
                  .size = programSize,
                  .attributes =
                      {
                          {"seq_table", "0x1000"},
                          {"samp_table", "0x200"},
                          {"samp_table_length", "8"},
                          {"instr_table_ptrs", "0x180"},
                          {"num_instr_banks", "1"},
                          {"artic_table", "0x300"},
                      },
              },
              SourceSegment{.name = "qsound", .offset = programSize, .size = sampleSize},
          },
  };
  return Fixture{.source = std::move(source), .bytes = std::move(bytes)};
}

Fixture cps3Fixture() {
  constexpr u32 programSize = 0x1000;
  constexpr u32 sampleSize = 0x200;
  std::vector<u8> bytes(programSize + sampleSize);

  be32(bytes, 0x100, 0x06000300);
  be32(bytes, 0x200, 0);
  be32(bytes, 0x204, 0x20);
  be32(bytes, 0x208, 0x100);
  be32(bytes, 0x20c, 60);
  be16(bytes, 0x300, 0x100);
  bytesAt(bytes, 0x400, {127, 0xff, 64, 0, 0, 0, 32, 63, 32, 96, 16, 24});
  bytesAt(bytes, 0x40c, {0xff, 0});

  be32(bytes, 0x800, 0x108);
  bytesAt(bytes, 0x900, {0});
  be16(bytes, 0x901, 0x21);
  bytesAt(bytes, 0x921,
          {
              0xe1, 0x40,                      // LFO rate
              0xc5, 0x20,                      // vibrato
              0xe2, 0x30,                      // bipolar linear-gain tremolo
              0xe0, 1,                         // restart LFO on a fresh attack
              0xc2, 0,    0xc4, 0,             // bank/program
              0xdd, 3,    0xdc, 2,  0xe7, 64,  // set replaces add; neutral fine tune
              0xc7, 0,                         // hard-left QSound balance
              0xbf, 0x3c, 4,                   // fresh note
              5,                               // delay prefix
              0xbf, 0xbc, 4,                   // fresh note which establishes hold
              2,                               // delay prefix
              0xbf, 0x3e, 4,                   // legato continuation
              3,                               // delay prefix
              0xe8, 2,    9,                   // game-facing meta event
              0xc6, 100,  0xc8, 64,            // volume/expression remain separate
              0xdf, 1,    0xde, 64, 0xdf, 0,   // set replaces add; wrapped adjustment reaches silence
              0xff,
          });
  for (u32 offset = 0; offset < 0x100; ++offset) {
    bytes[programSize + offset] = static_cast<u8>(offset);
  }

  SourceFile source{
      .id = SourceId{52},
      .name = "cps3 fixture",
      .size = bytes.size(),
      .attributes =
          {
              {std::string(mame::kMameGameAttribute), "cps3-fixture"},
              {std::string(mame::kMameFormatAttribute), "CPS2"},
              {std::string(mame::kMameFormatVersionAttribute), "CPS3"},
          },
      .segments =
          {
              SourceSegment{
                  .name = "audiocpu",
                  .offset = 0,
                  .size = programSize,
                  .attributes =
                      {
                          {"seq_table", "0x800"},
                          {"samp_table", "0x200"},
                          {"samp_table_length", "16"},
                          {"instr_table_ptrs", "0x100"},
                          {"num_instr_banks", "1"},
                      },
              },
              SourceSegment{.name = "qsound", .offset = programSize, .size = sampleSize},
          },
  };
  return Fixture{.source = std::move(source), .bytes = std::move(bytes)};
}

Fixture cps3HeldPitchChainFixture() {
  auto fixture = cps3Fixture();
  bytesAt(fixture.bytes, 0x921,
          {
              0xad, 0x53, 0x0c, 0x0c,  // attack 83, wait 12
              0xad, 0xd6, 0x0c, 0x0c,  // attack 86 and hold, wait 12
              0xad, 0x5a, 0x06, 0x06,  // change held voice to 90, wait 6
              0xad, 0xdb, 0x06, 0x06,  // attack 91 and hold, wait 6
              0xad, 0xdd, 0x0c, 0x0c,  // change held voice to 93 and hold, wait 12
              0xd0, 0xd1,              // repeat slots begin, as in sfiii2 song 13
              0xa8, 0x5f, 0x82, 0x68,  // change held voice to 95 for 360 ticks
              0x02, 0x68,              // wait 360
              0xff,
          });
  return fixture;
}

Fixture cps3RepeatBreakFixture() {
  auto fixture = cps3Fixture();
  std::fill(fixture.bytes.begin() + 0x921, fixture.bytes.begin() + 0x940, 0);
  bytesAt(fixture.bytes, 0x921,
          {
              0xd0,  // repeat start
              0xbf,
              0x3c,
              1,
              1,  // A, wait
              0xd8,
              0x00,
              0x06,  // on the last pass, branch from 0x929 to 0x92f
              0xbf,
              0x3d,
              1,
              1,  // B, wait
              0xd4,
              1,  // repeat once
              0xbf,
              0x3e,
              1,
              0xff,  // C, end
          });
  return fixture;
}

Fixture cps3PracticalLoopFixture() {
  auto fixture = cps3Fixture();
  std::fill(fixture.bytes.begin() + 0x921, fixture.bytes.begin() + 0x940, 0);
  bytesAt(fixture.bytes, 0x921,
          {
              0xd0,  // repeat start
              0xbf,
              0x3c,
              1,
              1,  // note and wait
              0xd4,
              0x7e,  // 127 total passes
              0xff,
          });
  return fixture;
}

Fixture lateCps2SignedRepeatBreakFixture() {
  auto fixture = lateCps2Fixture();
  bytesAt(fixture.bytes, 0x1121,
          {
              0xd8,
              0xff,
              0xfd,  // -3 from the end of this command, back to itself
              0xff,
          });
  return fixture;
}

Fixture cps3ByteSignedBranchFixture() {
  auto fixture = cps3Fixture();
  bytesAt(fixture.bytes, 0x921,
          {
              0xcd,
              0x00,
              0xfd,  // CPS3 sign-extends this low byte, branching back to CD
              0xff,
          });
  return fixture;
}

ScanResult scan(const Fixture& fixture) {
  SourceStore sources;
  const SourceId source = sources.add(fixture.source, fixture.bytes);
  ScanIdAllocator ids;
  auto result = cpsDefinition().module.scan(ScanInput{
      .source = sources.source(source),
      .reader = sources.reader(source),
      .ids = ids,
  });
  const auto validation = validateScanResult(source, result, sources, {});
  expect(validation.empty(), validation.empty() ? "CPS fixture scan should pass admission validation"
                                                : validation.diagnostics().front().message);
  return result;
}

template <class AssetType>
std::vector<const AssetType*> assets(const ScanResult& result) {
  std::vector<const AssetType*> found;
  for (const auto& asset : result.assets) {
    if (const auto* typed = std::get_if<AssetType>(&asset)) {
      found.push_back(typed);
    }
  }
  return found;
}

const InstrumentSetAsset* instrumentDomain(const ScanResult& result, std::string_view domain) {
  for (const auto* set : assets<InstrumentSetAsset>(result)) {
    if (!set->instruments.empty() && set->instruments.front().identity &&
        set->instruments.front().identity->domain == domain) {
      return set;
    }
  }
  return nullptr;
}

const SequenceProgramAsset& onlySequence(const ScanResult& result) {
  const auto sequences = assets<SequenceProgramAsset>(result);
  expect(sequences.size() == 1, "fixture should produce exactly one sequence");
  return *sequences.front();
}

const MiscAsset& miscAt(const ScanResult& result, u64 offset, u64 size) {
  const auto miscAssets = assets<MiscAsset>(result);
  const auto found = std::ranges::find_if(miscAssets, [&](const MiscAsset* asset) {
    return asset->metadata.range.offset == offset && asset->metadata.range.size == size;
  });
  expect(found != miscAssets.end(), "fixture should contain the expected misc asset");
  return **found;
}

const SourceAnnotation& miscRoot(const ScanResult& result, const MiscAsset& misc, std::string_view kind) {
  const auto roots = result.sourceMap.ownedBy(ObjectRefs::misc(misc.metadata.id));
  expect(roots.size() == 1, "misc asset should have exactly one explicitly owned annotation root");
  const auto& root = result.sourceMap.get(roots.front());
  expect(root.role == SourceRole::Table && root.localKind == kind && root.range == misc.metadata.range,
         "misc asset annotation root should describe its complete typed table");
  return root;
}

const SourceAnnotation& onlyChild(const ScanResult& result, const SourceAnnotation& parent) {
  const auto children = result.sourceMap.childrenOf(parent.id);
  expect(children.size() == 1, "fixture table should contain exactly one meaningful annotated entry");
  const auto& child = result.sourceMap.get(children.front());
  expect(child.parent == parent.id && result.sourceMap.assetOwner(child.id) == result.sourceMap.assetOwner(parent.id),
         "misc table entry should inherit ownership from its table root");
  return child;
}

bool fieldMatches(const SourceAnnotation& annotation, std::string_view name, u64 offset, u64 size, u64 value) {
  const auto field = std::ranges::find_if(annotation.fields, [&](const SourceField& candidate) {
    return candidate.name == name && candidate.range.offset == offset && candidate.range.size == size;
  });
  if (field == annotation.fields.end()) {
    return false;
  }
  const auto* typed = std::get_if<u64>(&field->value);
  return typed != nullptr && *typed == value;
}

bool pointsTo(const SourceAnnotation& annotation, u64 offset) {
  return std::ranges::any_of(annotation.links, [&](const SourceLink& link) {
    const auto* range = std::get_if<SourceRange>(&link.target);
    return link.role == SourceLinkRole::PointsTo && range != nullptr && range->offset == offset;
  });
}

}  // namespace

void cps3MameDecryptionUsesDriverAddressMask() {
  mame::RomGroupDefinition group{
      .name = "audiocpu",
      .loadMethod = mame::RomLoadMethod::Append,
      .encryption = "cps3",
      .attributes = {{"key1", "0x02203ee3"}, {"key2", "0x01301972"}},
  };
  const std::vector<u8> plain{0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
  const auto encoded = mame::assembleRomGroup(group, {plain});
  expect(encoded == std::vector<u8>({0xa9, 0x8b, 0xed, 0xc7, 0x73, 0xe0, 0x37, 0xac}),
         "CPS3 decrypt should XOR the MAME address-dependent mask in CPU byte order");
  expect(mame::assembleRomGroup(group, {encoded}) == plain,
         "CPS3 XOR decryption should be reversible at the same mapped addresses");
}

void kabukiMameDecryptionUsesDataAddressPath() {
  mame::RomGroupDefinition group{
      .name = "audiocpu",
      .loadMethod = mame::RomLoadMethod::Append,
      .encryption = "kabuki",
      .attributes =
          {
              {"kabuki_swap_key1", "0x76543210"},
              {"kabuki_swap_key2", "0x24601357"},
              {"kabuki_addr_key", "0x4343"},
              {"kabuki_xor_key", "0x43"},
          },
  };
  std::vector<u8> encrypted(0x8000);
  bytesAt(encrypted, 0, {0x00, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xff});
  const auto decrypted = mame::assembleRomGroup(group, {encrypted});
  expect(std::ranges::equal(std::span(decrypted).first<8>(),
                            std::array<u8, 8>{0x07, 0x01, 0xa5, 0x29, 0xa4, 0xab, 0xed, 0xf8}),
         "Kabuki ROM assembly should retain the driver's data-byte decryption rather than its opcode path");
}

void cps1ModuleRetainsYm2151AndOkiDomains() {
  const auto fixture = cps1Fixture();
  const auto result = scan(fixture);
  expect(result.diagnostics.empty(), "complete CPS1 fixture should scan without diagnostics");
  expect(result.assets.size() == 5 && result.explicitCollections.size() == 1 &&
             result.explicitCollections[0].miscAssets.size() == 1 &&
             assets<MiscAsset>(result).front()->metadata.range.offset == 0x106,
         "CPS1 should publish and collect its sequence table alongside sequence, synth, and sample assets");

  const auto* ym = instrumentDomain(result, kCps1Ym2151Domain);
  const auto* oki = instrumentDomain(result, kCps1OkiDomain);
  const auto sampleSets = assets<SampleCollectionAsset>(result);
  expect(ym != nullptr && ym->instruments.size() == 1 && oki != nullptr && oki->instruments.size() == 1,
         "CPS1 should keep FM and sample playback as separate instrument identity domains");
  expect(sampleSets.size() == 1 && sampleSets[0]->samples.samples.size() == 127 &&
             sampleSets[0]->samples.samples[0].codec == AudioCodec::OkiAdpcm &&
             sampleSets[0]->samples.samples[0].encodedData.offset == 0x2400,
         "CPS1 OKI directory addresses should resolve relative to the sample ROM segment");
  const auto decodedOki = decodeSample(sampleSets[0]->samples.samples[0], fixture.bytes);
  const auto decodedEmpty = decodeSample(sampleSets[0]->samples.samples[1], fixture.bytes);
  expect(decodedOki && decodedOki->sampleRate == 7576 && decodedOki->pcm.size() == 32 &&
             std::ranges::any_of(decodedOki->pcm, [](s16 frame) { return frame != 0; }) && decodedEmpty &&
             decodedEmpty->pcm.size() == 8 &&
             std::ranges::all_of(decodedEmpty->pcm, [](s16 frame) { return frame == 0; }),
         "CPS1 OKI ADPCM and structural empty directory slots should both decode through the shared sample path");

  const auto& sequenceTable = miscAt(result, 0x106, 2);
  const auto& sequenceTableRoot = miscRoot(result, sequenceTable, "cps-sequence-pointer-table");
  const auto& sequencePointer = onlyChild(result, sequenceTableRoot);
  expect(sequencePointer.role == SourceRole::Pointer && sequencePointer.localKind == "cps-sequence-pointer" &&
             sequencePointer.range.offset == 0x106 && sequencePointer.range.size == 2 &&
             fieldMatches(sequencePointer, "encoded_pointer", 0x106, 2, 0x500) && pointsTo(sequencePointer, 0x500),
         "CPS1 misc sequence table should annotate its big-endian pointer and resolved target");

  const auto* voice =
      ym->instruments[0].synthVoice ? std::get_if<Ym2151Voice>(&*ym->instruments[0].synthVoice) : nullptr;
  expect(voice != nullptr && voice->algorithm == 6 && voice->feedback == 2 && voice->operatorMask == 0x0f &&
             voice->lfoEnabled && voice->resetLfoOnSelect && voice->lfo.waveform == Ym2151LfoWaveform::Triangle &&
             voice->operators[0].attackRate == 31,
         "CPS1 patch bytes should become a typed canonical YM2151 voice");

  const auto& sequence = onlySequence(result);
  expect(sequence.program.dialect.value == kCpsEarlyDialectId && sequence.program.tracks.size() == 1,
         "CPS1 V4.25 should use the shared early interpreter");
  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence.program, cpsEarlyDialect());
  const auto notes =
      std::count_if(performance.tracks[0].events.begin(), performance.tracks[0].events.end(),
                    [](const PerformanceEvent& event) { return std::holds_alternative<NotePerformanceEvent>(event); });
  expect(notes == 1, "CPS1 typed FM sequence should render without requiring a sampled preview voice");
}

void cps1V1DefaultsAndPitchWrappingMatchLegacyDriver() {
  const auto result = scan(cps1V1Fixture());
  expect(result.diagnostics.empty(), "complete CPS1 V1 fixture should scan without diagnostics");
  const auto& sequenceTable = miscAt(result, 0x103, 2);
  const auto& sequencePointer = onlyChild(result, miscRoot(result, sequenceTable, "cps-sequence-pointer-table"));
  expect(fieldMatches(sequencePointer, "encoded_pointer", 0x103, 2, 0x300) && pointsTo(sequencePointer, 0x300),
         "CPS1 V1 misc sequence table should decode its little-endian pointers");

  const auto& sequence = onlySequence(result);
  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence.program, cps1V1Dialect());
  const auto note = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  const auto* typed = note == performance.tracks[0].events.end() ? nullptr : std::get_if<NotePerformanceEvent>(&*note);
  const auto bend = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<PitchBendPerformanceEvent>(event);
  });
  const auto* typedBend =
      bend == performance.tracks[0].events.end() ? nullptr : std::get_if<PitchBendPerformanceEvent>(&*bend);
  expect(typed != nullptr && typed->key == 108.0 && typed->durationTicks == 1,
         "CPS1 V1 should wrap its pre-offset key at 96 and initialize note duration to one driver tick");
  expect(typedBend != nullptr && std::abs(typedBend->semitones + 4.0 / 256.0) < 0.0001,
         "CPS1 V1 tuning should retain the driver's signed 8.8 fixed-point pitch units");
}

void cps2EarlyModuleUsesPhysicalModulation() {
  const auto result = scan(earlyCps2Fixture());
  expect(result.diagnostics.empty(), "complete early CPS2 fixture should scan without diagnostics");
  expect(assets<MiscAsset>(result).size() == 3 && result.explicitCollections[0].miscAssets.size() == 3,
         "early CPS2 should expose sequence, sample-info, and articulation tables as misc assets");
  const auto* instruments = instrumentDomain(result, kCpsQSoundDomain);
  expect(instruments != nullptr && instruments->instruments.size() == 256 &&
             instruments->instruments[0].regions.size() == 1,
         "early CPS2 should retain its fixed 256-entry first instrument bank");
  expect(std::abs(instruments->instruments[0].regions[0].unityKey - 60.0) < 0.0001,
         "little-endian QSound sample indexes should resolve to their sample unity key");

  const auto& sequenceTable = miscAt(result, 0x1000, 0x100);
  const auto& sequencePointer = onlyChild(result, miscRoot(result, sequenceTable, "cps-sequence-pointer-table"));
  expect(sequencePointer.role == SourceRole::Pointer &&
             fieldMatches(sequencePointer, "encoded_pointer", 0x1000, 4, 0x101100) && pointsTo(sequencePointer, 0x1100),
         "CPS2 misc sequence table should mask its driver flag while preserving the encoded pointer");

  const auto& sampleTable = miscAt(result, 0x200, 8);
  const auto& sampleInfo = onlyChild(result, miscRoot(result, sampleTable, "cps-qsound-sample-info-table"));
  expect(sampleInfo.role == SourceRole::TableEntry && sampleInfo.localKind == "cps-qsound-sample-info" &&
             sampleInfo.fieldsAsChildren && fieldMatches(sampleInfo, "bank", 0x200, 1, 0) &&
             fieldMatches(sampleInfo, "start_offset", 0x201, 2, 0) &&
             fieldMatches(sampleInfo, "loop_offset", 0x203, 2, 0) &&
             fieldMatches(sampleInfo, "end_offset", 0x205, 2, 0x100) &&
             fieldMatches(sampleInfo, "unity_key", 0x207, 1, 60),
         "CPS2 sample-info misc asset should expose each raw driver field as an inspector child");

  const auto& articulationTable = miscAt(result, 0x300, 0x800);
  const auto& articulation = onlyChild(result, miscRoot(result, articulationTable, "cps-qsound-articulation-table"));
  expect(articulation.role == SourceRole::TableEntry && articulation.localKind == "cps-qsound-articulation" &&
             articulation.fieldsAsChildren && fieldMatches(articulation, "attack_rate", 0x300, 1, 63) &&
             fieldMatches(articulation, "decay_rate", 0x301, 1, 32) &&
             fieldMatches(articulation, "sustain_level", 0x302, 1, 96) &&
             fieldMatches(articulation, "sustain_rate", 0x303, 1, 16) &&
             fieldMatches(articulation, "release_rate", 0x304, 1, 24) &&
             fieldMatches(articulation, "unknown", 0x305, 3, 0),
         "CPS2 articulation misc asset should expose meaningful rows and their exact fields");

  const auto& sequence = onlySequence(result);
  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence.program, cpsEarlyDialect());
  bool physicalVibrato = false;
  bool physicalTremolo = false;
  bool physicalPan = false;
  bool physicalCenter = false;
  bool markerWorkaround = false;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
      physicalVibrato |= modulation->pitchDepthSemitones.has_value() && modulation->phaseRunsAtZeroDepth;
      physicalTremolo |= modulation->volumeDepthLinearGain.has_value() && modulation->phaseRunsAtZeroDepth;
    } else if (const auto* balance = std::get_if<StereoBalancePerformanceEvent>(&event)) {
      physicalPan |= balance->leftGain == 1.0 && balance->rightGain == 0.0;
      physicalCenter |= balance->leftGain == 1.0 && balance->rightGain == 1.0;
    }
    markerWorkaround |= std::holds_alternative<MarkerPerformanceEvent>(event);
  }
  expect(performance.tracks[0].hasPhysicalModulation && physicalVibrato && physicalTremolo && physicalPan &&
             physicalCenter && !markerWorkaround,
         "early CPS2 modulation and balance should stay physical without legacy MIDI marker workarounds");
}

void cps2EarlyZeroRateSlursRemainLinked() {
  const auto result = scan(earlyCps2ZeroRateSlurFixture());
  expect(result.diagnostics.empty(), "mshvsf zero-rate slur fixture should scan without diagnostics");
  const auto& sequence = onlySequence(result);
  const auto& commands = sequence.program.tracks[0].commands;
  const auto firstNote = std::ranges::find_if(
      commands, [](const SourceCommand& command) { return command.semantic == SequenceSemantic::Note; });
  const auto* noteIndex = firstNote == commands.end() ? nullptr : semanticOperand(*firstNote, "note_index");
  expect(noteIndex != nullptr && noteIndex->display == SourceValueDisplay::Default &&
             semanticOperand(*firstNote, "note") == nullptr,
         "early CPS annotations should identify the encoded note index without presenting it as an absolute MIDI key");

  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence.program, cpsEarlyDialect());
  std::vector<const NotePerformanceEvent*> notes;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    }
  }
  const std::array<double, 5> expectedKeys{34, 64, 65, 66, 67};
  expect(notes.size() == expectedKeys.size(), "the mshvsf slur chain should retain all five source notes");
  for (size_t index = 0; index < expectedKeys.size(); ++index) {
    expect(notes[index]->key == expectedKeys[index], "early CPS octave state should resolve the slur chain's pitches");
  }

  expect(performance.tracks[0].automations.size() == 3,
         "each zero-rate tied key change should remain an explicit pitch transition");
  for (size_t index = 0; index < performance.tracks[0].automations.size(); ++index) {
    const auto* transition = pitchTransitionIntent(performance.tracks[0].automations[index]);
    expect(transition != nullptr && transition->note == notes[index + 2]->note &&
               transition->previousNote == std::optional{notes[index + 1]->note} &&
               transition->startKey == expectedKeys[index + 1] && transition->targetKey == expectedKeys[index + 2] &&
               transition->timing.timelineTicks == 0 &&
               transition->preferredRendering == PitchTransitionRenderingHint::Portamento,
           "zero-rate early CPS portamento should preserve immediate, attack-free voice linkage");
  }

  const auto verifyPitchBend = [&](ModulationConversionPolicy modulationPolicy) {
    const MidiSequence midi = renderMidiSequence(
        performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend}, modulationPolicy);
    std::vector<u8> attacks;
    bool hasPitchBend = false;
    bool hasPortamento = false;
    for (const auto& event : midi.tracks[0].events) {
      if (const auto* note = std::get_if<NoteDuration>(&event)) {
        attacks.push_back(note->key);
      } else {
        hasPitchBend |= std::holds_alternative<PitchBend>(event);
        hasPortamento |= std::holds_alternative<PortamentoControl>(event);
      }
    }
    expect(attacks == std::vector<u8>{34, 64} && hasPitchBend && !hasPortamento,
           "pitch-bend export and preview should sustain the E4 attack through the F4/F#4/G4 slur");
  };
  verifyPitchBend(ModulationConversionPolicy::SynthModulators);
  verifyPitchBend(ModulationConversionPolicy::SequenceEventSimulation);

  const MidiSequence portamento =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::Portamento});
  expect(std::ranges::count_if(
             portamento.tracks[0].events,
             [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); }) == 3 &&
             std::ranges::none_of(portamento.tracks[0].events,
                                  [](const MidiEvent& event) { return std::holds_alternative<PitchBend>(event); }),
         "native-portamento export should retain all three zero-rate target changes");
}

void cps2LateDriverSemanticsRemainProfileSpecific() {
  const auto result = scan(lateCps2Fixture());
  expect(result.diagnostics.empty(), result.diagnostics.empty()
                                         ? "complete late CPS2 fixture should scan without diagnostics"
                                         : result.diagnostics.front().message);
  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(onlySequence(result).program, cpsLateDialect());

  const NotePerformanceEvent* note = nullptr;
  bool initialExpression = false;
  bool sourceExpression = false;
  bool nonlinearVolume = false;
  bool quantizedPan = false;
  bool runningLfo = false;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* typed = std::get_if<NotePerformanceEvent>(&event)) {
      note = typed;
    } else if (const auto* expression = std::get_if<ExpressionPerformanceEvent>(&event)) {
      initialExpression |= !expression->header.sourceCommand.valid() && std::abs(expression->linearGain - 0.5) < 0.0001;
      sourceExpression |=
          expression->header.sourceCommand.valid() && std::abs(expression->linearGain - 6.0 / 512.0) < 0.0001;
    } else if (const auto* level = std::get_if<LevelPerformanceEvent>(&event)) {
      nonlinearVolume |= level->header.sourceCommand.valid() && std::abs(level->linearGain - 0x4c8 / 8191.0) < 0.0001;
    } else if (const auto* balance = std::get_if<StereoBalancePerformanceEvent>(&event)) {
      quantizedPan |= std::abs(balance->leftGain - 1.0 / 16.0) < 0.0001 && balance->rightGain == 1.0;
    } else if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
      runningLfo |= modulation->phaseRunsAtZeroDepth;
    }
  }
  expect(note != nullptr && note->key == 67.0 && initialExpression && sourceExpression && nonlinearVolume &&
             quantizedPan && runningLfo,
         "late CPS2 should retain its independent transpose fields, QSound gain tables, pan quantization, and "
         "free-running LFO");
}

void cps3ModuleDecodesDelayPrefixesLegatoAndRegions() {
  const auto result = scan(cps3Fixture());
  expect(result.diagnostics.empty(), "complete CPS3 fixture should scan without diagnostics");
  expect(assets<MiscAsset>(result).size() == 2 && result.explicitCollections[0].miscAssets.size() == 2,
         "CPS3 should expose its sequence and sample-info tables as misc assets");
  const auto* instruments = instrumentDomain(result, kCpsQSoundDomain);
  expect(instruments != nullptr && instruments->instruments.size() == 1 &&
             instruments->instruments[0].explicitAddress == InstrumentAddress{.bank = 0, .program = 0} &&
             instruments->instruments[0].regions.size() == 1,
         "CPS3 bank and relative instrument pointers should produce one variable-region instrument");
  const auto& region = instruments->instruments[0].regions[0];
  expect(region.keyRange.low == 0 && region.keyRange.high == 127 && std::abs(region.unityKey - 59.875) < 0.0001 &&
             std::abs(region.attenuationDb - 96.0) < 0.0001 && region.envelope.sustainAmplitude &&
             std::abs(*region.envelope.sustainAmplitude - 97.0 / 128.0) < 0.0001,
         "CPS3 regions should use driver units for tuning, wrapped gain adjustment, and sustain level");

  const auto& sequenceTable = miscAt(result, 0x800, 0x100);
  const auto& sequencePointer = onlyChild(result, miscRoot(result, sequenceTable, "cps-sequence-pointer-table"));
  expect(fieldMatches(sequencePointer, "encoded_pointer", 0x800, 4, 0x108) && pointsTo(sequencePointer, 0x900),
         "CPS3 misc sequence table should resolve its driver-relative pointer base");

  const auto& sampleTable = miscAt(result, 0x200, 16);
  const auto& sampleInfo = onlyChild(result, miscRoot(result, sampleTable, "cps-qsound-sample-info-table"));
  expect(sampleInfo.fieldsAsChildren && fieldMatches(sampleInfo, "start_address", 0x200, 4, 0) &&
             fieldMatches(sampleInfo, "loop_address", 0x204, 4, 0x20) &&
             fieldMatches(sampleInfo, "end_address", 0x208, 4, 0x100) &&
             fieldMatches(sampleInfo, "unity_key", 0x20c, 4, 60),
         "CPS3 sample-info misc asset should expose its big-endian 32-bit fields");

  const auto& sequence = onlySequence(result);
  expect(sequence.program.dialect.value == kCpsLateDialectId && sequence.program.tracks.size() == 1,
         "CPS3 should use the late interpreter");
  const auto& commands = sequence.program.tracks[0].commands;
  expect(std::ranges::any_of(commands,
                             [](const SourceCommand& command) {
                               return command.semantic == SequenceSemantic::Wait && command.encodedSize == 1;
                             }),
         "late low-bit bytes should decode as delay prefixes rather than rest opcodes");

  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence.program, cpsLateDialect());
  std::vector<const NotePerformanceEvent*> notes;
  bool linearTremolo = false;
  bool hardLeftBalance = false;
  bool initialExpression = false;
  bool wrappedAdjustmentSilence = false;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    } else if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
      linearTremolo |= modulation->volumeDepthLinearGain.has_value() && !modulation->phaseRunsAtZeroDepth;
    } else if (const auto* balance = std::get_if<StereoBalancePerformanceEvent>(&event)) {
      hardLeftBalance |= balance->leftGain == 1.0 && balance->rightGain == 0.0;
    } else if (const auto* expression = std::get_if<ExpressionPerformanceEvent>(&event)) {
      initialExpression |=
          !expression->header.sourceCommand.valid() && std::abs(expression->linearGain - 65.0 / 128.0) < 0.0001;
      wrappedAdjustmentSilence |= expression->header.sourceCommand.valid() && expression->linearGain == 0.0;
    }
  }
  expect(notes.size() == 3 && notes[0]->header.tick == 0 && notes[1]->header.tick == 5 && notes[2]->header.tick == 7 &&
             !notes[0]->extendsPrevious && !notes[1]->extendsPrevious && !notes[2]->extendsPrevious &&
             notes[0]->note != notes[1]->note && notes[1]->note != notes[2]->note,
         "CPS3 delay prefixes and held-note bit should produce the driver event timeline and legato transition");
  const auto* transition = performance.tracks[0].automations.size() == 1
                               ? pitchTransitionIntent(performance.tracks[0].automations.front())
                               : nullptr;
  expect(transition != nullptr && transition->note == notes[2]->note &&
             transition->previousNote == std::optional{notes[1]->note} && transition->startKey == 62.0 &&
             transition->targetKey == 64.0 && transition->timing.timelineTicks == 0 &&
             transition->preferredRendering == PitchTransitionRenderingHint::PitchBend,
         "a CPS3 held-note key change should retain its pitch and attack-free voice linkage");
  expect(notes[0]->key == 62.0 && notes[2]->key == 64.0 && notes[0]->restartsLfoPhase && !notes[2]->restartsLfoPhase &&
             linearTremolo && hardLeftBalance && initialExpression && wrappedAdjustmentSilence,
         "CPS3 tuning, LFO reset, exact balance, initial expression, and wrapped gain should remain physical");
}

void cps3HeldNotesRetargetOneVoiceWithoutLosingPitch() {
  const auto result = scan(cps3HeldPitchChainFixture());
  expect(result.diagnostics.empty(), "sfiii2 held-note regression fixture should scan without diagnostics");
  const auto& sequence = onlySequence(result);
  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence.program, cpsLateDialect());

  std::vector<const NotePerformanceEvent*> sourceNotes;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      sourceNotes.push_back(note);
    }
  }
  const std::array<u64, 6> expectedTicks{0, 12, 24, 30, 36, 48};
  const std::array<double, 6> expectedKeys{83, 86, 90, 91, 93, 95};
  const std::array<u32, 6> expectedDurations{12, 12, 6, 6, 12, 360};
  expect(sourceNotes.size() == expectedTicks.size(), "every CPS3 note command should survive target-neutral rendering");
  for (size_t index = 0; index < sourceNotes.size(); ++index) {
    expect(sourceNotes[index]->header.tick == expectedTicks[index] && sourceNotes[index]->key == expectedKeys[index] &&
               sourceNotes[index]->durationTicks == expectedDurations[index] && !sourceNotes[index]->extendsPrevious,
           "CPS3 held-note source events should preserve their own key, time, duration, and identity");
  }

  expect(performance.tracks[0].automations.size() == 3,
         "the three attack-free CPS3 key changes should remain explicit pitch transitions");
  const std::array<size_t, 3> destinations{2, 4, 5};
  const std::array<size_t, 3> predecessors{1, 3, 4};
  for (size_t index = 0; index < destinations.size(); ++index) {
    const auto* transition = pitchTransitionIntent(performance.tracks[0].automations[index]);
    expect(transition != nullptr && transition->note == sourceNotes[destinations[index]]->note &&
               transition->previousNote == std::optional{sourceNotes[predecessors[index]]->note} &&
               transition->startKey == expectedKeys[predecessors[index]] &&
               transition->targetKey == expectedKeys[destinations[index]] &&
               transition->preferredRendering == PitchTransitionRenderingHint::PitchBend,
           "each CPS3 held-note destination should continue the immediately preceding physical voice");
  }

  const MidiExportOptions bendOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend};
  const PerformanceSequence lowered = lowerMidiPerformanceAutomation(performance, bendOptions);
  const MidiSequence midi = renderMidiSequence(performance, bendOptions);
  const MidiSequence previewMidi =
      renderMidiSequence(performance, bendOptions, ModulationConversionPolicy::SequenceEventSimulation);
  std::vector<NoteDuration> attacks;
  for (const auto& event : midi.tracks[0].events) {
    if (const auto* note = std::get_if<NoteDuration>(&event)) {
      attacks.push_back(*note);
    }
  }
  expect(attacks.size() == 3 && attacks[0].tick == 0 && attacks[0].key == 83 && attacks[0].duration == 12 &&
             attacks[1].tick == 12 && attacks[1].key == 86 && attacks[1].duration == 18 && attacks[2].tick == 30 &&
             attacks[2].key == 91 && attacks[2].duration == 378,
         "MIDI lowering should retain three physical attacks while sustaining each held CPS3 voice");

  for (const auto* rendered : {&midi, &previewMidi}) {
    std::vector<std::pair<u64, u16>> midiRanges;
    std::vector<std::pair<u64, s16>> midiBends;
    for (const auto& event : rendered->tracks[0].events) {
      if (const auto* range = std::get_if<PitchBendRange>(&event); range != nullptr && range->tick >= 24) {
        midiRanges.emplace_back(range->tick, range->cents);
      } else if (const auto* bend = std::get_if<PitchBend>(&event)) {
        midiBends.emplace_back(bend->tick, bend->value);
      }
    }
    expect(midiRanges == std::vector<std::pair<u64, u16>>{{24, 400}},
           "the sfiii2 held voices should use one compatible four-semitone range");
    expect(midiBends == std::vector<std::pair<u64, s16>>{{24, 8191}, {30, 0}, {36, 4096}, {48, 8191}},
           "the sfiii2 pitch commands should produce actual bend-value changes in export and preview MIDI");
  }

  const auto hasBend = [&](u64 tick, double semitones) {
    return std::ranges::any_of(lowered.tracks[0].events, [&](const PerformanceEvent& event) {
      const auto* bend = std::get_if<PitchBendPerformanceEvent>(&event);
      return bend != nullptr && bend->header.tick == tick && std::abs(bend->semitones - semitones) < 0.000001;
    });
  };
  expect(hasBend(24, 4.0) && hasBend(36, 2.0) && hasBend(48, 4.0),
         "MIDI lowering should emit every attack-free CPS3 pitch change relative to its sounding attack");
}

void cpsLateRepeatBreakUsesEndOfCommandBase() {
  const auto result = scan(cps3RepeatBreakFixture());
  expect(result.diagnostics.empty(), "complete CPS3 repeat-break fixture should scan without diagnostics");
  const auto& sequence = onlySequence(result);
  const auto performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence.program, cpsLateDialect());
  std::vector<std::pair<u64, double>> notes;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.emplace_back(note->header.tick, note->key);
    }
  }
  expect(notes == std::vector<std::pair<u64, double>>{{0, 60}, {1, 61}, {2, 60}, {3, 62}},
         "late repeat breaks should resolve their forward displacement from the end of the encoded command");
}

void cps3TerminalMaxRepeatActsAsPracticalLoop() {
  const auto result = scan(cps3PracticalLoopFixture());
  expect(result.diagnostics.empty(), "CPS3 practical-loop fixture should scan without diagnostics");
  const auto& sequence = onlySequence(result);
  const auto& commands = sequence.program.tracks[0].commands;
  const auto repeat =
      std::ranges::find_if(commands, [](const SourceCommand& command) { return command.opcode == 0xd4; });
  expect(repeat != commands.end() && repeat->flow.unconditionalJump(),
         "a terminal CPS3 D4 7E repeat should decode through the declared-loop path");

  const auto performance = SequenceVm(SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 1})
                               .render(sequence.program, cpsLateDialect());
  const auto notes = std::ranges::count_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  expect(performance.diagnostics.empty() && performance.tracks[0].endTick == 2 && notes == 2,
         "one requested loop should replay a CPS3 practical loop once instead of expanding all 127 passes");
}

void cpsLateControlFlowOffsetsFollowEachDriver() {
  const auto cps2Result = scan(lateCps2SignedRepeatBreakFixture());
  const auto& cps2Commands = onlySequence(cps2Result).program.tracks[0].commands;
  const auto cps2Break =
      std::ranges::find_if(cps2Commands, [](const SourceCommand& command) { return command.address.value == 0x1121; });
  expect(cps2Break != cps2Commands.end() && cps2Break->flow.staticTargets.size() == 1 &&
             cps2Break->flow.staticTargets[0].value == 0x1121,
         "late CPS2 repeat breaks should sign-extend their 16-bit displacement");

  const auto cps3Result = scan(cps3ByteSignedBranchFixture());
  const auto& cps3Commands = onlySequence(cps3Result).program.tracks[0].commands;
  const auto cps3Branch =
      std::ranges::find_if(cps3Commands, [](const SourceCommand& command) { return command.address.value == 0x921; });
  expect(cps3Branch != cps3Commands.end() && cps3Branch->flow.staticTargets.size() == 1 &&
             cps3Branch->flow.staticTargets[0].value == 0x921,
         "CPS3 CD should preserve the driver's independent sign extension of its low displacement byte");
}
