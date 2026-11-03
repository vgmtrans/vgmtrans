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
  if (!reader.has(result.startAddress, 10)) {
    return std::nullopt;
  }
  if (!inspectStream) {
    return result;
  }

  result.stream = inspectSnesBrrStream(reader, result.startAddress);
  if (!result.stream) {
    return std::nullopt;
  }
  if (result.stream->loops) {
    const u32 lastBlock = static_cast<u32>(result.stream->encodedData.endOffset()) - 9;
    if (result.loopAddress < result.startAddress || result.loopAddress > lastBlock ||
        ((result.loopAddress - result.startAddress) % 9) != 0) {
      return std::nullopt;
    }
  }
  return result;
}

std::vector<SnesBrrSample> readSnesBrrCatalog(ByteReader reader, u32 directoryAddress, std::vector<u8> srcns) {
  std::ranges::sort(srcns);
  const auto duplicates = std::ranges::unique(srcns);
  srcns.erase(duplicates.begin(), duplicates.end());

  std::vector<SnesBrrSample> catalog;
  const SnesSampleDirectory directory(reader, directoryAddress);
  for (const u8 srcn : srcns) {
    const auto entry = directory.entry(srcn);
    if (!entry) {
      continue;
    }
    catalog.push_back(SnesBrrSample{
        .srcn = srcn,
        .directoryEntry = entry->entryRange,
        .startAddress = entry->startAddress,
        .loopAddress = entry->loopAddress,
        .stream = *entry->stream,
    });
  }

  return catalog;
}

std::optional<SampleRef> SnesBrrSampleRefs::findSrcn(u8 srcn) const {
  const auto found = std::ranges::find(entries_, srcn, &Entry::srcn);
  return found == entries_.end() ? std::nullopt : std::optional<SampleRef>{found->sample};
}

SnesBrrSampleRefs addSnesBrrSamples(SamplePoolBuilder& samples, ByteReader reader,
                                    std::span<const SnesBrrSample> catalog, std::string_view directoryEntryKind) {
  return addSnesBrrSamples(samples, reader, catalog, {}, directoryEntryKind);
}

SnesBrrSampleRefs addSnesBrrSamples(SamplePoolBuilder& samples, ByteReader reader,
                                    std::span<const SnesBrrSample> catalog, std::span<const u8> usedSrcns,
                                    std::string_view directoryEntryKind) {
  SourceRange directoryRange;
  for (const auto& info : catalog) {
    directoryRange.include(info.directoryEntry);
  }
  samples.include(directoryRange);
  const SourceAnnotationId root =
      samples.source(SourceRole::Table, "Sample DIR", directoryRange, "snes-sample-dir").id();

  SnesBrrSampleRefs refs;
  refs.entries_.reserve(catalog.size());
  for (const auto& info : catalog) {
    const bool unused = !usedSrcns.empty() && std::ranges::find(usedSrcns, info.srcn) == usedSrcns.end();
    const std::string name = fmt::format("Sample {}", info.srcn);
    const char* suffix = unused ? " (unused)" : "";
    const u32 encodedLength = static_cast<u32>(info.stream.encodedData.size);
    const u32 decodedLength = (encodedLength / 9) * 16;
    const u32 loopStart = info.stream.loops ? ((info.loopAddress - info.startAddress) / 9) * 16 : 0;
    auto sample = samples.add(info.srcn, Sample{
                                             .name = name + suffix,
                                             .codec = AudioCodec::SnesBrr,
                                             .encodedData = info.stream.encodedData,
                                             .sampleRate = 32000,
                                             .channels = 1,
                                             .loop =
                                                 Loop{
                                                     .enabled = info.stream.loops,
                                                     .start = loopStart,
                                                     .length = info.stream.loops ? decodedLength - loopStart : 0,
                                                 },
                                         });

    auto directoryEntry = sample
                              .source(name + " DIR Entry" + suffix, info.directoryEntry, directoryEntryKind)
                              .field("start", reader.range(static_cast<u32>(info.directoryEntry.offset), 2),
                                     info.startAddress, SourceValueDisplay::Address)
                              .field("loop", reader.range(static_cast<u32>(info.directoryEntry.offset) + 2, 2),
                                     info.loopAddress, SourceValueDisplay::Address)
                              .link(SourceLinkRole::PointsTo, SourceTarget{info.stream.encodedData}, "BRR data")
                              .parent(root);
    sample
        .source(name + " BRR Data" + suffix, info.stream.encodedData, "snes-brr-payload")
        .role(SourceRole::Payload)
        .parent(directoryEntry.id());

    SampleRef canonical = sample.ref();
    const auto earlierSamples = catalog.first(refs.entries_.size());
    const auto alias = std::ranges::find_if(earlierSamples, [&](const SnesBrrSample& candidate) {
      return candidate.startAddress == info.startAddress && candidate.stream.loops == info.stream.loops &&
             (!info.stream.loops || candidate.loopAddress == info.loopAddress);
    });
    if (alias != earlierSamples.end()) {
      canonical = refs.entries_[std::distance(earlierSamples.begin(), alias)].sample;
    }
    refs.entries_.push_back(SnesBrrSampleRefs::Entry{
        .srcn = info.srcn,
        .sample = canonical,
    });
  }

  return refs;
}

}  // namespace vgmtrans::core
