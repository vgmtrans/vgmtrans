/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS1/SonyPS1.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <optional>
#include <vector>

namespace vgmtrans::formats::sony_ps1 {

using namespace core;

namespace {

constexpr u32 kReadAheadBlocks = 10;
constexpr int kMaxFilterDifference = static_cast<int>(kReadAheadBlocks * 2.5);
constexpr int kMaxRangeFilterDifference = static_cast<int>(kReadAheadBlocks * 3.2);
constexpr int kMinimumStrictUniqueBytes = static_cast<int>(kReadAheadBlocks * 4);
constexpr int kMaximumByteRepetition = static_cast<int>(kReadAheadBlocks * 5.5);
constexpr u32 kBackScanLimit = 0x5000;

[[nodiscard]] bool zeroBlock(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kPsxAdpcmBlockBytes)) {
    return false;
  }
  for (u32 byte = 0; byte < kPsxAdpcmBlockBytes; ++byte) {
    if (reader.u8At(offset + byte) != 0) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool validFilterShift(u8 value) {
  return (value >> 4) <= 4 && (value & 0x0f) <= 12;
}

[[nodiscard]] bool validFlags(u8 value) {
  return (value & 0xf8) == 0;
}

// This is the legacy PSXSampColl start test, retained deliberately: its
// continuity, uniqueness, and repetition checks have years of corpus tuning.
[[nodiscard]] bool validSampleStart(ByteReader reader, u32 offset, bool allowShort) {
  if (!zeroBlock(reader, offset)) {
    return false;
  }
  const u32 first = offset + kPsxAdpcmBlockBytes;
  if (!reader.has(first, kPsxAdpcmBlockBytes) || reader.le16(first) == 0) {
    return false;
  }

  std::array<int, 256> byteCount{};
  int uniqueBytes = 0;
  int filterDifference = 0;
  int rangeDifference = 0;
  u8 previousFilterShift = 0;
  u32 framesSeen = 0;

  for (u32 frame = 0; frame < kReadAheadBlocks; ++frame) {
    const u64 current64 = static_cast<u64>(first) + frame * kPsxAdpcmBlockBytes;
    if (current64 > std::numeric_limits<u32>::max()) {
      return false;
    }
    const u32 current = static_cast<u32>(current64);
    if (!reader.has(current, kPsxAdpcmBlockBytes)) {
      return false;
    }
    if (zeroBlock(reader, current)) {
      if (!allowShort || current == first) {
        return false;
      }
      break;
    }
    ++framesSeen;

    const u8 filterShift = reader.u8At(current);
    const u8 flags = reader.u8At(current + 1);
    if (!validFlags(flags) || !validFilterShift(filterShift)) {
      return false;
    }
    for (u32 byte = 2; byte < kPsxAdpcmBlockBytes; ++byte) {
      const u8 value = reader.u8At(current + byte);
      ++byteCount[value];
      if (byteCount[value] == 1) {
        ++uniqueBytes;
      }
    }

    if (frame == 0 && filterShift == 0 && flags == 0) {
      u32 zeros = 0;
      for (u32 byte = 2; byte < kPsxAdpcmBlockBytes; ++byte) {
        zeros += reader.u8At(current + byte) == 0 ? 1 : 0;
      }
      if (zeros >= 10) {
        return false;
      }
    }

    if (frame != 0) {
      filterDifference += std::abs(static_cast<int>(filterShift >> 4) - static_cast<int>(previousFilterShift >> 4));
      rangeDifference += std::abs(static_cast<int>(filterShift & 0x0f) - static_cast<int>(previousFilterShift & 0x0f));
    }
    previousFilterShift = filterShift;
  }

  if (filterDifference > kMaxFilterDifference || rangeDifference > kMaxRangeFilterDifference ||
      uniqueBytes < (allowShort ? 8 : kMinimumStrictUniqueBytes)) {
    return false;
  }
  if (std::ranges::any_of(byteCount, [](int count) { return count > kMaximumByteRepetition; })) {
    return false;
  }
  return allowShort || framesSeen == kReadAheadBlocks;
}

[[nodiscard]] bool implicitSampleStart(ByteReader reader, u32 offset, u32 end) {
  u32 continuedBlocks = 0;
  u8 continueFlag = 0xff;
  bool badBlock = false;
  while (static_cast<u64>(offset) + continuedBlocks * kPsxAdpcmBlockBytes + kPsxAdpcmBlockBytes <= end) {
    const u32 current = offset + continuedBlocks * kPsxAdpcmBlockBytes;
    const u8 flags = reader.u8At(current + 1);
    if (!validFlags(flags)) {
      badBlock = true;
      break;
    }
    if (continueFlag == 0xff && (flags == 0 || flags == 2)) {
      continueFlag = flags;
    }
    if (flags != continueFlag) {
      if (flags == 0 || flags == 2) {
        badBlock = true;
      }
      break;
    }
    ++continuedBlocks;
  }
  return !badBlock && ((continueFlag == 0 && continuedBlocks >= 16) || (continueFlag == 2 && continuedBlocks >= 3));
}

[[nodiscard]] std::optional<SonyPs1SampleBodyLayout> parseBody(ByteReader reader, u32 start) {
  if (reader.size() > std::numeric_limits<u32>::max()) {
    return std::nullopt;
  }
  const u32 end = static_cast<u32>(reader.size());
  SonyPs1SampleBodyLayout body{.offset = start};
  u32 offset = start;

  while (static_cast<u64>(offset) + kPsxAdpcmBlockBytes * 2 <= end) {
    if (!zeroBlock(reader, offset) && !implicitSampleStart(reader, offset, end)) {
      break;
    }

    const u32 firstDataBlock = offset + kPsxAdpcmBlockBytes;
    if (!reader.has(firstDataBlock, kPsxAdpcmBlockBytes) || !validFlags(reader.u8At(firstDataBlock + 1)) ||
        !validFilterShift(reader.u8At(firstDataBlock)) || zeroBlock(reader, firstDataBlock)) {
      break;
    }

    const u32 sampleStart = offset;
    offset += kPsxAdpcmBlockBytes;
    u32 firstEnd = 0;
    while (reader.has(offset, kPsxAdpcmBlockBytes) && offset + kPsxAdpcmBlockBytes <= end) {
      if (zeroBlock(reader, offset)) {
        break;
      }
      const bool endFlag = (reader.u8At(offset + 1) & 1) != 0;
      offset += kPsxAdpcmBlockBytes;
      if (!endFlag) {
        continue;
      }
      if (firstEnd == 0) {
        firstEnd = offset;
      }
      if (static_cast<u64>(offset) + kPsxAdpcmBlockBytes * 2 < end) {
        const u8 nextFilterShift = reader.u8At(offset);
        const u8 nextFlags = reader.u8At(offset + 1);
        if (nextFlags < 1 || nextFlags > 3 || !validFilterShift(nextFilterShift)) {
          if ((nextFlags != 0 || nextFilterShift != 0) && !zeroBlock(reader, offset + kPsxAdpcmBlockBytes)) {
            break;
          }
        }
      } else {
        break;
      }
    }

    u32 extraGunk = 0;
    while (reader.has(offset, kPsxAdpcmBlockBytes) && reader.u8At(offset + 1) == 7) {
      extraGunk += kPsxAdpcmBlockBytes;
      offset += kPsxAdpcmBlockBytes;
    }
    if (firstEnd == 0) {
      firstEnd = offset;
    }
    const u32 available = firstEnd - sampleStart;
    const u32 encodedLength = extraGunk <= available ? available - extraGunk : available;
    auto stream = inspectPsxAdpcmStream(reader, sampleStart, sampleStart + encodedLength);
    if (!stream) {
      break;
    }
    body.samples.push_back(SonyPs1SampleLayout{
        .offset = sampleStart,
        .storageLength = offset - sampleStart,
        .stream = *stream,
    });
  }

  body.length = offset - start;
  if (body.length <= kPsxAdpcmBlockBytes * 2 || body.samples.empty()) {
    return std::nullopt;
  }
  return body;
}

[[nodiscard]] bool matchesVab(const SonyPs1SampleBodyLayout& body, const std::vector<u32>& sizes) {
  u64 expectedOffset = 0;
  size_t sampleIndex = 0;
  for (const u32 size : sizes) {
    if (size == 0) {
      continue;
    }
    if (sampleIndex >= body.samples.size()) {
      return false;
    }
    const auto& sample = body.samples[sampleIndex++];
    if (sample.offset - body.offset != expectedOffset || sample.storageLength < size ||
        sample.storageLength > size + 32) {
      return false;
    }
    expectedOffset += size;
  }
  return true;
}

}  // namespace

std::vector<SonyPs1SampleBodyLayout> findSonyPs1SampleBodies(ByteReader reader) {
  std::vector<SonyPs1SampleBodyLayout> bodies;
  if (reader.size() > std::numeric_limits<u32>::max()) {
    return bodies;
  }
  const u32 end = static_cast<u32>(reader.size());
  for (u32 offset = 0; static_cast<u64>(offset) + kPsxAdpcmBlockBytes * (kReadAheadBlocks + 1) < end; ++offset) {
    if (!zeroBlock(reader, offset)) {
      u32 lastNonzero = kPsxAdpcmBlockBytes - 1;
      while (lastNonzero != 0 && reader.u8At(offset + lastNonzero) == 0) {
        --lastNonzero;
      }
      offset += lastNonzero;
      continue;
    }
    if (!validSampleStart(reader, offset, false)) {
      continue;
    }

    const u32 originalOffset = offset;
    u32 start = offset;
    u32 scanned = kPsxAdpcmBlockBytes;
    while (scanned < kBackScanLimit && offset >= scanned + kPsxAdpcmBlockBytes) {
      const u32 candidate = offset - scanned;
      scanned += kPsxAdpcmBlockBytes;
      if (!zeroBlock(reader, candidate)) {
        if (!validFilterShift(reader.u8At(candidate)) || !validFlags(reader.u8At(candidate + 1))) {
          break;
        }
        continue;
      }
      if (!validSampleStart(reader, candidate, true)) {
        break;
      }
      start = candidate;
    }

    auto body = parseBody(reader, start);
    if (!body) {
      continue;
    }
    const u32 length = body->length;
    bodies.push_back(std::move(*body));
    if (static_cast<u64>(start) + length - 1 < originalOffset) {
      offset = originalOffset + 32;
    } else {
      offset = start + length - 1;
    }
  }
  return bodies;
}

bool matchesSonyPs1SampleBodyAt(ByteReader reader, u32 offset, const std::vector<u32>& sampleSizes) {
  if (!validSampleStart(reader, offset, true)) {
    return false;
  }
  bool foundSample = false;
  bool foundAudio = false;
  u64 sampleOffset = offset;
  for (const u32 size : sampleSizes) {
    if (size == 0) {
      continue;
    }
    if (sampleOffset + size > std::numeric_limits<u32>::max() || !reader.has(sampleOffset, size) ||
        !inspectPsxAdpcmStream(reader, static_cast<u32>(sampleOffset), static_cast<u32>(sampleOffset + size))) {
      return false;
    }
    foundSample = true;
    foundAudio |= std::ranges::any_of(reader.slice(sampleOffset, size), [](u8 byte) { return byte != 0; });
    sampleOffset += size;
  }
  return foundSample && foundAudio;
}

std::optional<u32> matchSonyPs1SampleBody(ByteReader reader, u32 preferredOffset, const std::vector<u32>& sampleSizes,
                                          bool forceSingle) {
  const auto bodies = findSonyPs1SampleBodies(reader);
  // FilegroupMatcher intentionally pairs a sole instrument set and sample
  // collection even when footprint heuristics reject the pair.
  if (forceSingle && bodies.size() == 1) {
    return bodies.front().offset;
  }
  std::optional<u32> closest;
  u64 closestDistance = std::numeric_limits<u64>::max();
  for (const auto& body : bodies) {
    // The VAB size table is authoritative when a body omits the conventional
    // silent frame at the start of some samples. In that case the generic
    // body parser can merge adjacent samples, but it still gives us a strongly
    // validated collection start. Recheck that start using the declared VAB
    // boundaries before rejecting it.
    if (!matchesVab(body, sampleSizes) && !matchesSonyPs1SampleBodyAt(reader, body.offset, sampleSizes)) {
      continue;
    }
    const u64 distance = body.offset > preferredOffset ? body.offset - preferredOffset : preferredOffset - body.offset;
    if (distance < closestDistance) {
      closest = body.offset;
      closestDistance = distance;
    }
  }
  return closest;
}

}  // namespace vgmtrans::formats::sony_ps1
