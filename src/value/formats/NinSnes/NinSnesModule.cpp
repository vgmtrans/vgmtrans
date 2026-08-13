/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnes.h"

#include "value/scan/FormatRegistry.h"

#include <algorithm>
#include <string>

namespace vgmtrans::formats::nin_snes {

using namespace core;

namespace {

[[nodiscard]] SourceRange sequenceRange(ByteReader reader, const Layout& layout, const SequenceProgram& program) {
  u64 first = layout.playlistAddress;
  u64 last = first + 2;
  if (program.sectionPlaylist) {
    for (const SequenceSection& section : program.sectionPlaylist->sections) {
      first = std::min(first, section.address.value);
      last = std::max(last, section.address.value + kTrackCount * 2);
    }
    for (const PlaylistCommand& command : program.sectionPlaylist->commands) {
      if (!command.range.valid()) {
        continue;
      }
      first = std::min(first, command.range.offset);
      last = std::max(last, command.range.endOffset());
    }
  }
  return reader.range(first, last - first);
}

[[nodiscard]] ScanResult scan(const ScanInput& input) {
  const auto layout = findLayout(input.reader);
  if (!layout) {
    return {};
  }

  ScanResultBuilder result(input, "NinSnes");
  const std::string displayName = result.sourceDisplayName();
  // The sequence range depends on its decoded playlist, so decode against the
  // stable draft ID before filling the draft.
  auto sequence = result.sequence(displayName);
  SequenceParse parsed =
      decodeSequence(input.reader, *layout, sequence.id(), &result.sourceMap(), &result.diagnostics());
  sequence.range(sequenceRange(input.reader, *layout, parsed.program));
  sequence.program(std::move(parsed.program));

  // A recognizable driver can still have its instrument loader stripped (a
  // few unused SPCs do). Preserve the sequence as a standalone asset, but only
  // advertise an exportable collection once all three musical parts exist.
  if (layout->instrumentTableAddress && layout->spcDirAddress) {
    if (const auto synth = addSynth(result, *layout, parsed.recipes, displayName)) {
      result.sourceCollection(displayName).sequence(sequence).instrumentSet(synth->instruments).samples(synth->samples);
    } else {
      result.warning("NinSnes sequence found, but no valid instruments or samples were discovered",
                     input.reader.range(0, input.reader.size()));
    }
  } else {
    result.warning("NinSnes sequence found, but instrument table or SPC DIR address was not detected",
                   input.reader.range(0, input.reader.size()));
  }

  return result.finish();
}

}  // namespace

FormatDefinition definition() {
  return FormatDefinition{
      .module = {.name = "NinSnes",
                 .preferredSampleFilter = SampleFilter::SnesDspLowPass,
                 .acceptedFormats = {source_formats::kSnesAram},
                 .scan = scan},
      .sequenceDialects = {sequenceDialect()},
  };
}

}  // namespace vgmtrans::formats::nin_snes
