/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/WolfTeamSnes/WolfTeamSnes.h"
#include "value/formats/WolfTeamSnes/WolfTeamSnesGrammar.h"

#include "value/scan/BytePattern.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::wolf_team_snes {

using namespace core;
namespace {

constexpr auto kReadBgmHeader =
    makeMaskedBytePattern("\x8f\x00\x86\x8f\x02\x87\x8f\x00\xe0\xfa\x99\xe1\x60\x98\x00\xe1", "xxxxxxxxxxxxxx?x");
constexpr auto kReadBgmHeaderLoader =
    makeMaskedBytePattern("\x8f\x00\xe0\xfa\x99\xe1\x60\x98\x00\xe1\x8f\x0e\xe2\x8d\x23", "xxxxxxxx?xxxxxx");
constexpr auto kResolveScorePointer =
    makeMaskedBytePattern("\x8d\x01\xf7\x86\xc4\xe0\x8d\x02\xf7\x86\xc4\xe1\x8d\x05\xf7\x86"
                          "\x1c\x98\x00\xe1\x60\x84\xe0\xc4\xe0\x98\x00\xe1\x8d\x00\xf7\xe0"
                          "\x8d\x03\xd7\x86\x8d\x01\xf7\xe0\x68\xff\xf0\x06\x60\x84\x99\x60"
                          "\x88\x00",
                          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx?");
constexpr auto kReadScoreByte = makeMaskedBytePattern(
    "\x8d\x03\xf7\x86\xc4\xa2\x8d\x04\xf7\x86\xc4\xa3\x8d\x00\xf7\xa2\x10\x03", "xxxxxxxxxxxxxxxxxx");
constexpr auto kLoadInstrumentRemap =
    makeMaskedBytePattern("\x8d\x01\xf7\xa2\x8d\x07\xd7\x86\x68\x40\x90\x0a\x68\x00\x0d\x28"
                          "\x01\x8e\xb0\x02\xbc\xbc\x8f\x04\xe1\x1c\x1c\xc4\xe0\x8d\x00\xf7"
                          "\xe0\x8d\x0f\xd7\x86\x8d\x01\xf7\xe0\x8d\x08\xd7\x86\x8d\x02\xf7"
                          "\xe0\x8d\x09\xd7\x86\x8d\x03\xf7\xe0\x80\xa8\x40\x8d\x12\xd7\x86",
                          "xxxxxxxxxxxxx?xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
constexpr auto kLoadInstrumentDirectWithDelay =
    makeMaskedBytePattern("\x8d\x01\xf7\x00\x8d\x06\xd7\x00\x8d\x02\xf7\x00\x8d\x07\xd7\x00"
                          "\x8f\x04\xe1\x1c\x1c\xc4\xe0",
                          "xxx?xxx?xxx?xxx?xxxxxxx");
constexpr auto kLoadInstrumentDirectNoDelay =
    makeMaskedBytePattern("\x8d\x01\xf7\x00\x8d\x07\xd7\x00\x8f\x04\xe1\x1c\x1c\xc4\xe0"
                          "\x8d\x00\xf7\xe0\x8d\x0f\xd7\x00\x8d\x01\xf7\xe0\x8d\x08\xd7\x00"
                          "\x8d\x02\xf7\xe0\x8d\x09\xd7\x00\x8d\x03\xf7\xe0\x80\xa8\x40"
                          "\x8d\x12\xd7\x00",
                          "xxx?xxx?"
                          "xxxxxxx"
                          "xxxxxxx?"
                          "xxxxxxx?"
                          "xxxxxxx?"
                          "xxxxxxx"
                          "xxx?");
constexpr auto kArcusSetDir = makeMaskedBytePattern("\x8d\x5d\xe8\x04\xcb\xf2\xc4\xf3", "xxxxxxxx");
constexpr auto kLateHeaderBaseAdd = makeMaskedBytePattern("\xfa\x99\xe1\x60\x98\x00\xe1", "xxxxx?x");
constexpr auto kLateResolvePointerBaseAdd =
    makeMaskedBytePattern("\x68\xff\xf0\x06\x60\x84\x99\x60\x88\x00", "xxxxxxxxx?");
constexpr auto kLateReadScoreByteGeneric = makeMaskedBytePattern(
    "\x8d\x03\xf7\x00\xc4\x00\x8d\x04\xf7\x00\xc4\x00\x8d\x00\xf7\x00\x10\x03", "xxx?x?xxx?x?xxx?xx");
constexpr auto kMiddleHighCommandDispatch = makeMaskedBytePattern("\x80\xa8\xe0\x1c\x5d\x1f\x00\x00", "xxxxxx??");
constexpr auto kMiddleSetHeaderBank = makeMaskedBytePattern("\x8f\x00\xad", "x?x");

constexpr u32 kCurrentSongPageAddress = 0x99;
constexpr u32 kLateSlotBase = 0x0200;
constexpr u32 kLateSlotSize = 0x20;
constexpr u32 kLateChannelCount = 14;
constexpr u32 kDescriptorOffset = 0x23;
constexpr u32 kLateHeaderLength = kDescriptorOffset + kLateChannelCount * 3;
constexpr u32 kTableSearchLimit = 0x80;
constexpr u16 kLateVolumeTable = 0x0140;
constexpr u16 kLateInstrumentTable = 0x0400;
constexpr u16 kLateSampleDir = 0xff00;

constexpr u16 kArcusHeader = 0x1800;
constexpr u8 kArcusRelocationPage = 0x18;
constexpr u32 kArcusSlotBase = 0;
constexpr u32 kArcusSlotSize = 0x10;
constexpr u32 kArcusChannelCount = 11;
constexpr u32 kArcusHeaderLength = kDescriptorOffset + kArcusChannelCount * 3;
constexpr u16 kSegmentedSampleDir = 0x0400;
constexpr u16 kArcusPatchMap = 0x1800;
constexpr u16 kSegmentedPatchTable = 0x0500;

constexpr u32 kMiddleBankPageAddress = 0x00ad;
constexpr u32 kMiddleSlotBase = 0x0200;
constexpr u32 kMiddleSlotSize = 0x20;
constexpr u32 kMiddleChannelCount = 14;
constexpr u32 kMiddleHeaderLength = kDescriptorOffset + kMiddleChannelCount * 3;
constexpr u32 kAceONeraeCommandTable = 0x1fe6;
constexpr u32 kDarkKingdomCommandTable = 0x2035;

struct HeaderProfile {
  u16 headerAddress = 0;
  u8 relocationPage = 0;
  u32 slotBase = 0;
  u32 slotSize = 0;
  u32 channelCount = 0;
  u8 activeMask = 0;
  u32 headerLength = 0;
};

struct HeaderCandidate {
  u16 headerAddress = 0;
  u8 relocationPage = 0;
  std::vector<ChannelLayout> channels;
  int score = 0;
  int activeChannels = 0;
  int validChannels = 0;
};

struct LateProfile {
  Variant variant = Variant::LateFamily;
  u8 sequenceBaseAdd = 0;
  LateTraits traits;
  bool instrumentModelConfirmed = false;
  bool searchAllHeaderBanks = false;
};

struct MiddleProfile {
  Variant variant = Variant::Middle;
  u32 commandTableAddress = 0;
  std::vector<u8> bankPages;
};

struct PatternImmediate {
  const MaskedBytePattern* pattern = nullptr;
  u32 offset = 0;
};

[[nodiscard]] u16 relocatedAddress(u8 low, u8 relativeHigh, u8 basePage) {
  return static_cast<u16>(low | (static_cast<u16>(static_cast<u8>(relativeHigh + basePage)) << 8));
}

void appendUnique(std::vector<u8>& values, u8 value) {
  if (std::ranges::find(values, value) == values.end()) {
    values.push_back(value);
  }
}

[[nodiscard]] std::vector<u8> patternImmediates(ByteReader reader, std::initializer_list<PatternImmediate> patterns) {
  std::vector<u8> values;
  for (const auto& spec : patterns) {
    u32 begin = 0;
    while (const auto found = findBytePattern(reader, *spec.pattern, begin)) {
      if (reader.has(*found + spec.offset, 1)) {
        appendUnique(values, reader.u8At(*found + spec.offset));
      }
      if (*found == std::numeric_limits<u32>::max()) {
        break;
      }
      begin = *found + 1;
    }
  }
  return values;
}

[[nodiscard]] std::optional<u8> firstPatternImmediate(ByteReader reader, const MaskedBytePattern& pattern, u32 offset) {
  const auto found = findBytePattern(reader, pattern);
  return found ? std::optional<u8>{reader.u8At(*found + offset)} : std::nullopt;
}

[[nodiscard]] bool validateLateStream(ByteReader reader, u16 start, const LateTraits& traits) {
  u32 offset = start;
  for (u32 command = 0; command < 8; ++command) {
    if (!reader.has(offset, 1)) {
      return false;
    }
    const detail::CommandShape shape = detail::commandShape(Variant::LateFamily, traits, reader.u8At(offset));
    if (shape.size == 0 || !reader.has(offset, shape.size)) {
      return false;
    }
    if (shape.terminatesStream) {
      return true;
    }
    offset += shape.size;
  }
  return true;
}

[[nodiscard]] bool validateArcusStream(ByteReader reader, u16 start) {
  u32 offset = start;
  int strongEvents = 0;
  int notes = 0;
  int weakRun = 0;
  for (u32 command = 0; command < 12; ++command) {
    if (!reader.has(offset, 1)) {
      return false;
    }
    const u8 opcode = reader.u8At(offset);
    const detail::CommandShape shape = detail::commandShape(Variant::Arcus, {}, opcode);
    if (!reader.has(offset, shape.size)) {
      return false;
    }
    if (shape.strongValidationSignal) {
      ++strongEvents;
      weakRun = 0;
      notes += opcode < 0x80;
    } else if (++weakRun > 2) {
      return false;
    }
    if (shape.terminatesStream) {
      return strongEvents > 0;
    }
    offset += shape.size;
  }
  return strongEvents >= 2 && notes > 0;
}

[[nodiscard]] bool middleHandler(ByteReader reader, u32 table, u8 opcode) {
  if (opcode < 0xe0) {
    return true;
  }
  const u32 entry = table + (opcode - 0xe0) * 2;
  if (!reader.has(entry, 2)) {
    return false;
  }
  const u16 handler = reader.le16(entry);
  return handler >= 0x0800 && handler < 0x2800;
}

[[nodiscard]] bool usableMiddleCommandTable(ByteReader reader, u32 table) {
  if (!reader.has(table, 0x40)) {
    return false;
  }
  constexpr std::array<u8, 7> essential{0xe0, 0xe1, 0xe2, 0xe7, 0xec, 0xf4, 0xfd};
  if (!std::ranges::all_of(essential, [&](u8 opcode) { return middleHandler(reader, table, opcode); })) {
    return false;
  }
  int handlers = 0;
  for (u32 opcode = 0xe0; opcode <= 0xff; ++opcode) {
    const u16 handler = reader.le16(table + (opcode - 0xe0) * 2);
    if (handler == 0) {
      continue;
    }
    if (handler < 0x0800 || handler >= 0x2800) {
      return false;
    }
    ++handlers;
  }
  return handlers >= 12;
}

[[nodiscard]] bool validateMiddleStream(ByteReader reader, u16 start, u32 commandTable) {
  u32 offset = start;
  int events = 0;
  int notes = 0;
  for (u32 command = 0; command < 12; ++command) {
    if (!reader.has(offset, 1)) {
      return false;
    }
    const u8 opcode = reader.u8At(offset);
    const detail::CommandShape shape = detail::commandShape(Variant::Middle, {}, opcode);
    if (shape.size == 0 || !middleHandler(reader, commandTable, opcode) || !reader.has(offset, shape.size)) {
      return false;
    }
    ++events;
    notes += opcode < 0x80;
    if (shape.terminatesStream) {
      return events > 0;
    }
    offset += shape.size;
  }
  return events >= 2 && notes > 0;
}

template <class Validate>
[[nodiscard]] std::vector<u16> resolveStarts(ByteReader reader, u16 table, const HeaderProfile& profile,
                                             Validate validate) {
  std::vector<u16> starts;
  for (u32 index = 0; index < kTableSearchLimit; ++index) {
    const u32 entry = table + index * 2;
    if (!reader.has(entry, 2)) {
      break;
    }
    const u8 relativeHigh = reader.u8At(entry + 1);
    if (relativeHigh == 0xff) {
      break;
    }
    const u16 start = relocatedAddress(reader.u8At(entry), relativeHigh, profile.relocationPage);
    if (!reader.has(start, 1) || !validate(start)) {
      break;
    }
    starts.push_back(start);
  }
  return starts;
}

template <class Validate>
[[nodiscard]] std::optional<HeaderCandidate> evaluateHeader(ByteReader reader, const HeaderProfile& profile,
                                                            Validate validate) {
  if (!reader.has(profile.headerAddress, profile.headerLength)) {
    return std::nullopt;
  }
  HeaderCandidate candidate{
      .headerAddress = profile.headerAddress,
      .relocationPage = profile.relocationPage,
  };
  candidate.channels.reserve(profile.channelCount);
  candidate.score += reader.u8At(profile.headerAddress + 0x22) != 0 ? 2 : 0;

  for (u8 channelIndex = 0; channelIndex < profile.channelCount; ++channelIndex) {
    const u32 descriptor = profile.headerAddress + kDescriptorOffset + channelIndex * 3;
    const u8 status = reader.u8At(descriptor);
    const u16 table =
        relocatedAddress(reader.u8At(descriptor + 1), reader.u8At(descriptor + 2), profile.relocationPage);
    ChannelLayout channel{
        .index = channelIndex,
        .status = status,
        .pointerTableAddress = table,
        .descriptorRange = reader.range(descriptor, 3),
    };
    if ((status & profile.activeMask) == 0) {
      candidate.channels.push_back(std::move(channel));
      continue;
    }
    ++candidate.activeChannels;
    if (!reader.has(table, 2)) {
      candidate.channels.push_back(std::move(channel));
      continue;
    }
    channel.streamStarts = resolveStarts(reader, table, profile, validate);
    if (channel.streamStarts.empty()) {
      candidate.channels.push_back(std::move(channel));
      continue;
    }
    ++candidate.validChannels;
    candidate.score += 8 + std::min<int>(static_cast<int>(channel.streamStarts.size()), 4);

    const u32 slot = profile.slotBase + channelIndex * profile.slotSize;
    const bool live = (reader.u8At(slot) & profile.activeMask) != 0;
    const u16 liveTable = reader.le16(slot + 1);
    if (liveTable == table) {
      candidate.score += live ? 5 : 2;
    }
    const u16 liveCurrent = reader.le16(slot + 3);
    if (live && reader.has(liveCurrent, 1) && validate(liveCurrent)) {
      ++candidate.score;
    }
    candidate.channels.push_back(std::move(channel));
  }

  if (candidate.activeChannels == 0 || candidate.validChannels == 0) {
    return std::nullopt;
  }
  candidate.score += candidate.validChannels >= 2 ? 4 : 0;
  return candidate;
}

[[nodiscard]] HeaderProfile lateHeaderProfile(u8 page) {
  return HeaderProfile{static_cast<u16>(page << 8), page, kLateSlotBase,    kLateSlotSize,
                       kLateChannelCount,           0x80, kLateHeaderLength};
}

[[nodiscard]] HeaderProfile arcusHeaderProfile() {
  return HeaderProfile{kArcusHeader, kArcusRelocationPage, kArcusSlotBase, kArcusSlotSize, kArcusChannelCount,
                       0x01,         kArcusHeaderLength};
}

[[nodiscard]] HeaderProfile middleHeaderProfile(u8 page) {
  return HeaderProfile{static_cast<u16>(page << 8), page, kMiddleSlotBase,    kMiddleSlotSize,
                       kMiddleChannelCount,         0x01, kMiddleHeaderLength};
}

[[nodiscard]] std::optional<HeaderCandidate> lateCandidate(ByteReader reader, const LateProfile& profile) {
  const auto evaluate = [&](u8 page) {
    return evaluateHeader(reader, lateHeaderProfile(page),
                          [&](u16 start) { return validateLateStream(reader, start, profile.traits); });
  };
  if (!profile.searchAllHeaderBanks) {
    const u8 page = static_cast<u8>(profile.sequenceBaseAdd + reader.u8At(kCurrentSongPageAddress));
    if (auto candidate = evaluate(page); candidate && candidate->validChannels >= 1) {
      return candidate;
    }
  }
  std::optional<HeaderCandidate> best;
  const u32 firstPage = profile.searchAllHeaderBanks ? 0 : profile.sequenceBaseAdd;
  for (u32 page = firstPage; page <= 0xff; ++page) {
    auto candidate = evaluate(static_cast<u8>(page));
    if (!candidate || candidate->validChannels < 2) {
      continue;
    }
    if (!best || candidate->score > best->score) {
      best = std::move(candidate);
    }
  }
  return best && best->score >= 18 ? best : std::nullopt;
}

[[nodiscard]] Variant lateVariant(u8 baseAdd, const LateTraits& traits) {
  if (baseAdd == 0x24 && !traits.remapHighInstrumentIds && traits.programChangeHasDelay) {
    return Variant::LeadingJockey;
  }
  if (baseAdd == 0x30 && !traits.remapHighInstrumentIds && !traits.programChangeHasDelay &&
      !traits.hasInstrument5KeySplit) {
    return Variant::TenshiNoUta;
  }
  if (baseAdd == 0x38) {
    return Variant::StarOcean;
  }
  if (baseAdd == 0x40 && traits.remapHighInstrumentIds && traits.specialInstrumentUpper == 0x50 &&
      !traits.hasInstrument5KeySplit) {
    return Variant::ParlorMini;
  }
  if (baseAdd == 0x40 && traits.remapHighInstrumentIds && traits.specialInstrumentUpper == 0x48 &&
      !traits.hasInstrument5KeySplit) {
    return Variant::TalesOfPhantasia;
  }
  return Variant::LateFamily;
}

[[nodiscard]] std::vector<LateProfile> lateProfiles(ByteReader reader) {
  const bool lateScoreReader = findBytePattern(reader, kReadScoreByte).has_value() ||
                               findBytePattern(reader, kLateReadScoreByteGeneric).has_value();
  const bool directWithDelay = findBytePattern(reader, kLoadInstrumentDirectWithDelay).has_value();
  const bool directNoDelay = findBytePattern(reader, kLoadInstrumentDirectNoDelay).has_value();
  const auto remapUpper = firstPatternImmediate(reader, kLoadInstrumentRemap, 13);
  auto baseAdds = patternImmediates(reader, {{&kReadBgmHeaderLoader, 8},
                                             {&kReadBgmHeader, 14},
                                             {&kLateHeaderBaseAdd, 5},
                                             {&kResolveScorePointer, 49},
                                             {&kLateResolvePointerBaseAdd, 9}});
  const bool searchAll = baseAdds.empty() && lateScoreReader;
  if (searchAll) {
    baseAdds.push_back(0);
  }

  std::vector<LateProfile> result;
  for (const u8 baseAdd : baseAdds) {
    const bool starOcean = baseAdd == 0x38;
    const bool remapped = remapUpper.has_value() || starOcean || baseAdd == 0x40;
    const LateTraits traits{
        .specialInstrumentUpper = remapUpper.value_or(starOcean ? 0x50 : 0x48),
        .remapHighInstrumentIds = remapped,
        .hasInstrument5KeySplit = starOcean,
        .programChangeHasDelay = !remapped && (baseAdd == 0x24 || directWithDelay),
    };
    const bool confirmed = remapped || directWithDelay || directNoDelay;
    result.push_back(LateProfile{lateVariant(baseAdd, traits), baseAdd, traits, confirmed, searchAll});
  }
  return result;
}

[[nodiscard]] std::vector<MiddleProfile> middleProfiles(ByteReader reader) {
  std::vector<MiddleProfile> profiles{
      MiddleProfile{Variant::AceONerae, kAceONeraeCommandTable, {0x30, 0x50}},
      MiddleProfile{Variant::DarkKingdom, kDarkKingdomCommandTable, {0x47, 0x59}},
  };
  std::vector<u8> discoveredPages;
  appendUnique(discoveredPages, reader.u8At(kMiddleBankPageAddress));
  for (u8 page : patternImmediates(reader, {{&kMiddleSetHeaderBank, 1}})) {
    appendUnique(discoveredPages, page);
  }
  u32 begin = 0;
  while (const auto found = findBytePattern(reader, kMiddleHighCommandDispatch, begin)) {
    const u32 table = reader.le16(*found + 6);
    if (usableMiddleCommandTable(reader, table) && std::ranges::none_of(profiles, [&](const MiddleProfile& profile) {
          return profile.commandTableAddress == table;
        })) {
      profiles.push_back(MiddleProfile{Variant::Middle, table, discoveredPages});
    }
    begin = *found + 1;
  }
  return profiles;
}

[[nodiscard]] std::optional<HeaderCandidate> middleCandidate(ByteReader reader, const MiddleProfile& profile) {
  const auto evaluate = [&](u8 page) {
    return evaluateHeader(reader, middleHeaderProfile(page),
                          [&](u16 start) { return validateMiddleStream(reader, start, profile.commandTableAddress); });
  };
  const u8 livePage = reader.u8At(kMiddleBankPageAddress);
  if (std::ranges::find(profile.bankPages, livePage) != profile.bankPages.end()) {
    if (auto live = evaluate(livePage); live && live->validChannels >= 1) {
      return live;
    }
  }
  std::optional<HeaderCandidate> best;
  for (u8 page : profile.bankPages) {
    auto candidate = evaluate(page);
    if (candidate && candidate->validChannels >= 2 && (!best || candidate->score > best->score)) {
      best = std::move(candidate);
    }
  }
  if (!best) {
    for (u32 page = 0; page <= 0xff; ++page) {
      auto candidate = evaluate(static_cast<u8>(page));
      if (candidate && candidate->validChannels >= 2 && (!best || candidate->score > best->score)) {
        best = std::move(candidate);
      }
    }
  }
  return best && best->score >= 18 ? best : std::nullopt;
}

[[nodiscard]] Layout makeSegmentedLayout(ByteReader reader, Variant variant, const HeaderCandidate& candidate,
                                         u32 commandTable = 0) {
  const bool arcus = variant == Variant::Arcus;
  return Layout{
      .variant = variant,
      .sequenceHeaderAddress = candidate.headerAddress,
      .relocationPage = candidate.relocationPage,
      .headerLength = arcus ? kArcusHeaderLength : kMiddleHeaderLength,
      .middleCommandTableAddress = commandTable,
      .channels = candidate.channels,
      .instruments =
          InstrumentLayout{
              .sampleDirAddress = kSegmentedSampleDir,
              .patchTableAddress = kSegmentedPatchTable,
              .patchMapAddress = arcus ? std::optional<u16>{kArcusPatchMap} : std::nullopt,
              .globalPitchBase = arcus ? reader.u8At(0xe3) : u8{0},
              .entrySize = static_cast<u8>(arcus ? 6 : 8),
              .confirmed = true,
          },
  };
}

}  // namespace

const char* variantName(Variant variant) {
  switch (variant) {
    case Variant::Arcus:
      return "Arcus / early segmented";
    case Variant::Middle:
      return "Middle-family segmented";
    case Variant::DarkKingdom:
      return "Dark Kingdom";
    case Variant::AceONerae:
      return "Ace o Nerae!";
    case Variant::LeadingJockey:
      return "Leading Jockey";
    case Variant::TenshiNoUta:
      return "Tenshi no Uta";
    case Variant::StarOcean:
      return "Star Ocean";
    case Variant::ParlorMini:
      return "Parlor Mini";
    case Variant::TalesOfPhantasia:
      return "Tales of Phantasia";
    case Variant::LateFamily:
      return "Late-family";
  }
  return "Unknown";
}

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }

  std::optional<std::pair<LateProfile, HeaderCandidate>> bestLate;
  for (const auto& profile : lateProfiles(reader)) {
    auto candidate = lateCandidate(reader, profile);
    if (!candidate) {
      continue;
    }
    const bool betterScore = !bestLate || candidate->score > bestLate->second.score;
    const bool betterTie = bestLate && candidate->score == bestLate->second.score && profile.instrumentModelConfirmed &&
                           !bestLate->first.instrumentModelConfirmed;
    if (betterScore || betterTie) {
      bestLate = std::pair{profile, std::move(*candidate)};
    }
  }
  if (bestLate) {
    const auto& [profile, candidate] = *bestLate;
    return Layout{
        .variant = profile.variant,
        .sequenceHeaderAddress = candidate.headerAddress,
        .relocationPage = candidate.relocationPage,
        .headerLength = kLateHeaderLength,
        .lateTraits = profile.traits,
        .channels = candidate.channels,
        .instruments =
            InstrumentLayout{
                .sampleDirAddress = kLateSampleDir,
                .patchTableAddress = kLateInstrumentTable,
                .volumeTableAddress = kLateVolumeTable,
                .entrySize = 4,
                .confirmed = profile.instrumentModelConfirmed,
            },
    };
  }

  std::optional<std::pair<MiddleProfile, HeaderCandidate>> bestMiddle;
  for (const auto& profile : middleProfiles(reader)) {
    if (!usableMiddleCommandTable(reader, profile.commandTableAddress)) {
      continue;
    }
    auto candidate = middleCandidate(reader, profile);
    if (candidate && (!bestMiddle || candidate->score > bestMiddle->second.score)) {
      bestMiddle = std::pair{profile, std::move(*candidate)};
    }
  }
  if (bestMiddle) {
    return makeSegmentedLayout(reader, bestMiddle->first.variant, bestMiddle->second,
                               bestMiddle->first.commandTableAddress);
  }

  if (!findBytePattern(reader, kArcusSetDir)) {
    return std::nullopt;
  }
  auto arcus =
      evaluateHeader(reader, arcusHeaderProfile(), [&](u16 start) { return validateArcusStream(reader, start); });
  if (!arcus || arcus->score < 18) {
    return std::nullopt;
  }
  return makeSegmentedLayout(reader, Variant::Arcus, *arcus);
}

}  // namespace vgmtrans::formats::wolf_team_snes
