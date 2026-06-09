/**
 * VGMTrans (c) - 2002-2026
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "commands.h"

#include "base/Types.h"
#include "DBGVGMRoot.h"
#include "core/Model.h"
#include "core/ProjectSession.h"
#include "formats/ValueFormats.h"
#include "RawFile.h"
#include "SeqTrack.h"
#include "StitchExport.h"
#include "VGMColl.h"
#include "VGMExport.h"
#include "VGMFile.h"
#include "VGMInstrSet.h"
#include "VGMMiscFile.h"
#include "VGMRgn.h"
#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "VGMSeq.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <system_error>

#include <fmt/color.h>
#include <fmt/format.h>

std::map<std::string, Command> commandRegistry;

namespace {

void printItemTree(VGMItem* item, int depth, int maxDepth) {
  if (depth > maxDepth)
    return;
  std::string indent(depth * 2, ' ');
  fmt::print("{}[0x{:x}:0x{:x}] {}\n", indent, item->offset(), item->length(), item->name());
  for (auto* child : item->children()) {
    printItemTree(child, depth + 1, maxDepth);
  }
}

template <typename T>
void listVGMFiles() {
  auto files = dbgRoot.getLoadedFiles();
  int count = 0;
  for (size_t i = 0; i < files.size(); ++i) {
    if (dynamic_cast<T*>(files[i])) {
      fmt::print(
          "[{}] [{}:{}] {} ({})\n", fmt::styled(fmt::format("#{}", i), fmt::fg(fmt::color::cyan)),
          fmt::styled(fmt::format("0x{:x}", files[i]->startOffset()), fmt::fg(fmt::color::yellow)),
          fmt::styled(fmt::format("0x{:x}", files[i]->size()), fmt::fg(fmt::color::yellow)),
          files[i]->name(), fmt::styled(files[i]->formatName(), fmt::fg(fmt::color::dim_gray)));
      count++;
    }
  }
  if (count == 0) {
    fmt::println("No matching files found.");
  }
}

void listAllVGMFiles() {
  auto files = dbgRoot.getLoadedFiles();
  if (files.empty()) {
    fmt::println("No VGM files loaded.");
    return;
  }
  for (size_t i = 0; i < files.size(); ++i) {
    fmt::print(
        "[{}] [{}:{}] {} ({})\n", fmt::styled(fmt::format("#{}", i), fmt::fg(fmt::color::cyan)),
        fmt::styled(fmt::format("0x{:x}", files[i]->startOffset()), fmt::fg(fmt::color::yellow)),
        fmt::styled(fmt::format("0x{:x}", files[i]->size()), fmt::fg(fmt::color::yellow)),
        files[i]->name(), fmt::styled(files[i]->formatName(), fmt::fg(fmt::color::dim_gray)));
  }
}

VGMFile* getVGMFile(const std::string& indexStr) {
  try {
    int idx = std::stoi(indexStr);
    auto files = dbgRoot.getLoadedFiles();
    if (idx >= 0 && static_cast<size_t>(idx) < files.size()) {
      return files[idx];
    }
  } catch (...) {
  }

  fmt::println("Invalid index");
  return nullptr;
}

RawFile* getRawFile(const std::string& indexStr) {
  try {
    int idx = std::stoi(indexStr);
    auto rawfiles = dbgRoot.rawFiles();
    if (idx >= 0 && static_cast<size_t>(idx) < rawfiles.size()) {
      return rawfiles[idx];
    }
  } catch (...) {
  }

  fmt::println("Invalid index");
  return nullptr;
}

VGMColl* getVGMColl(const std::string& indexStr) {
  try {
    int idx = std::stoi(indexStr);
    auto colls = dbgRoot.vgmColls();
    if (idx >= 0 && static_cast<size_t>(idx) < colls.size()) {
      return colls[idx];
    }
  } catch (...) {
  }

  fmt::println("Invalid collection index");
  return nullptr;
}

void printHexDump(const u8* data, size_t length) {
  for (size_t i = 0; i < length; i += 16) {
    fmt::print("{:08x}: ", i);
    for (size_t j = 0; j < 16; ++j) {
      if (i + j < length) {
        fmt::print("{:02x} ", data[i + j]);
      } else {
        fmt::print("   ");
      }
    }
    fmt::print(" ");
    for (size_t j = 0; j < 16 && i + j < length; ++j) {
      char c = data[i + j];
      fmt::print("{}", (c >= 32 && c < 127) ? c : '.');
    }
    fmt::print("\n");
  }
}

void writeToFile(const std::string& path, const char* data, size_t size) {
  std::ofstream file(path, std::ios::binary);
  if (file) {
    file.write(data, static_cast<std::streamsize>(size));
    fmt::println("Wrote {} bytes to {}", size, path);
  } else {
    fmt::println("Failed to open {} for writing", path);
  }
}

bool ensureDirectory(const std::filesystem::path& dir) {
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  if (error) {
    fmt::println("Failed to create {}: {}", dir.string(), error.message());
    return false;
  }
  if (!std::filesystem::is_directory(dir, error)) {
    fmt::println("{} is not a directory", dir.string());
    return false;
  }
  return true;
}

bool writeValueArtifact(const std::filesystem::path& path, std::span<const u8> bytes) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    fmt::println("Failed to open {} for writing", path.string());
    return false;
  }

  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    fmt::println("Failed to write {}", path.string());
    return false;
  }

  fmt::println("Wrote {} bytes to {}", bytes.size(), path.string());
  return true;
}

void printChildren(VGMItem* item) {
  for (auto* child : item->children()) {
    fmt::print("  [0x{:x}:0x{:x}] {}\n", child->offset(), child->length(), child->name());
  }
}

const char* valueAssetKindName(const vgmtrans::core::Asset& asset) {
  using namespace vgmtrans::core;
  if (std::holds_alternative<SequenceAsset>(asset)) {
    return "sequence";
  }
  if (std::holds_alternative<InstrumentBankAsset>(asset)) {
    return "instrument-bank";
  }
  if (std::holds_alternative<SampleCollectionAsset>(asset)) {
    return "sample-collection";
  }
  return "misc";
}

const char* valueSeverityName(vgmtrans::core::Severity severity) {
  using vgmtrans::core::Severity;
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

void printValueDiagnostic(const vgmtrans::core::Diagnostic& diagnostic) {
  fmt::print("[{}] {}", valueSeverityName(diagnostic.severity), diagnostic.message);
  if (diagnostic.range) {
    fmt::print(" (source #{} 0x{:x}:0x{:x})", diagnostic.range->source.value,
               diagnostic.range->offset, diagnostic.range->size);
  }
  fmt::print("\n");
}

std::vector<u8> valueBytesForRawFile(const RawFile& file) {
  const auto* begin = reinterpret_cast<const u8*>(file.data());
  return std::vector<u8>(begin, begin + file.size());
}

vgmtrans::core::ProjectSession valueSessionForRawFile(const RawFile& file) {
  vgmtrans::core::ProjectSession session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(vgmtrans::core::SourceFile{
                        .name = file.name(),
                        .path = file.path(),
                        .size = static_cast<u64>(file.size()),
                    },
                    valueBytesForRawFile(file));
  return session;
}

void printValueProjectSummary(const vgmtrans::core::Project& project) {
  fmt::println("Sources: {}  Assets: {}  Collections: {}  Diagnostics: {}",
               project.sources.size(), project.assets.size(), project.collections.size(),
               project.diagnostics.size());

  for (const auto& diagnostic : project.diagnostics) {
    printValueDiagnostic(diagnostic);
  }

  for (size_t i = 0; i < project.assets.size(); ++i) {
    const auto& asset = project.assets[i];
    const auto& meta = vgmtrans::core::metadata(asset);
    fmt::print("asset #{} [{}] id={} format={} name='{}' range=0x{:x}:0x{:x}",
               i, valueAssetKindName(asset), meta.id.value, meta.format, meta.name, meta.range.offset,
               meta.range.size);
    if (std::holds_alternative<vgmtrans::core::SequenceAsset>(asset)) {
      const auto& sequence = std::get<vgmtrans::core::SequenceAsset>(asset);
      fmt::print(" tracks={}", sequence.program.tracks.size());
    } else if (std::holds_alternative<vgmtrans::core::InstrumentBankAsset>(asset)) {
      const auto& bank = std::get<vgmtrans::core::InstrumentBankAsset>(asset);
      fmt::print(" instruments={}", bank.bank.instruments.size());
    } else if (std::holds_alternative<vgmtrans::core::SampleCollectionAsset>(asset)) {
      const auto& samples = std::get<vgmtrans::core::SampleCollectionAsset>(asset);
      fmt::print(" samples={}", samples.samples.samples.size());
    }
    fmt::print("\n");
  }

  for (size_t i = 0; i < project.collections.size(); ++i) {
    const auto& collection = project.collections[i];
    fmt::println("collection #{} id={} name='{}' sequence={} instrumentBanks={} sampleCollections={}",
                 i, collection.id.value, collection.name,
                 collection.sequence ? std::to_string(collection.sequence->value) : std::string("-"),
                 collection.instrumentBanks.size(), collection.sampleCollections.size());
  }
}

void printValueItemTree(const vgmtrans::core::ItemTree& tree,
                        vgmtrans::core::ItemId id,
                        int depth,
                        int maxDepth) {
  if (depth > maxDepth) {
    return;
  }

  const auto* item = vgmtrans::core::itemById(tree, id);
  if (item == nullptr) {
    return;
  }

  const std::string indent(static_cast<size_t>(depth) * 2, ' ');
  fmt::print("{}#{} [{}] {} 0x{:x}:0x{:x}", indent, item->id.value, item->detailKind, item->name,
             item->range.offset, item->range.size);
  if (!item->description.empty()) {
    fmt::print(" - {}", item->description);
  }
  fmt::print("\n");

  for (const auto child : item->children) {
    printValueItemTree(tree, child, depth + 1, maxDepth);
  }
}

std::optional<vgmtrans::core::ExportKind> valueExportKindFromString(std::string kind) {
  std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  if (kind == "midi" || kind == "mid") {
    return vgmtrans::core::ExportKind::Midi;
  }
  if (kind == "sf2" || kind == "soundfont" || kind == "soundfont2") {
    return vgmtrans::core::ExportKind::SoundFont2;
  }
  if (kind == "dls") {
    return vgmtrans::core::ExportKind::Dls;
  }
  if (kind == "wav" || kind == "wave") {
    return vgmtrans::core::ExportKind::Wav;
  }
  return std::nullopt;
}

std::optional<vgmtrans::core::ExportRequest> valueExportRequestFromArgs(
    const std::vector<std::string>& args,
    size_t kindArgIndex) {
  vgmtrans::core::ExportRequest request{
      .kinds = {
          vgmtrans::core::ExportKind::Midi,
          vgmtrans::core::ExportKind::SoundFont2,
          vgmtrans::core::ExportKind::Dls,
          vgmtrans::core::ExportKind::Wav,
      },
      .loopPolicy = vgmtrans::core::LoopPolicy::PlayOnce,
  };

  if (args.size() <= kindArgIndex) {
    return request;
  }

  std::string kindName = args[kindArgIndex];
  std::transform(kindName.begin(), kindName.end(), kindName.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (kindName == "all") {
    return request;
  }

  const auto kind = valueExportKindFromString(kindName);
  if (!kind) {
    fmt::println("Unknown value export kind '{}'. Use all, midi, sf2, dls, or wav.", args[kindArgIndex]);
    return std::nullopt;
  }

  request.kinds = {*kind};
  return request;
}

std::string valueSafePathPart(std::string name) {
  if (name.empty()) {
    return "unnamed";
  }

  for (char& ch : name) {
    const auto value = static_cast<unsigned char>(ch);
    if (std::iscntrl(value) || ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
        ch == '<' || ch == '>' || ch == '|') {
      ch = '_';
    }
  }
  return name;
}

std::string valueCollectionDirectoryName(size_t index, const vgmtrans::core::Collection& collection) {
  return fmt::format("{:03}-collection-{}-{}", index, collection.id.value,
                     valueSafePathPart(collection.name));
}

size_t writeValueArtifacts(const std::filesystem::path& dir, std::span<const vgmtrans::core::Artifact> artifacts) {
  size_t written = 0;
  for (const auto& artifact : artifacts) {
    for (const auto& diagnostic : artifact.diagnostics) {
      printValueDiagnostic(diagnostic);
    }

    if (artifact.bytes.empty()) {
      fmt::println("Skipped empty artifact {}", artifact.filename);
      continue;
    }

    if (writeValueArtifact(dir / artifact.filename, artifact.bytes)) {
      ++written;
    }
  }
  return written;
}

}  // namespace

void cmd_help(const std::vector<std::string>&);

void printCmdUsage(const std::string& noun) {
  auto it = commandRegistry.find(noun);
  if (it != commandRegistry.end()) {
    const auto& cmd = it->second;
    fmt::println("Usage: {}", cmd.usage());
    if (!cmd.verbs.empty()) {
      fmt::println("{}", cmd.detailedDescription());
    }
  } else {
    fmt::println("Unknown noun: {}", noun);
  }
}

void printVerbUsage(const std::string& noun, const std::string& verbName) {
  auto it = commandRegistry.find(noun);
  if (it != commandRegistry.end()) {
    fmt::println("Usage: {}", it->second.verbUsage(verbName));
  }
}

bool dispatchVerb(const std::string& noun, const std::vector<std::string>& args) {
  auto cmdIt = commandRegistry.find(noun);
  if (cmdIt == commandRegistry.end()) {
    fmt::println("Unknown command: {}", noun);
    return false;
  }

  const auto& cmd = cmdIt->second;

  if (args.size() < 2) {
    printCmdUsage(noun);
    return false;
  }

  const std::string& verbName = args[1];
  for (const auto& verb : cmd.verbs) {
    if (verb.name == verbName) {
      if (static_cast<int>(args.size()) < verb.minArgs) {
        printVerbUsage(noun, verbName);
        return false;
      }
      if (verb.handler) {
        verb.handler(args);
        return true;
      }
    }
  }

  printCmdUsage(noun);
  return false;
}

// ============================================================================
// Verb Handlers
// ============================================================================

void rawfile_list(const std::vector<std::string>&) {
  auto rawfiles = dbgRoot.rawFiles();
  if (rawfiles.empty()) {
    fmt::println("No raw files loaded.");
    return;
  }
  for (size_t i = 0; i < rawfiles.size(); ++i) {
    fmt::print("#{} 0x0:0x{:x} {}\n", i, rawfiles[i]->size(), rawfiles[i]->name());
  }
}

void rawfile_info(const std::vector<std::string>& args) {
  RawFile* file = getRawFile(args[2]);
  if (file) {
    fmt::print("Name: {}\nPath: {}\nDetails: {} bytes\n", file->name(), file->path().string(),
               file->size());
  }
}

void rawfile_read(const std::vector<std::string>& args) {
  try {
    RawFile* file = getRawFile(args[2]);
    if (file) {
      size_t offset = std::stoul(args[3], nullptr, 16);
      size_t length = std::stoul(args[4], nullptr, 16);
      if (offset + length <= file->size()) {
        const u8* data = reinterpret_cast<const u8*>(file->data());
        printHexDump(data + offset, length);
      } else {
        fmt::println("Range out of bounds");
      }
    }
  } catch (...) {
    fmt::println("Invalid arguments");
  }
}

void rawfile_dump(const std::vector<std::string>& args) {
  RawFile* file = getRawFile(args[2]);
  if (file) {
    writeToFile(args[3], file->data(), file->size());
  }
}

void vgmfile_list(const std::vector<std::string>&) {
  listAllVGMFiles();
}

void vgmfile_info(const std::vector<std::string>& args) {
  VGMFile* file = getVGMFile(args[2]);
  if (file) {
    fmt::print("Name: {}\nFormat: {}\nID: {}\nOffset: 0x{:x}\nLength: 0x{:x}\n", file->name(),
               file->formatName(), file->id(), file->startOffset(), file->size());
  }
}

void vgmfile_tree(const std::vector<std::string>& args) {
  VGMFile* file = getVGMFile(args[2]);
  if (file) {
    int maxDepth = 2;
    if (args.size() > 3) {
      try {
        maxDepth = std::stoi(args[3]);
      } catch (...) {
      }
    }
    printItemTree(file, 0, maxDepth);
  }
}

void vgmfile_read(const std::vector<std::string>& args) {
  try {
    VGMFile* file = getVGMFile(args[2]);
    if (file) {
      size_t relOffset = std::stoul(args[3], nullptr, 16);
      size_t length = std::stoul(args[4], nullptr, 16);
      if (relOffset + length <= file->size()) {
        const char* rawData = file->rawFile()->data();
        const u8* data = reinterpret_cast<const u8*>(rawData + file->startOffset());
        printHexDump(data + relOffset, length);
      } else {
        fmt::println("Range out of bounds (file size: 0x{:x})", file->size());
      }
    }
  } catch (...) {
    fmt::println("Invalid arguments");
  }
}

void vgmfile_dump(const std::vector<std::string>& args) {
  VGMFile* file = getVGMFile(args[2]);
  if (file) {
    const char* rawData = file->rawFile()->data();
    const char* fileData = rawData + file->startOffset();
    writeToFile(args[3], fileData, file->size());
  }
}

void collection_list(const std::vector<std::string>&) {
  auto colls = dbgRoot.vgmColls();
  if (colls.empty()) {
    fmt::println("No collections loaded.");
    return;
  }
  for (size_t i = 0; i < colls.size(); ++i) {
    fmt::print("[#{}] {}\n", i, colls[i]->name());
  }
}

void collection_info(const std::vector<std::string>& args) {
  VGMColl* coll = getVGMColl(args[2]);
  if (coll) {
    fmt::println("Name: {}", coll->name());
    VGMSeq* seq = coll->seq();
    if (seq) {
      fmt::print("  Seq: [0x{:x}:0x{:x}] {}\n", seq->startOffset(), seq->size(), seq->name());
    }
    const auto& instrSets = coll->instrSets();
    for (size_t i = 0; i < instrSets.size(); ++i) {
      fmt::print("  InstrSet #{} [0x{:x}:0x{:x}] {}\n", i, instrSets[i]->startOffset(),
                 instrSets[i]->size(), instrSets[i]->name());
    }
    const auto& sampColls = coll->sampColls();
    for (size_t i = 0; i < sampColls.size(); ++i) {
      fmt::print("  SampColl #{} [0x{:x}:0x{:x}] {}\n", i, sampColls[i]->startOffset(),
                 sampColls[i]->size(), sampColls[i]->name());
    }
    const auto& miscFiles = coll->miscFiles();
    for (size_t i = 0; i < miscFiles.size(); ++i) {
      fmt::print("  Misc #{} [0x{:x}:0x{:x}] {}\n", i, miscFiles[i]->startOffset(),
                 miscFiles[i]->size(), miscFiles[i]->name());
    }
  }
}

void collection_export(const std::vector<std::string>& args) {
  VGMColl* coll = getVGMColl(args[2]);
  if (!coll)
    return;

  std::filesystem::path dir = args[3];
  if (!std::filesystem::exists(dir)) {
    std::filesystem::create_directories(dir);
  }

  fmt::println("Exporting collection '{}' to {}...", coll->name(), dir.string());
  conversion::saveAs<conversion::Target::MIDI | conversion::Target::SF2>(*coll, dir);
}

void instrumentset_list(const std::vector<std::string>&) {
  listVGMFiles<VGMInstrSet>();
}

void instrumentset_info(const std::vector<std::string>& args) {
  VGMFile* file = getVGMFile(args[2]);
  if (!file)
    return;

  VGMInstrSet* instrSet = dynamic_cast<VGMInstrSet*>(file);
  if (!instrSet) {
    fmt::println("Not an instrument set.");
    return;
  }

  fmt::print("Name: {}\nFormat: {}\nOffset: 0x{:x}\nLength: 0x{:x}\n", instrSet->name(),
             instrSet->formatName(), instrSet->startOffset(), instrSet->size());
  fmt::println("Instruments:");
  for (size_t i = 0; i < instrSet->instrCount(); ++i) {
    VGMInstr* instr = instrSet->instr(i);
    fmt::print("  Instr #{}: Bank {} Num {} - {}\n", i, instr->bank, instr->instrNum,
               instr->name());
  }
}

void instrumentset_inspect(const std::vector<std::string>& args) {
  VGMFile* file = getVGMFile(args[2]);
  if (!file)
    return;

  VGMInstrSet* instrSet = dynamic_cast<VGMInstrSet*>(file);
  if (!instrSet) {
    fmt::println("Not an instrument set.");
    return;
  }

  try {
    int instrIdx = std::stoi(args[3]);
    if (instrIdx >= 0 && static_cast<size_t>(instrIdx) < instrSet->instrCount()) {
      VGMInstr* instr = instrSet->instr(static_cast<size_t>(instrIdx));
      fmt::println("Regions for Instrument #{}:", instrIdx);
      const auto& regions = instr->regions();
      for (size_t i = 0; i < regions.size(); ++i) {
        VGMRgn* rgn = regions[i];
        fmt::print("  [Rgn #{}] [0x{:x}:0x{:x}] Key {}-{} Vel {}-{} Samp {} Unity {} ({})\n"
                   "    ADSR: A {:.3f}s D {:.3f}s S {:.1f}% R {:.3f}s\n",
                   i, rgn->offset(), rgn->length(), rgn->keyLow, rgn->keyHigh, rgn->velLow,
                   rgn->velHigh, rgn->sampNum, static_cast<int>(rgn->unityKey),
                   MidiEvent::getNoteName(rgn->unityKey), rgn->attack_time, rgn->decay_time,
                   rgn->sustain_level * 100.0, rgn->release_time);
      }
    } else {
      fmt::println("Instrument index out of bounds");
    }
  } catch (...) {
    fmt::println("Invalid instrument index");
  }
}

void instrumentset_export(const std::vector<std::string>& args) {
  VGMFile* file = getVGMFile(args[2]);
  if (!file)
    return;

  VGMInstrSet* instrSet = dynamic_cast<VGMInstrSet*>(file);
  if (!instrSet) {
    fmt::println("Not an instrument set.");
    return;
  }

  std::filesystem::path path = args[3];
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".dls") {
    fmt::println("Exporting instrument set '{}' to {}...", instrSet->name(), path.string());
    if (conversion::saveAsDLS(*instrSet, path)) {
      fmt::println("Done.");
    } else {
      fmt::println("Failed to export DLS.");
    }
  } else if (ext == ".sf2") {
    fmt::println("Exporting instrument set '{}' to {}...", instrSet->name(), path.string());
    if (conversion::saveAsSF2(*instrSet, path)) {
      fmt::println("Done.");
    } else {
      fmt::println("Failed to export SF2.");
    }
  } else {
    fmt::println("Unsupported extension: {}. Use .dls or .sf2", ext);
  }
}

void samplecollection_list(const std::vector<std::string>&) {
  listVGMFiles<VGMSampColl>();
}

void samplecollection_info(const std::vector<std::string>& args) {
  VGMFile* file = getVGMFile(args[2]);
  if (!file)
    return;

  VGMSampColl* sampColl = dynamic_cast<VGMSampColl*>(file);
  if (!sampColl) {
    fmt::println("Not a sample collection.");
    return;
  }

  fmt::print("Name: {}\nFormat: {}\nOffset: 0x{:x}\nLength: 0x{:x}\n", sampColl->name(),
             sampColl->formatName(), sampColl->startOffset(), sampColl->size());
  fmt::println("Samples ({}):", sampColl->sampleCount());
  for (size_t i = 0; i < sampColl->sampleCount(); ++i) {
    VGMSamp* samp = sampColl->sample(i);
    fmt::print("  [{}] Def [{}:{}] Data [{}:{}] {} ({} Hz)\n",
               fmt::styled(fmt::format("#{}", i), fmt::fg(fmt::color::cyan)),
               fmt::styled(fmt::format("0x{:x}", samp->offset()), fmt::fg(fmt::color::yellow)),
               fmt::styled(fmt::format("0x{:x}", samp->length()), fmt::fg(fmt::color::yellow)),
               fmt::styled(fmt::format("0x{:x}", samp->dataOff), fmt::fg(fmt::color::yellow)),
               fmt::styled(fmt::format("0x{:x}", samp->dataLength), fmt::fg(fmt::color::yellow)),
               samp->name(), samp->rate);
  }
}

void samplecollection_inspect(const std::vector<std::string>& args) {
  VGMFile* file = getVGMFile(args[2]);
  if (!file)
    return;

  VGMSampColl* sampColl = dynamic_cast<VGMSampColl*>(file);
  if (!sampColl) {
    fmt::println("Not a sample collection.");
    return;
  }

  try {
    int sampIdx = std::stoi(args[3]);
    if (sampIdx >= 0 && static_cast<size_t>(sampIdx) < sampColl->sampleCount()) {
      VGMSamp* samp = sampColl->sample(sampIdx);
      fmt::print("{}\n",
                 fmt::styled(fmt::format("Sample #{} Information:", sampIdx), fmt::emphasis::bold));
      fmt::print("  {:<12} {}\n", "Name:", samp->name());
      fmt::print("  {:<12} [{}:{}]\n", "Definition:",
                 fmt::styled(fmt::format("0x{:x}", samp->offset()), fmt::fg(fmt::color::yellow)),
                 fmt::styled(fmt::format("0x{:x}", samp->length()), fmt::fg(fmt::color::yellow)));
      fmt::print("  {:<12} [{}:{}] ({} bytes)\n", "Data:",
                 fmt::styled(fmt::format("0x{:x}", samp->dataOff), fmt::fg(fmt::color::yellow)),
                 fmt::styled(fmt::format("0x{:x}", samp->dataLength), fmt::fg(fmt::color::yellow)),
                 fmt::styled(std::to_string(samp->dataLength), fmt::fg(fmt::color::yellow)));

      u32 totalSamples = 0;
      if (samp->bytesPerSample() > 0) {
        totalSamples = samp->uncompressedSize() / samp->bytesPerSample();
      }
      fmt::print("  {:<12} {} samples\n",
                 "Size:", fmt::styled(std::to_string(totalSamples), fmt::fg(fmt::color::yellow)));
      fmt::print("  {:<12} {} Hz  Channels: {}  BPS: {}\n", "Format:", samp->rate, samp->channels,
                 samp->bitsPerSample());
      fmt::print("  {:<12} Unity Key: {} ({})  Fine Tune: {}\n", "",
                 static_cast<int>(samp->unityKey), MidiEvent::getNoteName(samp->unityKey),
                 samp->fineTune);

      if (samp->loopStatus() != 0) {
        fmt::print("  {:<12} Start 0x{:x}  Length 0x{:x}  Measure: {}\n",
                   "Loop:", samp->loop.loopStart, samp->loop.loopLength,
                   static_cast<int>(samp->loop.loopStartMeasure));
      } else {
        fmt::print("  {:<12} None\n", "Loop:");
      }
    } else {
      fmt::println("Sample index out of bounds");
    }
  } catch (...) {
    fmt::println("Invalid sample index");
  }
}

void samplecollection_export(const std::vector<std::string>& args) {
  VGMFile* file = getVGMFile(args[2]);
  if (!file)
    return;

  VGMSampColl* sampColl = dynamic_cast<VGMSampColl*>(file);
  if (!sampColl) {
    fmt::println("Not a sample collection.");
    return;
  }

  std::filesystem::path dir = args[3];
  if (!std::filesystem::exists(dir)) {
    std::filesystem::create_directories(dir);
  }

  fmt::println("Exporting {} samples to {}...", sampColl->sampleCount(), dir.string());
  conversion::saveAllAsWav(*sampColl, dir);
}

void samplecollection_dump(const std::vector<std::string>& args) {
  VGMFile* file = getVGMFile(args[2]);
  if (!file)
    return;

  VGMSampColl* sampColl = dynamic_cast<VGMSampColl*>(file);
  if (!sampColl) {
    fmt::println("Not a sample collection.");
    return;
  }

  try {
    int sampIdx = std::stoi(args[3]);
    if (sampIdx < 0 || static_cast<size_t>(sampIdx) >= sampColl->sampleCount()) {
      fmt::println("Sample index out of bounds");
      return;
    }
    VGMSamp* samp = sampColl->sample(sampIdx);
    const char* rawData = samp->rawFile()->data();
    writeToFile(args[4], rawData + samp->dataOff, samp->dataLength);
  } catch (...) {
    fmt::println("Invalid arguments");
  }
}

void sequence_list(const std::vector<std::string>&) {
  listVGMFiles<VGMSeq>();
}

void sequence_events(const std::vector<std::string>& args) {
  VGMFile* file = getVGMFile(args[2]);
  if (!file)
    return;

  VGMSeq* seq = dynamic_cast<VGMSeq*>(file);
  if (!seq) {
    fmt::println("Not a sequence file.");
    return;
  }

  try {
    int trackIdx = std::stoi(args[3]);
    if (trackIdx >= 0 && static_cast<size_t>(trackIdx) < seq->trackCount()) {
      SeqTrack* track = seq->track(static_cast<size_t>(trackIdx));
      fmt::println("Events for Track {}:", trackIdx);
      printChildren(track);
    } else {
      fmt::println("Track index out of bounds");
    }
  } catch (...) {
    fmt::println("Invalid track index");
  }
}

void sequence_export(const std::vector<std::string>& args) {
  VGMFile* file = getVGMFile(args[2]);
  if (!file)
    return;

  VGMSeq* seq = dynamic_cast<VGMSeq*>(file);
  if (!seq) {
    fmt::println("Not a sequence file.");
    return;
  }

  std::filesystem::path path = args[3];
  fmt::println("Exporting sequence '{}' to {}...", seq->name(), path.string());
  if (seq->saveAsMidi(path)) {
    fmt::println("Done.");
  } else {
    fmt::println("Failed to export MIDI.");
  }
}

void collection_stitch(const std::vector<std::string>& args) {
  std::filesystem::path midiPath = args[2];
  std::filesystem::path sf2Path = args[3];

  std::vector<conversion::MidiMergeEntry> entries;
  entries.reserve(args.size() - 4);

  std::vector<std::string> collectionIndices;
  collectionIndices.reserve(args.size() - 4);

  for (size_t argIdx = 4; argIdx < args.size(); ++argIdx) {
    VGMColl* coll = getVGMColl(args[argIdx]);
    if (!coll) {
      return;
    }

    VGMSeq* seq = coll->seq();
    if (!seq) {
      fmt::println("Collection '{}' has no sequence, so stitched export cannot be built.",
                   coll->name());
      return;
    }

    entries.push_back({coll, coll->name()});
    collectionIndices.push_back(args[argIdx]);
  }

  if (entries.empty()) {
    fmt::println("No collection indices were provided.");
    return;
  }

  conversion::StitchedExportResult exportResult;
  if (!conversion::exportStitchedMidiAndSf2(entries, midiPath, sf2Path,
                                            &exportResult)) {
    fmt::println("Failed to export stitched output. Check logs for details.");
    return;
  }

  fmt::println("Stitched MIDI: {}", midiPath.string());
  fmt::println("Merged SF2: {}", sf2Path.string());
  for (size_t i = 0; i < entries.size(); ++i) {
    std::string chunkName = entries[i].label;
    if (chunkName.empty()) {
      const VGMColl* coll = entries[i].collection;
      if (coll && coll->seq()) {
        chunkName = coll->seq()->name();
      }
      else if (coll) {
        chunkName = coll->name();
      }
      else {
        chunkName = "(unknown)";
      }
    }
    const u32 startTick =
        (i < exportResult.mergeResult.startTimes.size())
            ? exportResult.mergeResult.startTimes[i]
            : 0;
    const u32 bankOffset =
        (i < exportResult.bankOffsets.size()) ? exportResult.bankOffsets[i] : 0;
    fmt::println("  part {} (#{} '{}'): start tick {} bank +{}", i + 1, collectionIndices[i], chunkName,
                 startTick, bankOffset);
  }
}

void value_scan(const std::vector<std::string>& args) {
  RawFile* file = getRawFile(args[2]);
  if (!file) {
    return;
  }

  auto session = valueSessionForRawFile(*file);
  const auto project = session.scan();
  printValueProjectSummary(project);
}

void value_tree(const std::vector<std::string>& args) {
  RawFile* file = getRawFile(args[2]);
  if (!file) {
    return;
  }

  auto session = valueSessionForRawFile(*file);
  const auto project = session.scan();

  try {
    const int assetIndex = std::stoi(args[3]);
    if (assetIndex < 0 || static_cast<size_t>(assetIndex) >= project.assets.size()) {
      fmt::println("Asset index out of bounds");
      return;
    }

    int maxDepth = 4;
    if (args.size() > 4) {
      maxDepth = std::stoi(args[4]);
    }

    const auto& items = vgmtrans::core::metadata(project.assets[static_cast<size_t>(assetIndex)]).items;
    if (!items.root) {
      fmt::println("Asset has no item tree");
      return;
    }

    printValueItemTree(items, *items.root, 0, maxDepth);
  } catch (...) {
    fmt::println("Invalid arguments");
  }
}

void value_export(const std::vector<std::string>& args) {
  RawFile* file = getRawFile(args[2]);
  if (!file) {
    return;
  }

  auto session = valueSessionForRawFile(*file);
  const auto project = session.scan();
  if (project.collections.empty()) {
    fmt::println("Value scan did not discover any collections.");
    for (const auto& diagnostic : project.diagnostics) {
      printValueDiagnostic(diagnostic);
    }
    return;
  }

  try {
    const int collectionIndex = std::stoi(args[3]);
    if (collectionIndex < 0 || static_cast<size_t>(collectionIndex) >= project.collections.size()) {
      fmt::println("Collection index out of bounds");
      return;
    }

    std::filesystem::path dir = args[4];
    if (!ensureDirectory(dir)) {
      return;
    }

    const auto request = valueExportRequestFromArgs(args, 5);
    if (!request) {
      return;
    }

    const auto collectionId = project.collections[static_cast<size_t>(collectionIndex)].id;
    const auto artifacts = session.exportCollection(collectionId, *request);
    const auto written = writeValueArtifacts(dir, artifacts);
    fmt::println("Exported {} value artifacts for collection '{}'.", written,
                 project.collections[static_cast<size_t>(collectionIndex)].name);
  } catch (...) {
    fmt::println("Invalid arguments");
  }
}

void value_export_all(const std::vector<std::string>& args) {
  RawFile* file = getRawFile(args[2]);
  if (!file) {
    return;
  }

  auto session = valueSessionForRawFile(*file);
  const auto project = session.scan();
  if (project.collections.empty()) {
    fmt::println("Value scan did not discover any collections.");
    for (const auto& diagnostic : project.diagnostics) {
      printValueDiagnostic(diagnostic);
    }
    return;
  }

  std::filesystem::path dir = args[3];
  if (!ensureDirectory(dir)) {
    return;
  }

  const auto request = valueExportRequestFromArgs(args, 4);
  if (!request) {
    return;
  }

  const auto exports = session.exportAllCollections(*request);
  size_t written = 0;
  for (size_t i = 0; i < exports.size(); ++i) {
    const auto& collectionExport = exports[i];
    const auto* collection = vgmtrans::core::collectionById(project, collectionExport.collection);
    if (collection == nullptr) {
      fmt::println("Export skipped missing collection id {}", collectionExport.collection.value);
      continue;
    }

    const auto collectionDir = dir / valueCollectionDirectoryName(i, *collection);
    if (!ensureDirectory(collectionDir)) {
      continue;
    }

    fmt::println("Exporting value collection '{}' to {}...", collection->name, collectionDir.string());
    written += writeValueArtifacts(collectionDir, collectionExport.artifacts);
  }

  fmt::println("Exported {} value artifacts from {} collections.", written, exports.size());
}

void cmd_load(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    fmt::println("Usage: load <path>");
    return;
  }
  if (dbgRoot.openRawFile(args[1])) {
    fmt::println("Loaded {}", args[1]);
  } else {
    fmt::println("Failed to load {}", args[1]);
  }
}

void cmd_help(const std::vector<std::string>&) {
  fmt::print("{}\n", fmt::styled("Nouns:", fmt::emphasis::bold));
  for (const auto& [name, cmd] : commandRegistry) {
    if (name == "exit" || name == "quit" || name == "help" || name == "load")
      continue;

    // Print command name in green, verbs in cyan
    fmt::print("  {}", fmt::styled(cmd.name, fmt::fg(fmt::color::green)));
    if (!cmd.verbs.empty()) {
      fmt::print(" <");
      for (size_t i = 0; i < cmd.verbs.size(); ++i) {
        if (i > 0)
          fmt::print("|");
        fmt::print("{}", fmt::styled(cmd.verbs[i].name, fmt::fg(fmt::color::cyan)));
      }
      fmt::print(">");
    }
    // Pad to column 32
    std::string usageStr = cmd.usage();
    if (usageStr.length() < 30) {
      fmt::print("{}", std::string(30 - usageStr.length(), ' '));
    } else {
      fmt::print("  ");
    }
    fmt::print("{}\n", fmt::styled(cmd.description, fmt::fg(fmt::color::dim_gray)));

    // Print verb details
    for (const auto& verb : cmd.verbs) {
      fmt::print("    {}", fmt::styled(verb.name, fmt::fg(fmt::color::cyan)));
      if (!verb.args.empty()) {
        fmt::print(" {}", fmt::styled(verb.args, fmt::fg(fmt::color::yellow)));
      }
      size_t len = 4 + verb.name.length() + (verb.args.empty() ? 0 : 1 + verb.args.length());
      if (len < 40) {
        fmt::print("{}", std::string(40 - len, ' '));
      } else {
        fmt::print("  ");
      }
      fmt::print("{}\n", verb.description);
    }
  }
  fmt::print("\n{}\n", fmt::styled("Control:", fmt::emphasis::bold));
  fmt::print("  {} {}                   {}\n", fmt::styled("load", fmt::fg(fmt::color::green)),
             fmt::styled("<path>", fmt::fg(fmt::color::yellow)),
             fmt::styled("Load a file", fmt::fg(fmt::color::dim_gray)));
  fmt::print("  {}                          {}\n", fmt::styled("exit", fmt::fg(fmt::color::green)),
             fmt::styled("Exit the shell", fmt::fg(fmt::color::dim_gray)));
  fmt::print("  {}                          {}\n", fmt::styled("help", fmt::fg(fmt::color::green)),
             fmt::styled("Show this help", fmt::fg(fmt::color::dim_gray)));
}

void cmd_exit(const std::vector<std::string>&) {
  exit(0);
}

// ============================================================================
// Command Registration
// ============================================================================

void registerCommands() {
  commandRegistry["rawfile"] = {
      "rawfile",
      "Operate on raw files",
      {{"list", "", "List all loaded raw files", 2, rawfile_list},
       {"info", "<index>", "Show information about a raw file", 3, rawfile_info},
       {"read", "<index> <offset> <length>", "Read bytes from a raw file", 5, rawfile_read},
       {"dump", "<index> <path>", "Dump raw file to disk", 4, rawfile_dump}}};

  commandRegistry["vgmfile"] = {
      "vgmfile",
      "Operate on parsed VGM files",
      {{"list", "", "List all parsed VGM files", 2, vgmfile_list},
       {"info", "<index>", "Show information about a VGM file", 3, vgmfile_info},
       {"tree", "<index> [depth]", "Show item tree", 3, vgmfile_tree},
       {"read", "<index> <offset> <length>", "Read bytes from a VGM file", 5, vgmfile_read},
       {"dump", "<index> <path>", "Dump VGM file to disk", 4, vgmfile_dump}}};

  commandRegistry["collection"] = {
      "collection",
      "Operate on collections",
      {{"list", "", "List all collections", 2, collection_list},
       {"info", "<index>", "Show information about a collection", 3, collection_info},
       {"export", "<index> <dir>", "Export MIDI + SF2 to directory", 4, collection_export},
       {"stitch", "<midi_path> <sf2_path> <coll_idx...>",
        "Stitch collections in order and export remapped MIDI + merged SF2", 5,
        collection_stitch}}};

  commandRegistry["instrumentset"] = {
      "instrumentset",
      "Operate on instrument sets",
      {{"list", "", "List all instrument sets", 2, instrumentset_list},
       {"info", "<index>", "Show information about an instrument set", 3, instrumentset_info},
       {"inspect", "<index> <instr_idx>", "Inspect an instrument's regions", 4,
        instrumentset_inspect},
       {"export", "<index> <path>", "Export instrument set as DLS or SF2", 4,
        instrumentset_export}}};

  commandRegistry["samplecollection"] = {
      "samplecollection",
      "Operate on sample collections",
      {{"list", "", "List all sample collections", 2, samplecollection_list},
       {"info", "<index>", "Show samples in a collection", 3, samplecollection_info},
       {"inspect", "<index> <sample_idx>", "Inspect a sample", 4, samplecollection_inspect},
       {"export", "<index> <dir>", "Export all samples as WAV", 4, samplecollection_export},
       {"dump", "<index> <sample_idx> <path>", "Dump raw sample data", 5, samplecollection_dump}}};

  commandRegistry["sequence"] = {
      "sequence",
      "Operate on sequences",
      {{"list", "", "List all sequences", 2, sequence_list},
       {"events", "<index> <track_idx>", "List events in a sequence track", 4, sequence_events},
       {"export", "<index> <path>", "Export sequence as MIDI", 4, sequence_export}}};

  commandRegistry["value"] = {
      "value",
      "Run the value-oriented scan/export pipeline",
      {{"scan", "<rawfile_idx>", "Scan a raw file with value modules", 3, value_scan},
       {"tree", "<rawfile_idx> <asset_idx> [depth]", "Show a value asset ItemTree", 4, value_tree},
       {"export", "<rawfile_idx> <collection_idx> <dir> [all|midi|sf2|dls|wav]",
        "Export value artifacts for a collection", 5, value_export},
       {"export-all", "<rawfile_idx> <dir> [all|midi|sf2|dls|wav]",
        "Export value artifacts for all collections", 4, value_export_all}}};

  commandRegistry["help"] = {"help", "Show this help", {}};
  commandRegistry["exit"] = {"exit", "Exit the shell", {}};
  commandRegistry["quit"] = {"quit", "Exit the shell", {}};
  commandRegistry["load"] = {"load", "Load a file", {}};
}
