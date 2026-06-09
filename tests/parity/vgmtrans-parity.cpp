/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Root.h"
#include "components/seq/VGMSeq.h"
#include "conversion/MidiFile.h"
#include "core/Export.h"
#include "core/MidiExporter.h"
#include "core/Model.h"
#include "core/ProjectSession.h"
#include "formats/CapcomSnes/CapcomSnesModule.h"
#include "formats/CapcomSnes/CapcomSnesProfile.h"
#include "io/RawFile.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::capcom_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::vector<u8> readFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed to open input file: " + path.string());
  }

  stream.seekg(0, std::ios::end);
  const auto size = stream.tellg();
  if (size < 0) {
    throw std::runtime_error("failed to stat input file: " + path.string());
  }
  stream.seekg(0, std::ios::beg);

  std::vector<u8> bytes(static_cast<size_t>(size));
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!stream) {
    throw std::runtime_error("failed to read input file: " + path.string());
  }
  return bytes;
}

class HeadlessRoot final : public VGMRoot {
 public:
  void UI_setRootPtr(VGMRoot** root) override { *root = this; }
  void UI_log(LogItem*) override {}

  std::filesystem::path UI_getSaveFilePath(
      const std::string& suggestedFilename,
      const std::string& extension = "") override {
    return std::filesystem::path(suggestedFilename).replace_extension(extension);
  }

  std::filesystem::path UI_getSaveDirPath(
      const std::filesystem::path& suggestedDir = {}) override {
    if (!suggestedDir.empty()) {
      return suggestedDir;
    }
    return std::filesystem::current_path();
  }
};

std::vector<u8> legacyCapcomSnesMidi(std::span<const u8> aramBytes, const std::string& name) {
  if (aramBytes.size() > std::numeric_limits<u32>::max()) {
    throw std::runtime_error("input is too large for legacy VirtFile");
  }

  HeadlessRoot root;
  root.init();

  auto rawFile = std::make_unique<VirtFile>(
      aramBytes.data(),
      static_cast<u32>(aramBytes.size()),
      name);
  rawFile->setUseLoaders(false);

  if (!root.loadRawFile(std::move(rawFile))) {
    throw std::runtime_error("legacy scanner did not discover any files");
  }

  for (const auto& file : root.vgmFiles()) {
    const auto* sequenceSlot = std::get_if<VGMSeq*>(&file);
    if (sequenceSlot == nullptr || *sequenceSlot == nullptr) {
      continue;
    }
    auto* sequence = *sequenceSlot;

    auto midi = sequence->convertToMidi(nullptr);
    if (!midi) {
      throw std::runtime_error("legacy sequence failed to convert to MIDI");
    }

    std::vector<u8> bytes;
    midi->writeMidiToBuffer(bytes);
    return bytes;
  }

  throw std::runtime_error("legacy scanner did not discover a sequence");
}

std::vector<u8> valueCapcomSnesMidi(std::vector<u8> aramBytes, const std::string& name) {
  ProjectSession session;
  registerCapcomSnesModule(session.formats());
  registerCapcomSnesProfile(session.profiles());
  session.addSource(SourceFile{.name = name}, std::move(aramBytes));

  const Project project = session.scan();
  if (project.collections.empty()) {
    std::ostringstream message;
    message << "value scanner did not discover a collection";
    if (!project.diagnostics.empty()) {
      message << ": " << project.diagnostics.front().message;
    }
    throw std::runtime_error(message.str());
  }

  const auto artifacts = session.exportCollection(
      project.collections.front().id,
      ExportRequest{
          .kinds = {ExportKind::Midi},
          .loopPolicy = LoopPolicy::PlayOnce,
      });

  for (const auto& artifact : artifacts) {
    if (artifact.mediaType == "audio/midi") {
      if (!artifact.diagnostics.empty()) {
        throw std::runtime_error("value MIDI export reported: " + artifact.diagnostics.front().message);
      }
      return artifact.bytes;
    }
  }

  throw std::runtime_error("value exporter did not produce a MIDI artifact");
}

class MidiReader {
 public:
  explicit MidiReader(std::span<const u8> bytes) : bytes_(bytes) {}

  [[nodiscard]] bool empty() const noexcept { return position_ >= bytes_.size(); }
  [[nodiscard]] size_t position() const noexcept { return position_; }

  void require(size_t count, size_t limit) const {
    if (position_ > limit || count > limit - position_) {
      throw std::runtime_error("truncated MIDI data");
    }
  }

  u8 readU8(size_t limit) {
    require(1, limit);
    return bytes_[position_++];
  }

  u16 be16(size_t limit) {
    const u16 hi = readU8(limit);
    const u16 lo = readU8(limit);
    return static_cast<u16>((hi << 8) | lo);
  }

  u32 be32(size_t limit) {
    const u32 b0 = readU8(limit);
    const u32 b1 = readU8(limit);
    const u32 b2 = readU8(limit);
    const u32 b3 = readU8(limit);
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
  }

  u64 variableLength(size_t limit) {
    u64 value = 0;
    for (int i = 0; i < 4; ++i) {
      const u8 byte = readU8(limit);
      value = (value << 7) | (byte & 0x7f);
      if ((byte & 0x80) == 0) {
        return value;
      }
    }
    throw std::runtime_error("invalid MIDI variable-length quantity");
  }

  std::string ascii(size_t count, size_t limit) {
    require(count, limit);
    const auto begin = reinterpret_cast<const char*>(bytes_.data() + position_);
    position_ += count;
    return std::string(begin, count);
  }

  std::vector<u8> bytes(size_t count, size_t limit) {
    require(count, limit);
    std::vector<u8> result(bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
                           bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + count));
    position_ += count;
    return result;
  }

  void skip(size_t count, size_t limit) {
    require(count, limit);
    position_ += count;
  }

 private:
  std::span<const u8> bytes_;
  size_t position_ = 0;
};

struct NormalizedMidiEvent {
  u32 track = 0;
  u64 tick = 0;
  std::string kind;
  u8 channel = 0;
  u32 a = 0;
  u32 b = 0;
  u32 c = 0;
  std::string text;

  friend bool operator==(const NormalizedMidiEvent&, const NormalizedMidiEvent&) = default;
};

struct ActiveNote {
  u64 tick = 0;
  u8 velocity = 0;
};

using ActiveNoteKey = std::tuple<u32, u8, u8>;

std::string hexByte(u8 value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string text = "0x00";
  text[2] = digits[(value >> 4) & 0x0f];
  text[3] = digits[value & 0x0f];
  return text;
}

std::string describeEvent(const NormalizedMidiEvent& event) {
  std::ostringstream out;
  out << "track=" << event.track
      << " tick=" << event.tick
      << " kind=" << event.kind;
  if (!event.kind.empty()) {
    out << " channel=" << static_cast<int>(event.channel)
        << " a=" << event.a
        << " b=" << event.b
        << " c=" << event.c;
  }
  if (!event.text.empty()) {
    out << " text=\"" << event.text << "\"";
  }
  return out.str();
}

void addEvent(std::vector<NormalizedMidiEvent>& events, NormalizedMidiEvent event) {
  events.push_back(std::move(event));
}

void addNoteOff(std::vector<NormalizedMidiEvent>& events,
                std::map<ActiveNoteKey, std::vector<ActiveNote>>& activeNotes,
                u32 track,
                u64 tick,
                u8 channel,
                u8 key,
                u8 releaseVelocity) {
  const ActiveNoteKey activeKey{track, channel, key};
  auto active = activeNotes.find(activeKey);
  if (active == activeNotes.end() || active->second.empty()) {
    addEvent(events, NormalizedMidiEvent{
                         .track = track,
                         .tick = tick,
                         .kind = "note-off",
                         .channel = channel,
                         .a = key,
                         .b = releaseVelocity,
                     });
    return;
  }

  const ActiveNote note = active->second.front();
  active->second.erase(active->second.begin());
  addEvent(events, NormalizedMidiEvent{
                       .track = track,
                       .tick = note.tick,
                       .kind = "note",
                       .channel = channel,
                       .a = key,
                       .b = note.velocity,
                       .c = static_cast<u32>(tick - note.tick),
                   });
}

void finishActiveNotes(std::vector<NormalizedMidiEvent>& events,
                       const std::map<ActiveNoteKey, std::vector<ActiveNote>>& activeNotes) {
  for (const auto& [key, notes] : activeNotes) {
    const auto [track, channel, midiKey] = key;
    for (const auto& note : notes) {
      addEvent(events, NormalizedMidiEvent{
                           .track = track,
                           .tick = note.tick,
                           .kind = "note-on",
                           .channel = channel,
                           .a = midiKey,
                           .b = note.velocity,
                       });
    }
  }
}

void addMetaEvent(std::vector<NormalizedMidiEvent>& events,
                  u32 track,
                  u64 tick,
                  u8 type,
                  std::vector<u8> payload) {
  if (type == 0x2f) {
    addEvent(events, NormalizedMidiEvent{
                         .track = track,
                         .tick = tick,
                         .kind = "end",
                     });
    return;
  }

  if (type == 0x03) {
    addEvent(events, NormalizedMidiEvent{
                         .track = track,
                         .tick = tick,
                         .kind = "track-name",
                         .text = std::string(payload.begin(), payload.end()),
                     });
    return;
  }

  if (type == 0x51 && payload.size() == 3) {
    const u32 tempo = (static_cast<u32>(payload[0]) << 16) |
                      (static_cast<u32>(payload[1]) << 8) |
                      static_cast<u32>(payload[2]);
    addEvent(events, NormalizedMidiEvent{
                         .track = track,
                         .tick = tick,
                         .kind = "tempo",
                         .a = tempo,
                     });
    return;
  }

  if (type >= 0x01 && type <= 0x07) {
    addEvent(events, NormalizedMidiEvent{
                         .track = track,
                         .tick = tick,
                         .kind = "meta-text",
                         .a = type,
                         .text = std::string(payload.begin(), payload.end()),
                     });
    return;
  }

  addEvent(events, NormalizedMidiEvent{
                       .track = track,
                       .tick = tick,
                       .kind = "meta",
                       .a = type,
                       .b = static_cast<u32>(payload.size()),
                   });
}

std::vector<NormalizedMidiEvent> normalizeMidi(std::span<const u8> bytes) {
  MidiReader reader(bytes);
  expect(reader.ascii(4, bytes.size()) == "MThd", "MIDI missing MThd header");
  const u32 headerLength = reader.be32(bytes.size());
  expect(headerLength >= 6, "MIDI header is too short");
  const size_t headerEnd = reader.position() + headerLength;
  expect(headerEnd <= bytes.size(), "MIDI header extends past end of file");
  static_cast<void>(reader.be16(headerEnd));
  const u16 trackCount = reader.be16(headerEnd);
  static_cast<void>(reader.be16(headerEnd));
  reader.skip(headerEnd - reader.position(), bytes.size());

  std::vector<NormalizedMidiEvent> events;

  for (u32 track = 0; track < trackCount; ++track) {
    expect(reader.ascii(4, bytes.size()) == "MTrk", "MIDI missing MTrk header");
    const u32 trackLength = reader.be32(bytes.size());
    const size_t trackEnd = reader.position() + trackLength;
    expect(trackEnd <= bytes.size(), "MIDI track extends past end of file");

    u64 tick = 0;
    std::optional<u8> runningStatus;
    std::map<ActiveNoteKey, std::vector<ActiveNote>> activeNotes;

    while (reader.position() < trackEnd) {
      tick += reader.variableLength(trackEnd);
      u8 status = reader.readU8(trackEnd);
      std::optional<u8> firstDataByte;
      if (status < 0x80) {
        if (!runningStatus.has_value()) {
          throw std::runtime_error("MIDI running status used before status byte");
        }
        firstDataByte = status;
        status = *runningStatus;
      } else if (status < 0xf0) {
        runningStatus = status;
      } else {
        runningStatus.reset();
      }

      if (status == 0xff) {
        const u8 type = reader.readU8(trackEnd);
        const u64 length = reader.variableLength(trackEnd);
        if (length > std::numeric_limits<size_t>::max()) {
          throw std::runtime_error("MIDI meta event is too large");
        }
        addMetaEvent(events, track, tick, type, reader.bytes(static_cast<size_t>(length), trackEnd));
        continue;
      }

      if (status == 0xf0 || status == 0xf7) {
        const u64 length = reader.variableLength(trackEnd);
        if (length > std::numeric_limits<size_t>::max()) {
          throw std::runtime_error("MIDI sysex event is too large");
        }
        reader.skip(static_cast<size_t>(length), trackEnd);
        addEvent(events, NormalizedMidiEvent{
                             .track = track,
                             .tick = tick,
                             .kind = "sysex",
                             .a = status,
                             .b = static_cast<u32>(length),
                         });
        continue;
      }

      if (status >= 0xf0) {
        throw std::runtime_error("unsupported MIDI system event: " + hexByte(status));
      }

      const u8 command = static_cast<u8>(status & 0xf0);
      const u8 channel = static_cast<u8>(status & 0x0f);
      const bool oneDataByte = command == 0xc0 || command == 0xd0;
      const u8 data1 = firstDataByte.value_or(reader.readU8(trackEnd));
      const u8 data2 = oneDataByte ? 0 : reader.readU8(trackEnd);

      if (command == 0x80) {
        addNoteOff(events, activeNotes, track, tick, channel, data1, data2);
      } else if (command == 0x90) {
        if (data2 == 0) {
          addNoteOff(events, activeNotes, track, tick, channel, data1, data2);
        } else {
          activeNotes[{track, channel, data1}].push_back(ActiveNote{.tick = tick, .velocity = data2});
        }
      } else if (command == 0xb0) {
        addEvent(events, NormalizedMidiEvent{
                             .track = track,
                             .tick = tick,
                             .kind = "control",
                             .channel = channel,
                             .a = data1,
                             .b = data2,
                         });
      } else if (command == 0xc0) {
        addEvent(events, NormalizedMidiEvent{
                             .track = track,
                             .tick = tick,
                             .kind = "program",
                             .channel = channel,
                             .a = data1,
                         });
      } else if (command == 0xd0) {
        addEvent(events, NormalizedMidiEvent{
                             .track = track,
                             .tick = tick,
                             .kind = "channel-pressure",
                             .channel = channel,
                             .a = data1,
                         });
      } else if (command == 0xe0) {
        addEvent(events, NormalizedMidiEvent{
                             .track = track,
                             .tick = tick,
                             .kind = "pitch-bend",
                             .channel = channel,
                             .a = static_cast<u32>(data1 | (data2 << 7)),
                         });
      } else {
        addEvent(events, NormalizedMidiEvent{
                             .track = track,
                             .tick = tick,
                             .kind = "channel-event",
                             .channel = channel,
                             .a = command,
                             .b = data1,
                             .c = data2,
                         });
      }
    }

    finishActiveNotes(events, activeNotes);
    reader.skip(trackEnd - reader.position(), bytes.size());
  }

  expect(reader.empty(), "MIDI has trailing bytes after declared tracks");

  std::ranges::sort(events, [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.track, lhs.tick, lhs.kind, lhs.channel, lhs.a, lhs.b, lhs.c, lhs.text) <
           std::tie(rhs.track, rhs.tick, rhs.kind, rhs.channel, rhs.a, rhs.b, rhs.c, rhs.text);
  });
  return events;
}

bool compareMidi(std::span<const u8> legacyBytes, std::span<const u8> valueBytes, std::ostream& out) {
  const auto legacy = normalizeMidi(legacyBytes);
  const auto value = normalizeMidi(valueBytes);
  if (legacy == value) {
    out << "MIDI parity ok: " << legacy.size() << " normalized events\n";
    return true;
  }

  out << "MIDI parity mismatch\n";
  out << "legacy events: " << legacy.size() << "\n";
  out << "value events: " << value.size() << "\n";

  const size_t shared = std::min(legacy.size(), value.size());
  for (size_t i = 0; i < shared; ++i) {
    if (!(legacy[i] == value[i])) {
      out << "first mismatch at normalized event " << i << "\n";
      out << "legacy: " << describeEvent(legacy[i]) << "\n";
      out << "value:  " << describeEvent(value[i]) << "\n";
      return false;
    }
  }

  if (legacy.size() > shared) {
    out << "first extra legacy event: " << describeEvent(legacy[shared]) << "\n";
  } else if (value.size() > shared) {
    out << "first extra value event: " << describeEvent(value[shared]) << "\n";
  }
  return false;
}

int selfTest() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .name = "Parity",
          .events = {
              Tempo{.tick = 0, .microsecondsPerQuarter = 500000},
              ProgramChange{.tick = 0, .channel = 2, .program = 12},
              Volume{.tick = 0, .channel = 2, .value = 80},
              Pan{.tick = 12, .channel = 2, .value = 32},
              NoteDuration{.tick = 24, .channel = 2, .key = 64, .velocity = 100, .duration = 36},
              EndOfTrack{.tick = 60},
          },
      }},
  };

  const auto midi = MidiExporter().exportMidi(performance);
  const auto normalized = normalizeMidi(midi);

  expect(std::ranges::any_of(normalized, [](const auto& event) {
    return event.kind == "tempo" && event.tick == 0 && event.a == 500000;
  }), "self-test should normalize tempo events");
  expect(std::ranges::any_of(normalized, [](const auto& event) {
    return event.kind == "program" && event.channel == 2 && event.a == 12;
  }), "self-test should normalize program changes");
  expect(std::ranges::any_of(normalized, [](const auto& event) {
    return event.kind == "note" && event.tick == 24 && event.channel == 2 &&
           event.a == 64 && event.b == 100 && event.c == 36;
  }), "self-test should pair note durations");

  std::ostringstream parityOutput;
  expect(compareMidi(midi, midi, parityOutput), "self-test should compare identical MIDI");
  std::cout << "vgmtrans-parity self-test ok\n";
  return 0;
}

int compareCapcomSnesAramMidi(const std::filesystem::path& path) {
  const auto aramBytes = readFile(path);
  const std::string name = path.filename().string();
  const auto legacyMidi = legacyCapcomSnesMidi(aramBytes, name);
  const auto valueMidi = valueCapcomSnesMidi(aramBytes, name);
  return compareMidi(legacyMidi, valueMidi, std::cout) ? 0 : 1;
}

void printUsage(std::ostream& out) {
  out << "usage:\n"
      << "  vgmtrans-parity --self-test\n"
      << "  vgmtrans-parity capcom-snes-aram-midi <raw-aram-file>\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
      return selfTest();
    }

    if (argc == 3 && std::string(argv[1]) == "capcom-snes-aram-midi") {
      return compareCapcomSnesAramMidi(argv[2]);
    }

    printUsage(std::cerr);
    return 2;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
