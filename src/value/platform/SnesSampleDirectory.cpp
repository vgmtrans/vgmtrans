/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/platform/SnesSampleDirectory.h"

#include "value/model/SourceMap.h"
#include "value/synth/SynthModel.h"

#include <fmt/format.h>

#include <algorithm>

namespace vgmtrans::core {

std::optional<SnesBrrStream> inspectSnesBrrStream(ByteReader reader, u32 startAddress) {
  u32 offset = startAddress;
  while (reader.has(offset, 9)) {
    const u8 header = reader.u8At(offset);
    offset += 9;
    if ((header & 1) != 0) {
      return SnesBrrStream{
          .encodedData = reader.range(startAddress, offset - startAddress),
          .loops = (header & 2) != 0,
      };
    }
  }
  return std::nullopt;
}

std::optional<SnesSampleDirectoryEntry> SnesSampleDirectory::entry(u8 index, bool inspectStream) const {
  const u32 entryAddress = baseAddress_ + static_cast<u32>(index) * 4;
  auto result = readSnesSampleDirectoryEntry(reader_, entryAddress, inspectStream);
  if (result) {
    result->index = index;
  }
  return result;
}

std::optional<SnesSampleDirectoryEntry> readSnesSampleDirectoryEntry(ByteReader reader, u32 entryAddress,
                                                                     bool inspectStream) {
  if (!reader.has(entryAddress, 4)) {
    return std::nullopt;
  }

  SnesSampleDirectoryEntry result{
      .entryRange = reader.range(entryAddress, 4),
      .startAddress = reader.le16(entryAddress),
      .loopAddress = reader.le16(entryAddress + 2),
  };
  // Match the legacy validity rule: a complete 9-byte block plus at least one
  // following byte must fit in ARAM.
  if (result.loopAddress < result.startAddress || !reader.has(result.startAddress, 10)) {
    return std::nullopt;
  }
  if (!inspectStream) {
    return result;
  }

  result.stream = inspectSnesBrrStream(reader, result.startAddress);
  if (!result.stream) {
    return std::nullopt;
  }
  if (result.stream->loops && result.loopAddress >= result.stream->encodedData.endOffset()) {
    return std::nullopt;
  }
  return result;
}

std::optional<u32> SnesBrrCatalog::index(u8 srcn) const {
  const auto found = std::ranges::find(samples, srcn, &SnesBrrSample::srcn);
  return found == samples.end() ? std::nullopt
                                : std::optional<u32>{static_cast<u32>(std::distance(samples.begin(), found))};
}

std::optional<u32> SnesBrrCatalog::firstIndexStartingAt(u32 address) const {
  const auto found = std::ranges::find(samples, address, &SnesBrrSample::startAddress);
  return found == samples.end() ? std::nullopt
                                : std::optional<u32>{static_cast<u32>(std::distance(samples.begin(), found))};
}

std::optional<u32> SnesBrrCatalog::canonicalIndex(u8 srcn) const {
  const auto found = index(srcn);
  return found ? firstIndexStartingAt(samples[*found].startAddress) : std::nullopt;
}

SnesBrrCatalog readSnesBrrCatalog(ByteReader reader, u32 directoryAddress, std::span<const u8> referencedSrcns) {
  std::vector<u8> srcns(referencedSrcns.begin(), referencedSrcns.end());
  std::ranges::sort(srcns);
  const auto duplicates = std::ranges::unique(srcns);
  srcns.erase(duplicates.begin(), duplicates.end());

  SnesBrrCatalog catalog;
  const SnesSampleDirectory directory(reader, directoryAddress);
  for (const u8 srcn : srcns) {
    const auto entry = directory.entry(srcn);
    if (!entry || !entry->stream) {
      continue;
    }
    catalog.samples.push_back(SnesBrrSample{
        .srcn = srcn,
        .directoryEntry = entry->entryRange,
        .startAddress = entry->startAddress,
        .loopAddress = entry->loopAddress,
        .stream = *entry->stream,
    });
  }

  if (!catalog.samples.empty()) {
    const u32 begin = static_cast<u32>(catalog.samples.front().directoryEntry.offset);
    const u32 end = static_cast<u32>(catalog.samples.back().directoryEntry.endOffset());
    catalog.directoryRange = reader.range(begin, end - begin);
  }
  return catalog;
}

std::optional<SampleRef> SnesBrrSampleRefs::findSrcn(u8 srcn) const {
  const auto found = std::ranges::find(entries_, srcn, &Entry::srcn);
  return found == entries_.end() ? std::nullopt : std::optional<SampleRef>{found->sample};
}

std::optional<SampleRef> SnesBrrSampleRefs::firstStartingAt(u32 address) const {
  const auto found = std::ranges::find(entries_, address, &Entry::startAddress);
  return found == entries_.end() ? std::nullopt : std::optional<SampleRef>{found->sample};
}

SnesBrrSampleRefs addSnesBrrSamples(SampleCollectionBuilder& samples, ByteReader reader, const SnesBrrCatalog& catalog,
                                    std::string_view directoryEntryKind) {
  samples.include(catalog.directoryRange);
  const SourceAnnotationId root =
      samples.source(SourceRole::Table, "Sample DIR", catalog.directoryRange, "snes-sample-dir").id();

  SnesBrrSampleRefs refs;
  refs.entries_.reserve(catalog.samples.size());
  for (const auto& info : catalog.samples) {
    const u32 encodedLength = static_cast<u32>(info.stream.encodedData.size);
    const u32 decodedLength = (encodedLength / 9) * 16;
    const u32 lastBlockAddress = encodedLength >= 9 ? info.startAddress + encodedLength - 9 : info.startAddress;
    const bool loopEnabled =
        info.stream.loops && info.loopAddress >= info.startAddress && info.loopAddress <= lastBlockAddress;
    const u32 loopStart = loopEnabled ? ((info.loopAddress - info.startAddress) / 9) * 16 : 0;
    auto sample = samples.add(
        info.srcn, Sample{
                       .name = fmt::format("Sample {}", static_cast<unsigned>(info.srcn)),
                       .codec = AudioCodec::SnesBrr,
                       .encodedData = info.stream.encodedData,
                       .sampleRate = 32000,
                       .channels = 1,
                       .bitsPerSample = 16,
                       .loop =
                           Loop{
                               .enabled = loopEnabled,
                               .start = loopStart,
                               .length = loopEnabled && decodedLength >= loopStart ? decodedLength - loopStart : 0,
                           },
                   });

    auto directoryEntry = sample
                              .source(fmt::format("Sample {} DIR Entry", static_cast<unsigned>(info.srcn)),
                                      info.directoryEntry, directoryEntryKind)
                              .field("start", reader.range(static_cast<u32>(info.directoryEntry.offset), 2),
                                     info.startAddress, SourceValueDisplay::Address)
                              .field("loop", reader.range(static_cast<u32>(info.directoryEntry.offset) + 2, 2),
                                     info.loopAddress, SourceValueDisplay::Address)
                              .link(SourceLinkRole::PointsTo, SourceTarget{info.stream.encodedData}, "BRR data")
                              .parent(root);
    sample
        .source(fmt::format("Sample {} BRR Data", static_cast<unsigned>(info.srcn)), info.stream.encodedData,
                "snes-brr-payload")
        .role(SourceRole::Payload)
        .parent(directoryEntry.id());

    const auto canonical = refs.firstStartingAt(info.startAddress).value_or(sample.ref());
    refs.entries_.push_back(SnesBrrSampleRefs::Entry{
        .srcn = info.srcn,
        .startAddress = info.startAddress,
        .sample = canonical,
    });
  }

  return refs;
}

SampleCollection buildSnesBrrSampleCollection(ByteReader reader, const SnesBrrCatalog& catalog,
                                              AssetId sampleCollectionId, SourceMapBuilder& sourceMap,
                                              std::string_view directoryEntryKind) {
  SampleCollectionBuilder samples{sampleCollectionId, &sourceMap};
  [[maybe_unused]] const auto refs = addSnesBrrSamples(samples, reader, catalog, directoryEntryKind);
  return std::move(samples).finish().value;
}

}  // namespace vgmtrans::core
