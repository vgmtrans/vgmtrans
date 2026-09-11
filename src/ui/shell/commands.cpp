/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "commands.h"

#include "ExportOptions.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#include <fmt/format.h>

#include "value/export/CollectionStitch.h"
#include "value/session/Session.h"

namespace vgmtrans::shell {
namespace {

using namespace core;
using Args = std::span<const std::string>;

struct Context {
  Session& session;
  std::ostream& out;
  std::ostream& err;
  bool failed = false;

  void error(std::string_view message) {
    err << "error: " << message << '\n';
    failed = true;
  }

  void diagnostics(std::span<const Diagnostic> diagnostics);
  void writeArtifacts(const std::filesystem::path& directory, std::span<const Artifact> artifacts);
};

std::string_view severityName(Severity severity) {
  switch (severity) {
    case Severity::Info:
      return "info";
    case Severity::Warning:
      return "warning";
    case Severity::Error:
      return "error";
  }
  return "unknown";
}

std::string rangeText(SourceRange range) {
  return fmt::format("source {} 0x{:x}:0x{:x}", range.source.value, range.offset, range.size);
}

void printDiagnostic(std::ostream& out, const Diagnostic& diagnostic) {
  out << severityName(diagnostic.severity);
  if (!diagnostic.code.empty()) {
    out << " [" << diagnostic.code << ']';
  }
  out << ": " << diagnostic.message;
  if (diagnostic.range) {
    out << " (" << rangeText(*diagnostic.range) << ')';
  }
  out << '\n';
}

void Context::diagnostics(std::span<const Diagnostic> diagnostics) {
  for (const auto& diagnostic : diagnostics) {
    printDiagnostic(err, diagnostic);
    failed |= diagnostic.severity == Severity::Error;
  }
}

template <typename T = u32>
T number(std::string_view text) {
  const auto original = text;
  int base = 10;
  if (text.starts_with("0x") || text.starts_with("0X")) {
    text.remove_prefix(2);
    base = 16;
  }
  T value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
    throw std::invalid_argument(fmt::format("invalid unsigned integer '{}' (use decimal or 0x hex)", original));
  }
  return value;
}

template <typename T>
const T& require(const T* value, std::string_view kind, u32 id) {
  if (value == nullptr) {
    throw std::invalid_argument(fmt::format("{} {} not found", kind, id));
  }
  return *value;
}

const Asset& asset(const SessionSnapshot& snapshot, std::string_view text) {
  const AssetId id{number(text)};
  return require(snapshot.asset(id), "asset", id.value);
}

const Collection& collection(const SessionSnapshot& snapshot, std::string_view text) {
  const CollectionId id{number(text)};
  return require(snapshot.collection(id), "collection", id.value);
}

std::string_view assetKind(const Asset& asset) {
  return std::visit(
      [](const auto& value) -> std::string_view {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, SequenceProgramAsset>) {
          return "sequence";
        }
        if constexpr (std::is_same_v<T, SoundBankAsset>) {
          return "sound-bank";
        }
        if constexpr (std::is_same_v<T, SamplePoolAsset>) {
          return "sample-pool";
        }
        return "misc";
      },
      asset);
}

void printAsset(std::ostream& out, const Asset& asset) {
  const auto& meta = metadata(asset);
  out << fmt::format("asset {} [{}] {} | {} | {}\n", meta.id.value, assetKind(asset), meta.name, meta.format,
                     rangeText(meta.range));
}

void load(Context& context, Args args) {
  std::vector<std::pair<SourceId, std::string>> opened;
  for (const auto& path : args) {
    try {
      opened.emplace_back(context.session.addSourceFromPath(path), path);
    } catch (const std::exception& error) {
      context.error(error.what());
    }
  }
  const auto diagnosticsBefore = context.session.snapshot().diagnostics().size();
  context.session.scanPendingSources();
  const auto snapshot = context.session.snapshot();
  context.diagnostics(std::span{snapshot.diagnostics()}.subspan(diagnosticsBefore));
  for (const auto& [id, path] : opened) {
    if (snapshot.source(id) != nullptr) {
      context.out << fmt::format("Loaded source {}: {}\n", id.value, path);
    } else {
      context.error("no supported music data found in " + path);
    }
  }
  context.out << fmt::format("{} sources, {} assets, {} collections\n", snapshot.sources().size(),
                             snapshot.assets().size(), snapshot.collections().size());
}

void sources(Context& context, Args args) {
  const auto snapshot = context.session.snapshot();
  const auto print = [&](const SourceFile& source) {
    context.out << fmt::format("source {} | {} | {} bytes", source.id.value, source.name, source.size);
    if (source.parent) {
      context.out << " | parent " << source.parent->value;
    }
    if (!source.path.empty()) {
      context.out << " | " << source.path.string();
    }
    context.out << '\n';
    if (!args.empty()) {
      if (source.title) {
        context.out << "  title: " << *source.title << '\n';
      }
      if (source.knownFormat) {
        context.out << "  format: " << *source.knownFormat << '\n';
      }
      if (source.origin) {
        context.out << "  origin: " << rangeText(*source.origin) << '\n';
      }
    }
  };
  if (!args.empty()) {
    const SourceId id{number(args[0])};
    print(require(snapshot.source(id), "source", id.value));
  } else {
    for (const auto& source : snapshot.sources()) {
      print(source);
    }
  }
}

void assets(Context& context, Args args) {
  const auto snapshot = context.session.snapshot();
  if (args.empty()) {
    for (const auto& value : snapshot.assets()) {
      printAsset(context.out, value);
    }
    return;
  }
  const auto& value = asset(snapshot, args[0]);
  printAsset(context.out, value);
  if (const auto* sequence = std::get_if<SequenceProgramAsset>(&value)) {
    context.out << "  PPQN: " << sequence->program.timebase.ppqn << '\n';
    for (size_t i = 0; i < sequence->program.tracks.size(); ++i) {
      const auto& track = sequence->program.tracks[i];
      context.out << fmt::format("  track {} | source track {} | 0x{:x} | {} commands | {}\n", i,
                                 track.sourceTrackNumber, track.startAddress.value, track.commands.size(), track.name);
    }
  } else if (const auto* bank = std::get_if<SoundBankAsset>(&value)) {
    context.out << fmt::format("  {} instruments, {} local samples\n", bank->instruments.size(),
                               bank->localSamples.samples.size());
  } else if (const auto* pool = std::get_if<SamplePoolAsset>(&value)) {
    context.out << "  " << pool->pool.samples.size() << " samples\n";
  }
}

void collections(Context& context, Args args) {
  const auto snapshot = context.session.snapshot();
  const auto print = [&](const Collection& value) {
    context.out << fmt::format("collection {} | {}{}\n", value.id.value, value.name,
                               value.isDiscovered() ? "" : " (user)");
    const auto member = [&](std::string_view kind, AssetId id) {
      const auto& found = require(snapshot.asset(id), "asset", id.value);
      context.out << fmt::format("  {} {} | {}\n", kind, id.value, metadata(found).name);
    };
    if (value.members.sequence) {
      member("sequence", *value.members.sequence);
    }
    for (const auto id : value.members.soundBanks) {
      member("sound-bank", id);
    }
    for (const auto id : value.members.samplePools) {
      member("sample-pool", id);
    }
    for (const auto id : value.members.miscAssets) {
      member("misc", id);
    }
    for (const auto& issue : value.issues) {
      context.out << fmt::format("  {} [{}]: {}\n", severityName(issue.severity), issue.code, issue.message);
    }
  };
  if (!args.empty()) {
    print(collection(snapshot, args[0]));
  } else {
    for (const auto& value : snapshot.collections()) {
      print(value);
    }
  }
}

void remove(Context& context, Args args) {
  const auto snapshot = context.session.snapshot();
  if (args[0] == "source") {
    std::vector<SourceId> ids;
    for (const auto& text : args.subspan(1)) {
      const SourceId id{number(text)};
      require(snapshot.source(id), "source", id.value);
      ids.push_back(id);
    }
    context.session.removeSources(ids);
  } else if (args[0] == "asset") {
    std::vector<AssetId> ids;
    for (const auto& text : args.subspan(1)) {
      ids.push_back(metadata(asset(snapshot, text)).id);
    }
    context.session.removeAssets(ids);
  } else {
    throw std::invalid_argument("remove expects source or asset followed by IDs");
  }
}

void create(Context& context, Args args) {
  const auto snapshot = context.session.snapshot();
  CollectionMembers members;
  for (const auto& text : args.subspan(1)) {
    const auto& value = asset(snapshot, text);
    const auto id = metadata(value).id;
    if (std::holds_alternative<SequenceProgramAsset>(value)) {
      if (members.sequence) {
        throw std::invalid_argument("a collection can contain only one sequence");
      }
      members.sequence = id;
    } else if (std::holds_alternative<SoundBankAsset>(value)) {
      members.soundBanks.push_back(id);
    } else if (std::holds_alternative<SamplePoolAsset>(value)) {
      members.samplePools.push_back(id);
    } else {
      members.miscAssets.push_back(id);
    }
  }
  const auto id = context.session.createUserCollection(args[0], std::move(members));
  context.out << fmt::format("Created collection {}: {}\n", id.value, args[0]);
}

std::string fieldText(const SourceField& field) {
  return std::visit(
      [&](const auto& value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return {};
        } else {
          if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            if (field.display == SourceValueDisplay::Hex || field.display == SourceValueDisplay::Address) {
              return fmt::format("0x{:x}", value);
            }
          }
          return fmt::format("{}", value);
        }
      },
      field.value);
}

void printAnnotation(std::ostream& out, const SourceAnnotation& annotation, size_t depth) {
  const std::string indent(depth * 2, ' ');
  out << fmt::format("{}{} | {} | {}\n", indent, rangeText(annotation.range), annotation.kind, annotation.label);
  if (!annotation.description.empty()) {
    out << indent << "  " << annotation.description << '\n';
  }
  for (const auto& field : annotation.fields) {
    out << indent << "  " << field.name << ": " << fieldText(field) << '\n';
  }
}

void tree(Context& context, Args args) {
  const u32 maxDepth = args.size() > 1 ? number(args[1]) : 4;
  const auto snapshot = context.session.snapshot();
  const auto& value = asset(snapshot, args[0]);
  const auto inspection = context.session.inspect(metadata(value).id);
  if (!inspection) {
    context.out << "Asset has no source annotations.\n";
    return;
  }
  // An explicit stack also handles deeply nested source trees without recursion.
  std::vector<std::pair<SourceAnnotationId, u32>> pending;
  for (const auto id : inspection->roots() | std::views::reverse) {
    pending.emplace_back(id, 0);
  }
  while (!pending.empty()) {
    const auto [id, depth] = pending.back();
    pending.pop_back();
    printAnnotation(context.out, *inspection->annotation(id), depth);
    if (depth < maxDepth) {
      for (const auto child : inspection->children(id) | std::views::reverse) {
        pending.emplace_back(child, depth + 1);
      }
    }
  }
}

void events(Context& context, Args args) {
  const auto snapshot = context.session.snapshot();
  const auto& value = asset(snapshot, args[0]);
  const auto* sequence = std::get_if<SequenceProgramAsset>(&value);
  if (sequence == nullptr) {
    throw std::invalid_argument("asset is not a sequence");
  }
  const auto trackIndex = number(args[1]);
  if (trackIndex >= sequence->program.tracks.size()) {
    throw std::invalid_argument("track index out of range");
  }
  const auto& commands = sequence->program.tracks[trackIndex].commands;
  const size_t count = args.size() > 2 ? std::min(number<size_t>(args[2]), commands.size()) : commands.size();
  for (size_t i = 0; i < count; ++i) {
    const auto& command = commands[i];
    context.out << fmt::format("command {} opcode=0x{:02x} ", i, command.opcode);
    if (const auto* annotation = snapshot.sourceMap().find(command.annotation)) {
      printAnnotation(context.out, *annotation, 0);
    } else {
      context.out << rangeText(command.range) << '\n';
    }
  }
  if (count < commands.size()) {
    context.out << "... " << commands.size() - count << " more commands\n";
  }
}

template <typename T, typename Print>
void printIndexed(const std::vector<T>& values, Args selection, Print print) {
  if (!selection.empty()) {
    const auto index = number(selection[0]);
    if (index >= values.size()) {
      throw std::invalid_argument("index out of range");
    }
    print(values[index], index);
  } else {
    for (size_t i = 0; i < values.size(); ++i) {
      print(values[i], i);
    }
  }
}

void instruments(Context& context, Args args) {
  const auto snapshot = context.session.snapshot();
  const auto* bank = std::get_if<SoundBankAsset>(&asset(snapshot, args[0]));
  if (bank == nullptr) {
    throw std::invalid_argument("asset is not a sound bank");
  }
  printIndexed(bank->instruments, args.subspan(1), [&](const Instrument& instrument, size_t index) {
    context.out << fmt::format("instrument {} | {} | {} regions", index, instrument.name, instrument.regions.size());
    if (instrument.identity) {
      context.out << fmt::format(" | identity {}:{}", instrument.identity->domain, instrument.identity->key);
    }
    if (instrument.explicitAddress) {
      context.out << fmt::format(" | bank {} program {}", instrument.explicitAddress->bank,
                                 instrument.explicitAddress->program);
    }
    context.out << '\n';
    if (args.size() > 1) {
      for (const auto& region : instrument.regions) {
        const auto sample = region.sample.empty() ? "none"
                            : region.sample.needsBinding()
                                ? fmt::format("unbound:{}", region.sample.index())
                                : fmt::format("{}:{}", region.sample.owner().value, region.sample.index());
        context.out << fmt::format("  keys {}-{} velocity {}-{} | sample {} | unity key {} | pan {} | {} dB\n",
                                   region.keyRange.low, region.keyRange.high, region.velocityRange.low,
                                   region.velocityRange.high, sample, region.unityKey, region.pan,
                                   region.attenuationDb);
      }
    }
  });
}

std::string_view codecName(AudioCodec codec) {
  switch (codec) {
    case AudioCodec::Unknown:
      return "unknown";
    case AudioCodec::PcmS8:
      return "pcm-s8";
    case AudioCodec::PcmS16:
      return "pcm-s16";
    case AudioCodec::SnesBrr:
      return "snes-brr";
    case AudioCodec::SnesDspNoise:
      return "snes-noise";
    case AudioCodec::NdsImaAdpcm:
      return "nds-ima-adpcm";
    case AudioCodec::NdsPsg:
      return "nds-psg";
    case AudioCodec::GbaDirectSound:
      return "gba-direct-sound";
    case AudioCodec::GbaPsg:
      return "gba-psg";
    case AudioCodec::GbaPsgWave:
      return "gba-psg-wave";
    case AudioCodec::PsxAdpcm:
      return "psx-adpcm";
    case AudioCodec::KonamiK053260Adpcm:
      return "konami-k053260-adpcm";
    case AudioCodec::KonamiK054539Adpcm:
      return "konami-k054539-adpcm";
    case AudioCodec::OkiAdpcm:
      return "oki-adpcm";
  }
  return "unknown";
}

void samples(Context& context, Args args) {
  const auto snapshot = context.session.snapshot();
  const auto& value = asset(snapshot, args[0]);
  const SamplePool* pool = nullptr;
  if (const auto* bank = std::get_if<SoundBankAsset>(&value)) {
    pool = &bank->localSamples;
  } else if (const auto* samplePool = std::get_if<SamplePoolAsset>(&value)) {
    pool = &samplePool->pool;
  }
  if (pool == nullptr) {
    throw std::invalid_argument("asset is not a sound bank or sample pool");
  }
  printIndexed(pool->samples, args.subspan(1), [&](const Sample& sample, size_t index) {
    context.out << fmt::format("sample {} | {} | {} | {} Hz | {} channels | {}\n", index, sample.name,
                               codecName(sample.codec), sample.sampleRate, sample.channels,
                               rangeText(sample.encodedData));
    if (args.size() > 1) {
      context.out << fmt::format("  pitch {} cents | {} dB | loop {} start {} length {}\n", sample.pitch.cents,
                                 sample.attenuationDb, sample.loop.enabled, sample.loop.start, sample.loop.length);
    }
  });
}

void read(Context& context, Args args) {
  const auto bytes = context.session.sources().bytes(SourceId{number(args[0])});
  const auto offset = number<size_t>(args[1]);
  const auto length = number<size_t>(args[2]);
  if (offset > bytes.size() || length > bytes.size() - offset) {
    throw std::invalid_argument("byte range exceeds source size");
  }
  const auto selected = bytes.subspan(offset, length);
  for (size_t row = 0; row < selected.size(); row += 16) {
    const auto line = selected.subspan(row, std::min(size_t{16}, selected.size() - row));
    context.out << fmt::format("{:08x}  ", offset + row);
    for (const auto byte : line) {
      context.out << fmt::format("{:02x} ", byte);
    }
    context.out << std::string((16 - line.size()) * 3, ' ') << " |";
    for (const auto byte : line) {
      context.out << (byte >= 32 && byte <= 126 ? static_cast<char>(byte) : '.');
    }
    context.out << "|\n";
  }
}

void writeFile(const std::filesystem::path& path, std::span<const u8> bytes) {
  std::ofstream file;
  file.exceptions(std::ios::failbit | std::ios::badbit);
  try {
    if (!path.parent_path().empty()) {
      std::filesystem::create_directories(path.parent_path());
    }
    file.open(path, std::ios::binary);
    if (!bytes.empty()) {
      file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    file.close();
  } catch (const std::exception& error) {
    throw std::runtime_error(fmt::format("cannot write '{}': {}", path.string(), error.what()));
  }
}

void dump(Context& context, Args args) {
  const auto bytes = context.session.sources().bytes(SourceId{number(args[0])});
  writeFile(args[1], bytes);
  context.out << fmt::format("Wrote {} ({} bytes)\n", args[1], bytes.size());
}

void Context::writeArtifacts(const std::filesystem::path& directory, std::span<const Artifact> artifacts) {
  if (artifacts.empty()) {
    error("export produced no artifacts");
  }
  for (const auto& artifact : artifacts) {
    diagnostics(artifact.diagnostics);
    if (artifact.bytes.empty()) {
      error("export produced no data for " + artifact.filename);
      continue;
    }
    try {
      const std::filesystem::path filename(artifact.filename);
      if (filename.empty() || filename.has_parent_path() || filename == "." || filename == "..") {
        throw std::runtime_error("invalid artifact filename: " + artifact.filename);
      }
      const auto path = directory / filename;
      writeFile(path, artifact.bytes);
      out << fmt::format("Wrote {} ({} bytes)\n", path.string(), artifact.bytes.size());
    } catch (const std::exception& failure) {
      error(failure.what());
    }
  }
}

void exportCollections(Context& context, Args args) {
  const auto request = parseExportOptions(args.subspan(2), ExportTarget::Collection);
  const auto snapshot = context.session.snapshot();
  const std::filesystem::path directory(args[1]);
  if (args[0] != "all") {
    context.writeArtifacts(directory, context.session.exportCollection(collection(snapshot, args[0]).id, request));
    return;
  }
  if (snapshot.collections().empty()) {
    throw std::invalid_argument("no collections to export");
  }
  // Export one collection at a time so a large archive does not retain every
  // decoded sample and artifact in memory. IDs keep duplicate titles separate.
  for (const auto& value : snapshot.collections()) {
    context.writeArtifacts(directory / fmt::format("collection-{}", value.id.value),
                           context.session.exportCollection(value.id, request));
  }
}

void exportAsset(Context& context, Args args) {
  const auto snapshot = context.session.snapshot();
  const auto id = metadata(asset(snapshot, args[0])).id;
  const auto kind = parseExportKind(args[2]);
  const auto target = kind == ExportKind::Midi  ? ExportTarget::Sequence
                      : kind == ExportKind::Wav ? ExportTarget::Samples
                                                : ExportTarget::SoundBank;
  const auto request = parseExportOptions(args.subspan(3), target);
  std::vector<Artifact> artifacts;
  switch (kind) {
    case ExportKind::Midi:
      artifacts.push_back(context.session.exportSequenceMidi(id, request.sequence));
      break;
    case ExportKind::SoundFont2:
      artifacts.push_back(context.session.exportSoundBank(id, SynthExportFormat::SoundFont2, request));
      break;
    case ExportKind::Dls:
      artifacts.push_back(context.session.exportSoundBank(id, SynthExportFormat::Dls, request));
      break;
    case ExportKind::Wav:
      artifacts = context.session.exportSamples(id);
      break;
  }
  context.writeArtifacts(args[1], artifacts);
}

void stitch(Context& context, Args args) {
  const auto snapshot = context.session.snapshot();
  std::vector<CollectionId> ids;
  size_t i = 1;
  for (; i < args.size() && !args[i].starts_with("--"); ++i) {
    ids.push_back(collection(snapshot, args[i]).id);
  }
  if (ids.empty()) {
    throw std::invalid_argument("stitch requires collection IDs in playback order");
  }
  const auto request = parseExportOptions(args.subspan(i), ExportTarget::Stitch);
  auto result = context.session.stitchCollections(ids, request);
  const std::array artifacts{std::move(result.midi), std::move(result.soundFont)};
  context.writeArtifacts(args[0], artifacts);
}

void diagnostics(Context& context, Args) {
  const auto snapshot = context.session.snapshot();
  for (const auto& diagnostic : snapshot.diagnostics()) {
    printDiagnostic(context.out, diagnostic);
  }
}

void formats(Context& context, Args) {
  for (const auto& module : context.session.formats().modules()) {
    context.out << "format: " << module.name << '\n';
  }
  for (const auto& extractor : context.session.formats().extractors()) {
    context.out << "extractor: " << extractor.name << '\n';
  }
}

void help(Context& context, Args args);

struct Command {
  std::string_view name;
  std::string_view arguments;
  std::string_view description;
  size_t minArgs;
  size_t maxArgs;
  void (*run)(Context&, Args);
};

constexpr size_t unlimited = std::numeric_limits<size_t>::max();
constexpr Command commands[] = {
    {"help", "[command]", "Show commands or command usage", 0, 1, help},
    {"load", "<path>...", "Load and scan files into this session", 1, unlimited, load},
    {"sources", "[source-id]", "List sources or inspect one source", 0, 1, sources},
    {"assets", "[asset-id]", "List assets or inspect one asset", 0, 1, assets},
    {"collections", "[collection-id]", "Show collections, members, and issues", 0, 1, collections},
    {"remove", "<source|asset> <id>...", "Remove sources with their children, or selected assets", 2, unlimited,
     remove},
    {"create", "<name> <asset-id>...", "Create a collection from selected assets", 2, unlimited, create},
    {"tree", "<asset-id> [depth]", "Show source annotations and fields (default depth: 4)", 1, 2, tree},
    {"events", "<asset-id> <track-index> [limit]", "Show decoded sequence commands in source order", 2, 3, events},
    {"instruments", "<asset-id> [instrument-index]", "List instruments or inspect their regions", 1, 2, instruments},
    {"samples", "<asset-id> [sample-index]", "List samples or inspect loop and tuning data", 1, 2, samples},
    {"read", "<source-id> <offset> <length>", "Show source bytes in hex", 3, 3, read},
    {"dump", "<source-id> <path>", "Write the original bytes of a source", 2, 2, dump},
    {"export", "<collection-id|all> <directory> [formats...] [options]", "Export collections (default: midi)", 2,
     unlimited, exportCollections},
    {"export-asset", "<asset-id> <directory> <midi|sf2|dls|wav> [options]", "Export one sequence, bank, or sample pool",
     3, unlimited, exportAsset},
    {"stitch", "<directory> <collection-id>... [options]", "Join collections in order into MIDI and SF2", 2, unlimited,
     stitch},
    {"diagnostics", "", "Show scan diagnostics", 0, 0, diagnostics},
    {"formats", "", "List registered formats and extractors", 0, 0, formats},
    {"quit", "", "End the session", 0, 0, nullptr},
    {"exit", "", "End the session", 0, 0, nullptr},
};

const Command& findCommand(std::string_view name) {
  const auto found = std::ranges::find(commands, name, &Command::name);
  if (found == std::end(commands)) {
    throw std::invalid_argument(fmt::format("unknown command '{}'; type 'help'", name));
  }
  return *found;
}

void printExportHelp(std::ostream& out) {
  out << "Collection formats: midi, sf2, dls, wav, all. Combine formats (e.g. midi sf2); default: midi.\n"
         "Export all writes a collection-<id> subdirectory for each collection.\n"
         "WAV exports individual samples. Existing output files are replaced.\n\n";
  printExportOptions(out);
}

void help(Context& context, Args args) {
  if (args.empty()) {
    printHelp(context.out);
    return;
  }
  const auto& command = findCommand(args[0]);
  context.out << fmt::format("{} {}\n{}\n", command.name, command.arguments, command.description);
  if (command.name == "export" || command.name == "export-asset" || command.name == "stitch") {
    printExportHelp(context.out);
  }
}

std::vector<std::string> tokenize(std::string_view line) {
  std::vector<std::string> args;
  std::string token;
  char quote = 0;
  bool started = false;
  for (const char ch : line) {
    if (quote != 0) {
      if (ch == quote) {
        quote = 0;
      } else {
        token += ch;
      }
    } else if (ch == '\'' || ch == '"') {
      quote = ch;
      started = true;
    } else if (std::isspace(static_cast<unsigned char>(ch))) {
      if (started) {
        args.push_back(std::move(token));
        token.clear();
        started = false;
      }
    } else {
      token += ch;
      started = true;
    }
  }
  if (quote != 0) {
    throw std::invalid_argument("unterminated quote");
  }
  if (started) {
    args.push_back(std::move(token));
  }
  return args;
}

}  // namespace

CommandResult execute(Session& session, Args args, std::ostream& output, std::ostream& errors) {
  Context context{session, output, errors};
  try {
    if (args.empty()) {
      return CommandResult::Success;
    }
    const auto& command = findCommand(args[0]);
    args = args.subspan(1);
    if (args.size() < command.minArgs || args.size() > command.maxArgs) {
      throw std::invalid_argument(fmt::format("usage: {} {}", command.name, command.arguments));
    }
    if (command.run == nullptr) {
      return CommandResult::Exit;
    }
    command.run(context, args);
  } catch (const std::exception& error) {
    context.error(error.what());
  }
  return context.failed ? CommandResult::Error : CommandResult::Success;
}

CommandResult executeLine(Session& session, std::string_view line, std::ostream& output, std::ostream& errors) {
  try {
    return execute(session, tokenize(line), output, errors);
  } catch (const std::exception& error) {
    errors << "error: " << error.what() << '\n';
    return CommandResult::Error;
  }
}

void printHelp(std::ostream& output) {
  output << "Usage: vgmtrans-shell [-c COMMAND]... [--] [FILE...]\n"
            "Load files, then run commands. Without -c, read commands from the terminal or stdin.\n"
            "Scripts stop on the first failed command and exit with status 1.\n\n";
  for (const auto& command : commands) {
    output << fmt::format("  {} {}\n      {}\n", command.name, command.arguments, command.description);
  }
  output << "\nUse IDs printed by sources, assets, and collections; IDs remain stable within a session.\n"
            "Track, instrument, and sample indexes start at 0. Numbers accept decimal or 0x hex.\n"
            "Quote paths and names with spaces using single or double quotes; backslashes are literal.\n\n";
  printExportHelp(output);
}

std::vector<std::string> completeCommand(std::string_view prefix) {
  std::vector<std::string> matches;
  for (const auto& command : commands) {
    if (command.name.starts_with(prefix)) {
      matches.emplace_back(command.name);
    }
  }
  return matches;
}

}  // namespace vgmtrans::shell
