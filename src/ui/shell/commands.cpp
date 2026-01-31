/**
 * VGMTrans (c) - 2002-2026
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "commands.h"

#include <fstream>
#include <string>

#include <fmt/format.h>
#include <fmt/color.h>

#include "DBGVGMRoot.h"
#include "RawFile.h"
#include "SeqTrack.h"
#include "VGMColl.h"
#include "VGMFile.h"
#include "VGMInstrSet.h"
#include "VGMMiscFile.h"
#include "VGMRgn.h"
#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "VGMSeq.h"
#include "VGMExport.h"

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

void printHexDump(const uint8_t* data, size_t length) {
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

void printChildren(VGMItem* item) {
  for (auto* child : item->children()) {
    fmt::print("  [0x{:x}:0x{:x}] {}\n", child->offset(), child->length(), child->name());
  }
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
        const uint8_t* data = reinterpret_cast<const uint8_t*>(file->data());
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
        const uint8_t* data = reinterpret_cast<const uint8_t*>(rawData + file->startOffset());
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
  for (size_t i = 0; i < instrSet->aInstrs.size(); ++i) {
    VGMInstr* instr = instrSet->aInstrs[i];
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
    if (instrIdx >= 0 && static_cast<size_t>(instrIdx) < instrSet->aInstrs.size()) {
      VGMInstr* instr = instrSet->aInstrs[instrIdx];
      fmt::println("Regions for Instrument #{}:", instrIdx);
      const auto& regions = instr->regions();
      for (size_t i = 0; i < regions.size(); ++i) {
        VGMRgn* rgn = regions[i];
        fmt::print("  [Rgn #{}] [0x{:x}:0x{:x}] Key {}-{} Vel {}-{} Samp {} Unity {}\n"
                   "    ADSR: A {:.3f}s D {:.3f}s S {:.1f}% R {:.3f}s\n",
                   i, rgn->offset(), rgn->length(), rgn->keyLow, rgn->keyHigh, rgn->velLow,
                   rgn->velHigh, rgn->sampNum, static_cast<int>(rgn->unityKey), rgn->attack_time,
                   rgn->decay_time, rgn->sustain_level * 100.0, rgn->release_time);
      }
    } else {
      fmt::println("Instrument index out of bounds");
    }
  } catch (...) {
    fmt::println("Invalid instrument index");
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
  fmt::println("Samples ({}):", sampColl->samples.size());
  for (size_t i = 0; i < sampColl->samples.size(); ++i) {
    VGMSamp* samp = sampColl->samples[i];
    fmt::print("  [#{}] [0x{:x}:0x{:x}] {} ({} Hz)\n", i, samp->offset(), samp->length(),
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
    if (sampIdx >= 0 && static_cast<size_t>(sampIdx) < sampColl->samples.size()) {
      VGMSamp* samp = sampColl->samples[sampIdx];
      fmt::println("Sample #{} Information:", sampIdx);
      fmt::print("  Name: {}\n", samp->name());
      fmt::print("  [0x{:x}:0x{:x}]\n", samp->offset(), samp->length());
      fmt::print("  Rate: {} Hz  Channels: {}  BPS: {}\n", samp->rate, samp->channels,
                 samp->bitsPerSample());
      fmt::print("  Unity Key: {}  Fine Tune: {}\n", static_cast<int>(samp->unityKey),
                 samp->fineTune);
      if (samp->loopStatus() != 0) {
        fmt::print("  Loop: Start 0x{:x}  Length 0x{:x}  Measure: {}\n", samp->loop.loopStart,
                   samp->loop.loopLength, static_cast<int>(samp->loop.loopStartMeasure));
      } else {
        fmt::println("  Loop: None");
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

  fmt::println("Exporting {} samples to {}...", sampColl->samples.size(), dir.string());
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
    if (sampIdx < 0 || static_cast<size_t>(sampIdx) >= sampColl->samples.size()) {
      fmt::println("Sample index out of bounds");
      return;
    }
    VGMSamp* samp = sampColl->samples[sampIdx];
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
    if (trackIdx >= 0 && static_cast<size_t>(trackIdx) < seq->aTracks.size()) {
      SeqTrack* track = seq->aTracks[trackIdx];
      fmt::println("Events for Track {}:", trackIdx);
      printChildren(track);
    } else {
      fmt::println("Track index out of bounds");
    }
  } catch (...) {
    fmt::println("Invalid track index");
  }
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
       {"export", "<index> <dir>", "Export MIDI + SF2 to directory", 4, collection_export}}};

  commandRegistry["instrumentset"] = {
      "instrumentset",
      "Operate on instrument sets",
      {{"list", "", "List all instrument sets", 2, instrumentset_list},
       {"info", "<index>", "Show information about an instrument set", 3, instrumentset_info},
       {"inspect", "<index> <instr_idx>", "Inspect an instrument's regions", 4,
        instrumentset_inspect}}};

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
       {"events", "<index> <track_idx>", "List events in a sequence track", 4, sequence_events}}};

  commandRegistry["help"] = {"help", "Show this help", {}};
  commandRegistry["exit"] = {"exit", "Exit the shell", {}};
  commandRegistry["quit"] = {"quit", "Exit the shell", {}};
  commandRegistry["load"] = {"load", "Load a file", {}};
}
