/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS2/SonyPS2.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/SynthMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::sony_ps2 {

using namespace core;

namespace {

constexpr u32 kMaxCommands = 1048576;
constexpr u32 kSeTrackFlag = 1u << 24;

[[nodiscard]] constexpr bool isSeTrack(u32 sourceTrackNumber) noexcept {
  return (sourceTrackNumber & kSeTrackFlag) != 0;
}

[[nodiscard]] constexpr u32 seTrackNumber(u8 set, u8 timbre, u8 key) noexcept {
  return kSeTrackFlag | (static_cast<u32>(set) << 16) | (static_cast<u32>(timbre) << 8) | key;
}

struct MidiEvent {
  u32 offset = 0;
  u32 end = 0;
  u32 delta = 0;
  u8 status = 0;
  u8 channel = 0;
  u8 data1 = 0;
  u8 data2 = 0;
  u8 dataBytes = 0;
  u8 metaType = 0;
  std::vector<u8> payload;
  std::optional<u32> loopDestination;
  u8 loopId = 0;
  u8 loopCount = 0;
  bool endEvent = false;
  bool malformed = false;
};

struct TrackState {
  TrackState(const SequenceProgram& sequence, const TrackProgram& source)
      : channel(isSeTrack(source.sourceTrackNumber) ? 0 : static_cast<u8>(source.sourceTrackNumber)),
        seSequence(isSeTrack(source.sourceTrackNumber)),
        seSet(static_cast<u8>((source.sourceTrackNumber >> 16) & 0x0f)),
        seTimbre(static_cast<u8>((source.sourceTrackNumber >> 8) & 0x7f)),
        seKey(static_cast<u8>(source.sourceTrackNumber & 0x7f)) {
    if (sequence.sectionPlaylist) {
      sectionPan = sequence.behavior.initialChannelPan.value_or(ChannelPan{
          .position = 0.5,
          .voicePanLaw = PanLaw::ConstantMaximum,
      });
      sectionTempo = sequence.behavior.initialTempoMicrosecondsPerQuarter;
    }
  }

  void beginSection() {
    resetSectionState = sectionStarted;
    sectionStarted = true;
  }

  u8 channel = 0;
  bool seSequence = false;
  u8 seSet = 0;
  u8 seTimbre = 0;
  u8 seKey = 0;
  u8 bank = 0;
  u8 pendingBank = 0;
  u8 program = 0;
  u8 nrpnMsb = 0xff;
  u8 nrpnLsb = 0xff;
  u8 dataEntryMsb = 0;
  std::optional<std::pair<u8, u8>> dataEntryNrpn;
  u16 pitchBendPositive = 256;
  u16 pitchBendNegative = 256;
  std::vector<PitchBendZone> pitchBendZones;
  struct ActiveNote {
    u8 key = 0;
    u16 negative = 256;
    u16 positive = 256;
    bool keyDown = true;
    bool sustained = false;
  };
  std::vector<ActiveNote> activeNotes;
  bool sustain = false;
  bool portamentoEnabled = false;
  bool sectionStarted = false;
  bool resetSectionState = false;
  std::optional<ChannelPan> sectionPan;
  u32 sectionTempo = 500000;
  u16 pitchBendValue = 8192;
  double emittedPitchBendSemitones = 0.0;
  PerformanceNoteId seNote;
  double seTerminalKey = 0.0;
  bool initialized = false;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config) : programs(config.programs) {}
  std::vector<ProgramRuntimeInfo> programs;
};

[[nodiscard]] u64 delayedTick(const VmApi& vm, u32 delta) {
  return vm.tick() > std::numeric_limits<u64>::max() - delta ? std::numeric_limits<u64>::max() : vm.tick() + delta;
}

[[nodiscard]] double linearMidi7(u8 value) {
  return std::min<u8>(value, 127) / 127.0;
}

[[nodiscard]] int scaledModulationController(u8 value) {
  // modhsyn expands MIDI's 0..127 controller to 0..128 before applying the
  // Program's MIDI modulation depth.
  return static_cast<int>(std::min<u8>(value, 127)) * 128 / 127;
}

[[nodiscard]] int scaledSignedDepth(int depth, int controller) {
  const int product = depth * controller;
  // The EE code biases a negative product before an arithmetic shift. This
  // is signed division truncated toward zero, including values just below a
  // multiple of 128.
  return product < 0 ? -((-product) / 128) : product / 128;
}

[[nodiscard]] bool controllerAffectsPlayback(u8 controller) {
  switch (controller) {
    case 0:
    case 1:
    case 2:
    case 5:
    case 6:
    case 7:
    case 10:
    case 11:
    case 38:
    case 64:
    case 65:
    case 98:
    case 99:
    case 120:
    case 121:
    case 123:
      return true;
    default:
      return false;
  }
}

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& programState;

  [[nodiscard]] const ProgramRuntimeInfo* selectedProgram() const {
    const auto found = std::ranges::find_if(programState.programs, [&](const ProgramRuntimeInfo& candidate) {
      return candidate.bank == track.bank && candidate.program == track.program;
    });
    return found == programState.programs.end() ? nullptr : &*found;
  }

  [[nodiscard]] TrackState::ActiveNote bendRangeForKey(u8 key) const {
    TrackState::ActiveNote result{
        .key = key,
        .negative = track.pitchBendNegative,
        .positive = track.pitchBendPositive,
    };
    bool matched = false;
    for (const auto& zone : track.pitchBendZones) {
      if (key < zone.keyLow || key > zone.keyHigh) {
        continue;
      }
      if (!matched) {
        result.negative = zone.negative;
        result.positive = zone.positive;
        matched = true;
      } else {
        // A layered key can start several voices. Value-core currently has a
        // track-wide bend, so differing ranges within one key cannot all be
        // represented; retain the largest physical excursion as the fallback.
        result.negative = std::max(result.negative, zone.negative);
        result.positive = std::max(result.positive, zone.positive);
      }
    }
    return result;
  }

  [[nodiscard]] double currentPitchBend(const TrackState::ActiveNote& note) const {
    const int displacement = static_cast<int>(track.pitchBendValue) - 8192;
    const u16 range = displacement < 0 ? note.negative : note.positive;
    // modhsyn computes (range * signedWheel) / 8192 using integer units of
    // 1/128 semitone. In particular, +8191 is one unit shy of the full range.
    const int bendUnits = static_cast<int>(range) * displacement / 8192;
    return bendUnits / 128.0;
  }

  void emitCurrentPitchBend(PerformanceEmitter& delayed, const TrackState::ActiveNote& note) {
    const double semitones = currentPitchBend(note);
    if (semitones == track.emittedPitchBendSemitones) {
      return;
    }
    track.emittedPitchBendSemitones = semitones;
    // This is already the physical per-voice result. Supplying the normalized
    // wheel would make collection-aware MIDI rendering apply the instrument's
    // maximum range a second time.
    delayed.pitchBend(PitchBendPerformanceEvent{.semitones = semitones});
  }

  void releaseNotes(u8 key) {
    for (auto& note : track.activeNotes) {
      if (note.key == key && note.keyDown) {
        note.keyDown = false;
        note.sustained = track.sustain;
      }
    }
  }

  void releaseSustainedNotes() {
    for (auto& note : track.activeNotes) {
      note.sustained = false;
    }
  }

  void capturePortamentoSource(PerformanceEmitter& delayed, u8 key) {
    if (track.portamentoEnabled) {
      // modhsyn derives the next glide's source from the most recent note-off.
      // Its CC84 setter writes a separate field that the glide path never reads.
      delayed.portamentoControl(key);
    }
  }

  void releaseAllKeyDownNotes() {
    for (auto& note : track.activeNotes) {
      if (note.keyDown) {
        note.keyDown = false;
        note.sustained = false;
      }
    }
  }

  void updateProgramSettings(PerformanceEmitter& delayed) {
    track.pitchBendPositive = 256;
    track.pitchBendNegative = 256;
    track.pitchBendZones.clear();
    if (const auto* program = selectedProgram()) {
      track.pitchBendPositive = program->pitchBendPositive;
      track.pitchBendNegative = program->pitchBendNegative;
      track.pitchBendZones = program->pitchBendZones;
    }
    const u32 maximum = std::max(track.pitchBendPositive, track.pitchBendNegative);
    delayed.pitchBendRange(PitchBendRangePerformanceEvent{
        .cents = static_cast<u16>(std::min<u32>(65535, (maximum * 100u + 127u) / 128u)),
    });
  }

  void beforeCommand() {
    if (track.resetSectionState) {
      track.resetSectionState = false;
      if (track.channel == 0) {
        out.tempo(track.sectionTempo);
      }
      out.channelPan(track.sectionPan->position, track.sectionPan->voicePanLaw);
    }
    if (track.initialized) {
      return;
    }
    track.initialized = true;
    if (track.seSequence) {
      out.instrument(setbInstrumentIdentity(track.seSet, track.seTimbre));
    } else {
      out.instrument(instrumentIdentity(track.bank, track.program));
      updateProgramSettings(out);
    }
  }

  [[nodiscard]] Effects after(u32 delta) const { return Effects::wait(delta); }

  Effects note(u8 channel, u8 key, u8 velocity, u32 delta) {
    if (channel == track.channel) {
      auto delayed = out.at(delayedTick(vm, delta));
      if (velocity == 0) {
        delayed.noteOff(key);
        releaseNotes(key);
        capturePortamentoSource(delayed, key);
      } else {
        const auto note = bendRangeForKey(key);
        emitCurrentPitchBend(delayed, note);
        delayed.noteOn(key, velocityGain(velocity));
        track.activeNotes.push_back(note);
      }
    }
    return after(delta);
  }

  Effects noteOff(u8 channel, u8 key, u32 delta) {
    if (channel == track.channel) {
      auto delayed = out.at(delayedTick(vm, delta));
      delayed.noteOff(key);
      releaseNotes(key);
      capturePortamentoSource(delayed, key);
    }
    return after(delta);
  }

  Effects seNote(u8 set, u8 timbre, u8 key, u8 velocity, bool noteOnOnly, u32 delta) {
    if (track.seSequence && set == track.seSet && timbre == track.seTimbre && key == track.seKey) {
      auto delayed = out.at(delayedTick(vm, delta));
      if (velocity == 0) {
        delayed.noteOff(key);
        track.seNote = {};
      } else {
        // The 0x9n variant asks the driver not to replace an already sounding
        // equal-key voice. ActiveNoteState is key-addressed, so this currently
        // shares ordinary note-on behavior when equal keys overlap.
        (void)noteOnOnly;
        track.seNote = delayed.noteOn(key, velocityGain(velocity));
        track.seTerminalKey = key;
      }
    }
    return after(delta);
  }

  Effects sePitch(u8 set, u8 timbre, u8 key, u16 cents, bool negative, u32 time, u32 delta) {
    if (track.seSequence && set == track.seSet && timbre == track.seTimbre && key == track.seKey &&
        track.seNote.valid()) {
      auto delayed = out.at(delayedTick(vm, delta));
      const double start = delayed.currentPitchTransitionKey(track.seNote).value_or(track.seTerminalKey);
      const double target = start + (negative ? -cents : cents) / 100.0;
      const u32 duration = std::max<u32>(time, 1);
      delayed.pitchSlide(track.seNote, start, target, PitchSlideTiming::fixedDuration(duration, duration));
      track.seTerminalKey = target;
    }
    return after(delta);
  }

  Effects programChange(u8 channel, u8 value, u32 delta) {
    if (channel == track.channel) {
      track.bank = track.pendingBank;
      track.program = value;
      auto delayed = out.at(delayedTick(vm, delta));
      delayed.instrument(instrumentIdentity(track.bank, track.program));
      updateProgramSettings(delayed);
    }
    return after(delta);
  }

  Effects controller(u8 channel, u8 controller, u8 value, u32 delta) {
    if (channel != track.channel) {
      return after(delta);
    }
    auto delayed = out.at(delayedTick(vm, delta));
    switch (controller) {
      case 0:
        // Like the driver, Bank Select only stages the bank used by the next
        // Program Change; it does not retarget voices or the active program.
        track.pendingBank = value;
        break;
      case 1:
        if (const auto* program = selectedProgram()) {
          const int controllerAmount = scaledModulationController(value);
          const double depth =
              std::max(std::abs(scaledSignedDepth(program->midiPitchDepthPositive, controllerAmount)),
                       std::abs(scaledSignedDepth(program->midiPitchDepthNegative, controllerAmount))) /
              128.0;
          auto event = ModulationPerformanceEvent{
              .target = ModulationPerformanceTarget::VibratoDepth,
              .amount = linearMidi7(value),
              .pitchDepthSemitones = depth,
          };
          delayed.modulation(std::move(event));
        } else {
          delayed.modulation(ModulationPerformanceTarget::VibratoDepth, value / 127.0);
        }
        break;
      case 2:
        if (const auto* program = selectedProgram()) {
          const int controllerAmount = scaledModulationController(value);
          const double depth =
              std::clamp(std::max(std::abs(scaledSignedDepth(program->midiAmpDepthPositive, controllerAmount)),
                                  std::abs(scaledSignedDepth(program->midiAmpDepthNegative, controllerAmount))) /
                             128.0,
                         0.0, 0.999);
          auto event = ModulationPerformanceEvent{
              .target = ModulationPerformanceTarget::TremoloDepth,
              .amount = linearMidi7(value),
              .volumeDepthLinearGain = depth,
              .context =
                  LfoPerformanceContext{
                      .tremoloGainMode = TremoloGainMode::BipolarAroundNominal,
                  },
          };
          delayed.modulation(std::move(event));
        } else {
          delayed.modulation(ModulationPerformanceTarget::TremoloDepth, value / 127.0);
        }
        break;
      case 5:
        delayed.pitchTransitionSettings(value * 20.0);
        break;
      case 7:
        delayed.level(linearMidi7(value));
        break;
      case 10:
        // modhsyn stores CC10 as a signed offset from center, then applies it
        // independently to every voice's Program/Split/Sample pan.
        delayed.channelPan(panPositionFrom7Bit(std::min<u8>(value, 127)), PanLaw::ConstantMaximum);
        break;
      case 11:
        delayed.expression(linearMidi7(value));
        break;
      case 64: {
        const bool sustain = value != 0;
        delayed.sustainPedal(sustain);
        if (track.sustain && !sustain) {
          releaseSustainedNotes();
        }
        track.sustain = sustain;
        break;
      }
      case 65:
        track.portamentoEnabled = value != 0;
        delayed.portamentoEnable(track.portamentoEnabled);
        break;
      case 84:
        // Retained in the source map, but unused by this driver revision.
        break;
      case 98:
        track.nrpnLsb = value;
        track.dataEntryNrpn.reset();
        break;
      case 99:
        track.nrpnMsb = value;
        track.nrpnLsb = 0xff;
        track.dataEntryNrpn.reset();
        break;
      case 6:
        track.dataEntryMsb = value;
        track.dataEntryNrpn = std::pair{track.nrpnMsb, track.nrpnLsb};
        if (track.nrpnMsb == 3 && (track.nrpnLsb & 0x0f) == 0) {
          // SPU2 applies this as a core-global signed effect-return depth.
          // Track-local MIDI reverb send is the nearest portable projection;
          // it cannot retain core selection or wet-only voice routing.
          delayed.reverb(value / 127.0);
        } else if (track.nrpnMsb == 0x10 && track.nrpnLsb == 0) {
          delayed.marker(MarkerPerformanceEvent{.text = "SonyPS2 mark callback " + std::to_string(value)});
        } else if (track.nrpnMsb == 0x11) {
          delayed.marker(MarkerPerformanceEvent{.text = "SonyPS2 mark MSB callback " + std::to_string(value)});
        }
        // The remaining reverb NRPNs configure negative-phase sends, the
        // algorithm, delay, and feedback. They remain visible in the source
        // command stream until value-core has a physical reverb model.
        // modhsyn consumes the selection after every Data Entry MSB. Retain a
        // separate snapshot only so a following LSB can complete a 14-bit mark.
        track.nrpnMsb = 0xff;
        track.nrpnLsb = 0xff;
        break;
      case 38:
        if (track.dataEntryNrpn == std::pair<u8, u8>{0x10, 1}) {
          delayed.marker(MarkerPerformanceEvent{
              .text = "SonyPS2 mark callback " + std::to_string((track.dataEntryMsb << 7) | value),
          });
        } else if (track.dataEntryNrpn && track.dataEntryNrpn->first == 0x12) {
          delayed.marker(MarkerPerformanceEvent{
              .text = "SonyPS2 mark MSB callback " + std::to_string((track.dataEntryMsb << 7) | value),
          });
        }
        track.dataEntryNrpn.reset();
        break;
      case 120:
        // The driver frees hardware voices immediately. Value-core can end
        // every note here, but its target synths will still apply the region's
        // release envelope until an explicit hard-silence event is modeled.
        delayed.allNotesOff();
        track.activeNotes.clear();
        break;
      case 123:
        delayed.releaseAllNotes();
        releaseAllKeyDownNotes();
        break;
      case 121:
        track.pitchBendValue = 8192;
        track.emittedPitchBendSemitones = 0.0;
        delayed.sustainPedal(false);
        track.sustain = false;
        releaseSustainedNotes();
        delayed.portamentoEnable(false);
        track.portamentoEnabled = false;
        delayed.vibratoDepth(0.0);
        delayed.tremoloLinearGainDepth(0.0);
        delayed.level(1.0);
        delayed.expression(1.0);
        delayed.channelPan(0.5, PanLaw::ConstantMaximum);
        delayed.pitchBend(0.0);
        break;
      default:
        break;
    }
    return after(delta);
  }

  Effects pitchBend(u8 channel, u16 value, u32 delta) {
    if (channel == track.channel) {
      track.pitchBendValue = std::min<u16>(value, 16383);
      if (!track.activeNotes.empty()) {
        auto delayed = out.at(delayedTick(vm, delta));
        // The original driver updates every live voice with that voice's split
        // range. A track-wide event is exact while those ranges agree; if they
        // differ concurrently, prefer the most recently started voice until
        // value-core can address pitch automation per voice.
        emitCurrentPitchBend(delayed, track.activeNotes.back());
      }
    }
    return after(delta);
  }

  Effects tempo(u32 microseconds, u32 delta) {
    if (track.channel == 0 && microseconds != 0) {
      out.at(delayedTick(vm, delta)).tempo(microseconds);
    }
    return after(delta);
  }

  Effects loop(u8 slot, u8 count, Address destination, u32 delta) {
    Effects effects = after(delta);
    if (count == 0) {
      effects.flowOverride = vm.declaredLoop(destination).flowOverride;
    } else {
      effects.flowOverride = vm.countedRepeatUntil(slot & 7, static_cast<u32>(count) + 1, destination).flowOverride;
    }
    return effects;
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] std::optional<std::pair<u32, u32>> readVlq(ByteReader reader, u32& cursor, u32 end) {
  u32 value = 0;
  const u32 begin = cursor;
  for (u32 byte = 0; byte < 4 && cursor < end; ++byte) {
    const u8 next = reader.u8At(cursor++);
    value = (value << 7) | (next & 0x7f);
    if ((next & 0x80) == 0) {
      return std::pair{value, cursor - begin};
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<MidiEvent> inspectMidiEvents(ByteReader reader, const MidiBlockLayout& layout) {
  std::vector<MidiEvent> events;
  std::array<std::optional<u32>, 8> loopStarts;
  std::array<u8, 16> nrpnMsb{};
  std::array<u8, 16> nrpnLsb{};
  nrpnMsb.fill(127);
  nrpnLsb.fill(127);
  std::array<u8, 16> loopId{};
  u32 cursor = layout.dataOffset;
  u8 runningStatus = 0;
  bool omitDelta = false;
  while (cursor < layout.dataEnd && events.size() < kMaxCommands) {
    MidiEvent event{.offset = cursor};
    if (!omitDelta) {
      const auto delta = readVlq(reader, cursor, layout.dataEnd);
      if (!delta) {
        event.malformed = true;
        event.end = layout.dataEnd;
        events.push_back(std::move(event));
        break;
      }
      event.delta = delta->first;
    }
    omitDelta = false;
    if (cursor >= layout.dataEnd) {
      break;
    }
    u8 byte = reader.u8At(cursor++);
    if ((byte & 0x80) != 0) {
      event.status = byte;
      if (byte < 0xf0) {
        runningStatus = byte;
      }
    } else {
      if (runningStatus == 0) {
        event.malformed = true;
        event.end = cursor;
        events.push_back(std::move(event));
        break;
      }
      event.status = runningStatus;
      --cursor;
    }
    event.channel = event.status & 0x0f;
    const u8 family = event.status & 0xf0;
    auto dataByte = [&]() -> std::optional<u8> {
      if (cursor >= layout.dataEnd) {
        return std::nullopt;
      }
      const u8 raw = reader.u8At(cursor++);
      omitDelta |= (raw & 0x80) != 0;
      return raw & 0x7f;
    };
    if (family == 0x80 || family == 0xc0 || family == 0xd0) {
      const auto first = dataByte();
      if (!first) {
        event.malformed = true;
      } else {
        event.data1 = *first;
        event.dataBytes = 1;
      }
    } else if (family >= 0x90 && family <= 0xe0 && family != 0xa0) {
      const auto first = dataByte();
      const auto second = dataByte();
      if (!first || !second) {
        event.malformed = true;
      } else {
        event.data1 = *first;
        event.data2 = *second;
        event.dataBytes = 2;
      }
    } else if (family == 0xa0 && layout.compression == 1) {
      const auto packed = dataByte();
      if (!packed) {
        event.malformed = true;
      } else {
        const u32 dictionaryIndex = static_cast<u32>((*packed >> 4) * 16 + event.channel) * 2;
        if (dictionaryIndex + 1 >= layout.noteDictionary.size()) {
          event.malformed = true;
        } else {
          event.status = layout.noteDictionary[dictionaryIndex];
          event.channel = event.status & 0x0f;
          event.data1 = layout.noteDictionary[dictionaryIndex + 1] & 0x7f;
          event.data2 = static_cast<u8>(((*packed & 0x0f) << 3) | 7);
          event.dataBytes = 2;
        }
      }
    } else if (family == 0xa0) {
      // In compressed blocks Sony repurposes A0 as a dictionary note. In an
      // ordinary block it retains MIDI's two-byte Polyphonic Key Pressure
      // encoding. modmidi forwards it even though this modhsyn build ignores
      // the message, so it must not terminate parsing of the remaining track.
      const auto first = dataByte();
      const auto second = dataByte();
      if (!first || !second) {
        event.malformed = true;
      } else {
        event.data1 = *first;
        event.data2 = *second;
        event.dataBytes = 2;
      }
    } else if (event.status == 0xff) {
      omitDelta = false;
      if (cursor >= layout.dataEnd) {
        event.malformed = true;
      } else {
        event.metaType = reader.u8At(cursor++);
        const auto length = readVlq(reader, cursor, layout.dataEnd);
        if (!length || length->first > layout.dataEnd - cursor) {
          event.malformed = true;
        } else {
          const auto payload = reader.slice(cursor, length->first);
          event.payload.assign(payload.begin(), payload.end());
          cursor += length->first;
          event.endEvent = event.metaType == 0x2f;
        }
      }
    } else if (event.status == 0xf0 || event.status == 0xf7) {
      omitDelta = false;
      const auto length = readVlq(reader, cursor, layout.dataEnd);
      if (!length || length->first > layout.dataEnd - cursor) {
        event.malformed = true;
      } else {
        const auto payload = reader.slice(cursor, length->first);
        event.payload.assign(payload.begin(), payload.end());
        cursor += length->first;
      }
    } else {
      event.malformed = true;
    }
    event.end = cursor;

    if ((event.status & 0xf0) == 0xb0 && !event.malformed) {
      const u8 channel = event.channel;
      if (event.data1 == 99) {
        nrpnMsb[channel] = event.data2;
      } else if (event.data1 == 98) {
        nrpnLsb[channel] = event.data2;
      } else if (event.data1 == 6 && nrpnMsb[channel] == 0 && event.data2 < 8) {
        loopStarts[event.data2] = event.end;
      } else if (event.data1 == 6 && nrpnMsb[channel] == 1 && event.data2 < 8) {
        loopId[channel] = event.data2;
      } else if (event.data1 == 38 && nrpnMsb[channel] == 1 && loopStarts[loopId[channel]]) {
        event.loopId = loopId[channel];
        event.loopCount = event.data2;
        event.loopDestination = loopStarts[event.loopId];
      }
      (void)nrpnLsb;
    }
    events.push_back(std::move(event));
    if (events.back().endEvent || events.back().malformed) {
      break;
    }
  }
  return events;
}

[[nodiscard]] DecodedBytecodeCommand decodeMidiEvent(ByteReader reader, u32 bytecodeEnd, const MidiEvent& source,
                                                     std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, source.offset, bytecodeEnd, kCommandKindPrefix, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 family = source.status & 0xf0;
  std::string_view label = "MIDI Event";
  SequenceSemantic semantic = SequenceSemantic::State;
  CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback;
  if (source.malformed) {
    label = "Malformed SonyPS2 Event";
    semantic = SequenceSemantic::Unsupported;
    playback = CommandPlaybackStatus::Unsupported;
  } else if (family == 0x80 || family == 0x90) {
    label = source.data2 == 0 || family == 0x80 ? "Note Off" : "Note On";
    semantic = SequenceSemantic::Note;
  } else if (family == 0xa0) {
    label = "Polyphonic Key Pressure";
    semantic = SequenceSemantic::State;
    playback = CommandPlaybackStatus::SourceOnly;
  } else if (source.loopDestination) {
    label = "Loop End";
    semantic = SequenceSemantic::Loop;
    playback = CommandPlaybackStatus::AffectsControlFlow;
  } else if (family == 0xb0) {
    label = "Control Change";
    semantic = SequenceSemantic::State;
    playback = controllerAffectsPlayback(source.data1) ? CommandPlaybackStatus::AffectsPlayback
                                                       : CommandPlaybackStatus::SourceOnly;
  } else if (family == 0xc0) {
    label = "Program Change";
    semantic = SequenceSemantic::Program;
  } else if (family == 0xd0) {
    label = "Channel Pressure";
    semantic = SequenceSemantic::State;
    playback = CommandPlaybackStatus::SourceOnly;
  } else if (family == 0xe0) {
    label = "Pitch Bend";
    semantic = SequenceSemantic::Pitch;
  } else if (source.status == 0xff && source.metaType == 0x51) {
    label = "Tempo";
    semantic = SequenceSemantic::Tempo;
  } else if (source.endEvent) {
    label = "End of Sequence";
    semantic = SequenceSemantic::End;
    playback = CommandPlaybackStatus::StopsPlayback;
  } else if (source.status >= 0xf0) {
    label = source.status == 0xff ? "Meta Event" : "System Exclusive";
    semantic = SequenceSemantic::Meta;
    playback = CommandPlaybackStatus::SourceOnly;
  }
  auto event = cursor.command(label, semantic, playback);
  if (source.end > source.offset + 1) {
    static_cast<void>(event.rawBytes("encoded_bytes", source.end - source.offset - 1));
  }
  event.derived("delta", source.delta, SemanticOperandRole::Duration);
  event.derived("status", source.status, SourceValueDisplay::Hex);
  if (source.status < 0xf0) {
    event.derived("channel", source.channel, SemanticOperandRole::Channel);
  }
  if (source.malformed) {
    return event.stop();
  }
  if (family == 0x80) {
    event.derived("key", source.data1, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    return event.invoke<&Playback::noteOff>(source.channel, source.data1, source.delta);
  }
  if (family == 0x90) {
    event.derived("key", source.data1, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    event.derived("velocity", source.data2, SemanticOperandRole::Level);
    return event.invoke<&Playback::note>(source.channel, source.data1, source.data2, source.delta);
  }
  if (family == 0xb0) {
    event.derived("controller", source.data1);
    event.derived("value", source.data2);
    if (source.loopDestination) {
      const Address destination{*source.loopDestination};
      event.derived("loop_id", source.loopId);
      event.derived("repeat_count", source.loopCount, SemanticOperandRole::Count);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      return event.invokeFlow<&Playback::loop>(source.loopId, source.loopCount, destination, source.delta)
          .mayBranchTo(destination);
    }
    return event.invoke<&Playback::controller>(source.channel, source.data1, source.data2, source.delta);
  }
  if (family == 0xc0) {
    event.derived("program", source.data1, SemanticOperandRole::InstrumentProgram);
    return event.invoke<&Playback::programChange>(source.channel, source.data1, source.delta);
  }
  if (family == 0xe0) {
    const u16 value = static_cast<u16>((source.data2 << 7) | source.data1);
    event.derived("wheel", value, SemanticOperandRole::Pitch);
    return event.invoke<&Playback::pitchBend>(source.channel, value, source.delta);
  }
  if (source.status == 0xff && source.metaType == 0x51 && source.payload.size() == 3) {
    const u32 tempo =
        (static_cast<u32>(source.payload[0]) << 16) | (static_cast<u32>(source.payload[1]) << 8) | source.payload[2];
    event.derived("microseconds_per_quarter", tempo);
    return event.invoke<&Playback::tempo>(tempo, source.delta);
  }
  if (source.endEvent) {
    return event.wait(source.delta).end();
  }
  return event.wait(source.delta);
}

struct SeEvent {
  u32 offset = 0;
  u32 commandOffset = 0;
  u32 end = 0;
  u32 delta = 0;
  u8 opcode = 0;
  u8 set = 0;
  u8 timbre = 0;
  u8 key = 0;
  u8 velocity = 0;
  u8 operation = 0;
  u32 time = 0;
  u32 value = 0;
  u8 loopId = 0;
  u8 loopCount = 0;
  std::optional<u32> loopDestination;
  bool noteOnOnly = false;
  bool endEvent = false;
  bool malformed = false;
};

[[nodiscard]] std::vector<SeEvent> inspectSeEvents(ByteReader reader, const SeSequenceLayout& layout) {
  std::vector<SeEvent> events;
  u32 cursor = layout.dataOffset;
  while (cursor < layout.dataEnd && events.size() < kMaxCommands) {
    SeEvent event{.offset = cursor};
    const auto delta = readVlq(reader, cursor, layout.dataEnd);
    if (!delta || cursor >= layout.dataEnd) {
      event.end = layout.dataEnd;
      event.malformed = true;
      events.push_back(event);
      break;
    }
    event.delta = static_cast<u32>(std::llround(delta->first * 1000.0 / std::max<u16>(layout.timeScale, 1)));
    event.commandOffset = cursor;
    event.opcode = reader.u8At(cursor++);
    event.set = event.opcode & 0x0f;
    const u8 family = event.opcode & 0xf0;
    if (family == 0x90 || family == 0xa0) {
      if (!reader.has(cursor, 3) || cursor + 3 > layout.dataEnd) {
        event.malformed = true;
      } else {
        event.timbre = reader.u8At(cursor++);
        event.key = reader.u8At(cursor++);
        event.velocity = reader.u8At(cursor++);
        event.noteOnOnly = family == 0x90;
      }
    } else if (family == 0xb0) {
      if (!reader.has(cursor, 3) || cursor + 3 > layout.dataEnd) {
        event.malformed = true;
      } else {
        event.timbre = reader.u8At(cursor++);
        event.key = reader.u8At(cursor++);
        event.operation = reader.u8At(cursor++);
        const auto readValue = [&](u32& value) {
          const auto decoded = readVlq(reader, cursor, layout.dataEnd);
          if (!decoded) {
            return false;
          }
          value = decoded->first;
          return true;
        };
        switch (event.operation) {
          case 0x07:
          case 0x0c:
          case 0x0d:
            event.malformed = !readValue(event.time) || cursor >= layout.dataEnd;
            if (!event.malformed) {
              event.value = reader.u8At(cursor++);
            }
            break;
          case 0x0a:
          case 0x0b:
            event.malformed = !readValue(event.time) || !readValue(event.value);
            break;
          case 0x0e:
          case 0x0f:
            event.malformed = !readValue(event.time) || cursor + 2 > layout.dataEnd;
            if (!event.malformed) {
              event.value = reader.le16(cursor);
              cursor += 2;
            }
            break;
          case 0x10:
          case 0x11:
          case 0x20:
          case 0x21:
            event.malformed = cursor + 2 > layout.dataEnd;
            if (!event.malformed) {
              event.value = reader.le16(cursor);
              cursor += 2;
            }
            break;
          case 0x12:
          case 0x22:
            event.malformed = !readValue(event.value);
            break;
          default:
            event.malformed = true;
            break;
        }
      }
    } else if (event.opcode == 0xc0) {
      if (!reader.has(cursor, 5) || cursor + 5 > layout.dataEnd) {
        event.malformed = true;
      } else {
        const u8 mode = reader.u8At(cursor++);
        event.loopId = reader.u8At(cursor++);
        const u16 relative = reader.le16(cursor);
        cursor += 2;
        event.loopCount = reader.u8At(cursor++);
        u32 destination = layout.offset + relative;
        if (mode == 1) {
          // Converted below from command address to its preceding delta.
          destination |= 0x80000000;
        }
        event.loopDestination = destination;
      }
    } else if (event.opcode == 0xff) {
      event.endEvent = true;
    } else {
      event.malformed = true;
    }
    event.end = cursor;
    events.push_back(event);
    if (event.endEvent || event.malformed) {
      break;
    }
  }
  for (auto& event : events) {
    if (!event.loopDestination || (*event.loopDestination & 0x80000000) == 0) {
      continue;
    }
    const u32 commandAddress = *event.loopDestination & 0x7fffffff;
    const auto target = std::ranges::find(events, commandAddress, &SeEvent::commandOffset);
    if (target != events.end()) {
      event.loopDestination = target->offset;
    } else {
      event.malformed = true;
      event.loopDestination.reset();
    }
  }
  return events;
}

[[nodiscard]] DecodedBytecodeCommand decodeSeEvent(ByteReader reader, u32 bytecodeEnd, const SeEvent& source,
                                                   std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, source.offset, bytecodeEnd, kCommandKindPrefix, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 family = source.opcode & 0xf0;
  const bool note = family == 0x90 || family == 0xa0;
  const bool pitchSlide = family == 0xb0 && (source.operation == 0x0e || source.operation == 0x0f);
  auto event = cursor.command(source.malformed         ? "Malformed SeSeq Command"
                              : note                   ? (source.velocity == 0 ? "SE Note Off" : "SE Note On")
                              : source.loopDestination ? "SE Jump"
                              : source.endEvent        ? "End of SeSeq"
                                                       : "SE Voice Automation",
                              source.malformed         ? SequenceSemantic::Unsupported
                              : note                   ? SequenceSemantic::Note
                              : source.loopDestination ? SequenceSemantic::Loop
                              : source.endEvent        ? SequenceSemantic::End
                                                       : SequenceSemantic::State,
                              source.malformed                ? CommandPlaybackStatus::Unsupported
                              : source.endEvent               ? CommandPlaybackStatus::StopsPlayback
                              : family == 0xb0 && !pitchSlide ? CommandPlaybackStatus::SourceOnly
                                                              : CommandPlaybackStatus::AffectsPlayback);
  if (source.end > source.offset + 1) {
    static_cast<void>(event.rawBytes("encoded_bytes", source.end - source.offset - 1));
  }
  event.derived("delta_ms", source.delta, SemanticOperandRole::Duration);
  if (source.malformed) {
    return event.stop();
  }
  if (note) {
    event.derived("set", source.set, SemanticOperandRole::InstrumentBank);
    event.derived("timbre", source.timbre, SemanticOperandRole::InstrumentProgram);
    event.derived("key", source.key, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    event.derived("velocity", source.velocity, SemanticOperandRole::Level);
    return event.invoke<&Playback::seNote>(source.set, source.timbre, source.key, source.velocity, source.noteOnOnly,
                                           source.delta);
  }
  if (source.loopDestination) {
    const Address destination{*source.loopDestination};
    event.derived("loop_id", source.loopId);
    event.derived("repeat_count", source.loopCount, SemanticOperandRole::Count);
    event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
    return event.invokeFlow<&Playback::loop>(source.loopId, source.loopCount, destination, source.delta)
        .mayBranchTo(destination);
  }
  if (source.endEvent) {
    return event.wait(source.delta).end();
  }
  event.derived("set", source.set, SemanticOperandRole::InstrumentBank);
  event.derived("timbre", source.timbre, SemanticOperandRole::InstrumentProgram);
  event.derived("key", source.key, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
  event.derived("operation", source.operation);
  if (source.time != 0 || pitchSlide) {
    event.derived("time_ms", source.time, SemanticOperandRole::Duration);
  }
  event.derived("value", source.value);
  if (pitchSlide) {
    return event.invoke<&Playback::sePitch>(source.set, source.timbre, source.key, static_cast<u16>(source.value),
                                            source.operation == 0x0f, source.time, source.delta);
  }
  // These commands target one active (set,timbre,note) voice. Emitting a
  // generic MIDI fade or LFO update would still lose driver-specific curve,
  // phase, and retargeting semantics, so retain those commands as source data.
  return event.wait(source.delta);
}

}  // namespace

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config = SequenceProgramConfig{
      .commandKindPrefix = std::string(kCommandKindPrefix),
      .timebase = Timebase{.ppqn = 480},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kMaxCommands,
              .initialLevel = 1.0,
              .initialExpression = 1.0,
              .initialPitchBendRangeSemitones = 2,
              .initialTempoMicrosecondsPerQuarter = 500000,
          },
  };
  return config;
}

SequenceRuntime sequenceRuntime(RuntimeConfig config) {
  return makeCompiledRuntime<Cursor, ProgramState>(std::move(config));
}

SequenceProgram parseMidiSequence(ByteReader reader, AssetId id, const MidiBlockLayout& layout,
                                  SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  SequenceProgram program = sequenceConfig().makeProgram();
  program.timebase.ppqn = layout.ppqn;
  program.runtime = sequenceRuntime();
  if (sourceMap != nullptr) {
    sourceMap->header("SonyPS2 MIDI data block", reader.range(layout.offset, layout.dataOffset - layout.offset))
        .kind("sony-ps2-midi-header")
        .owner(ObjectRefs::sequence(id))
        .fieldsAsChildren()
        .field("sequence_offset", reader.range(layout.offset, 4), layout.dataOffset - layout.offset,
               SourceValueDisplay::Address)
        .field("division", reader.range(layout.offset + 4, 2), layout.ppqn);
  }
  const auto events = inspectMidiEvents(reader, layout);
  std::map<u32, const MidiEvent*> byOffset;
  for (const auto& event : events) {
    byOffset.emplace(event.offset, &event);
  }
  TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = layout.dataEnd,
      .maxCommands = kMaxCommands,
      .sourceHasTracks = false,
      .sequenceAsset = id,
      .sourceMap = sourceMap,
  };
  auto track = tracks.decode(0, layout.dataOffset, [&](u32 eventOffset) -> DecodedBytecodeCommand {
    const auto found = byOffset.find(eventOffset);
    if (found == byOffset.end()) {
      Cursor cursor(reader, eventOffset, layout.dataEnd, kCommandKindPrefix, diagnostics);
      return cursor.unsupported("Invalid SonyPS2 MIDI event address").stop();
    }
    return decodeMidiEvent(reader, layout.dataEnd, *found->second, diagnostics);
  });
  track.sourceTrackNumber = 0;
  program.tracks.push_back(track);
  for (u32 channel = 1; channel < 16; ++channel) {
    TrackProgram copy = track;
    copy.sourceTrackNumber = channel;
    program.tracks.push_back(std::move(copy));
  }
  return program;
}

std::optional<SequenceProgram> parseSongSequence(ByteReader reader, AssetId id, const SequenceLayout& layout,
                                                 u32 songOffset, u32 songEnd, SourceMapBuilder* sourceMap,
                                                 std::vector<Diagnostic>* diagnostics) {
  if (layout.midiBlocks.empty() || songOffset >= songEnd) {
    return std::nullopt;
  }
  SequenceProgram program = sequenceConfig().makeProgram();
  program.timebase.ppqn = layout.midiBlocks.front().ppqn;
  program.runtime = sequenceRuntime();
  program.tracks.resize(16);
  for (u32 channel = 0; channel < 16; ++channel) {
    program.tracks[channel].sourceTrackNumber = channel;
    program.tracks[channel].startAddress = Address{layout.midiBlocks.front().dataOffset};
  }
  for (const auto& block : layout.midiBlocks) {
    SequenceProgram section = parseMidiSequence(reader, id, block, nullptr, diagnostics);
    for (u32 channel = 0; channel < 16; ++channel) {
      auto& destination = program.tracks[channel].commands;
      for (auto command : section.tracks[channel].commands) {
        if (command.flow.defaultTransition.kind == CommandTransitionKind::End) {
          command.flow.defaultTransition = CommandTransition::endSection();
        }
        destination.push_back(std::move(command));
      }
    }
  }
  for (auto& track : program.tracks) {
    std::ranges::sort(track.commands, [](const SourceCommand& left, const SourceCommand& right) {
      return left.address.value < right.address.value;
    });
  }

  SectionPlaylist playlist;
  u8 songVolume = 128;
  u8 songPan = 64;
  u8 songTempo = 120;
  bool hasPrefixState = false;
  bool hasPlaybackCommand = false;
  bool hasRepeat = false;
  bool unsupported = false;
  u32 cursor = songOffset;
  while (cursor + 3 <= songEnd && playlist.commands.size() < 4096) {
    const u32 commandOffset = cursor;
    const u8 family = reader.u8At(cursor++);
    const u8 operation = reader.u8At(cursor++);
    const u8 value = reader.u8At(cursor++);
    if (family == 0xa0 && operation >= 1 && operation <= 6) {
      if (hasPlaybackCommand) {
        unsupported = true;
        break;
      }
      hasPrefixState = true;
      if (operation <= 3) {
        const int updated = operation == 1   ? value
                            : operation == 2 ? static_cast<int>(songVolume) + value
                                             : static_cast<int>(songVolume) - value;
        songVolume = static_cast<u8>(std::clamp(updated, 0, 128));
      } else {
        const int updated = operation == 4   ? value
                            : operation == 5 ? static_cast<int>(songPan) + value
                                             : static_cast<int>(songPan) - value;
        songPan = static_cast<u8>(std::clamp(updated, 0, 127));
      }
      continue;
    }
    if (family == 0xa0 && operation >= 0x21 && operation <= 0x23) {
      if (hasPlaybackCommand) {
        unsupported = true;
        break;
      }
      hasPrefixState = true;
      const int updated = operation == 0x21   ? value
                          : operation == 0x22 ? static_cast<int>(songTempo) + value
                                              : static_cast<int>(songTempo) - value;
      songTempo = static_cast<u8>(std::clamp(updated, 10, 255));
      continue;
    }
    if (family == 0xa0 && operation == 0 && value < layout.midiBlocks.size()) {
      hasPlaybackCommand = true;
      if (playlist.commands.empty()) {
        playlist.startAddress = Address{commandOffset};
      }
      PlaylistCommand command{
          .address = Address{commandOffset},
          .fallthrough = Address{cursor},
          .range = reader.range(commandOffset, 3),
          .kind = PlaylistCommandKind::PlaySection,
          .target = Address{layout.midiBlocks[value].dataOffset},
          .trackStarts = std::vector<std::optional<Address>>(16, Address{layout.midiBlocks[value].dataOffset}),
      };
      playlist.commands.push_back(std::move(command));
      continue;
    }
    if (family == 0xa0 && operation == 0x11 && cursor + 3 <= songEnd && reader.u8At(cursor) == 0xa1) {
      hasPlaybackCommand = true;
      hasRepeat = true;
      const u32 suffix = cursor;
      const u16 relative = static_cast<u16>((reader.u8At(cursor + 1) << 8) | reader.u8At(cursor + 2));
      cursor += 3;
      playlist.commands.push_back(PlaylistCommand{
          .address = Address{commandOffset},
          .fallthrough = Address{cursor},
          .range = reader.range(commandOffset, 6),
          .kind = PlaylistCommandKind::Repeat,
          .target = Address{songOffset + relative},
          .additionalPlays = value,
      });
      (void)suffix;
      continue;
    }
    if (family == 0xa0 && operation == 0x7f && value == 0x7f) {
      hasPlaybackCommand = true;
      playlist.commands.push_back(PlaylistCommand{
          .address = Address{commandOffset},
          .fallthrough = Address{cursor},
          .range = reader.range(commandOffset, 3),
          .kind = PlaylistCommandKind::End,
      });
      break;
    }
    unsupported = true;
    break;
  }
  if (playlist.commands.empty() || unsupported || (hasPrefixState && hasRepeat) ||
      playlist.commands.back().kind != PlaylistCommandKind::End) {
    if (diagnostics != nullptr) {
      diagnostics->push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = "SonyPS2 Song uses fades, clears, state changes between MIDI sections, or malformed commands "
                     "not representable by SectionPlaylist",
          .range = reader.range(songOffset, songEnd - songOffset),
      });
    }
    return std::nullopt;
  }
  if (hasPrefixState) {
    if (songVolume != 128) {
      program.behavior.initialMasterLevel = songVolume / 128.0;
    }
    if (songPan != 64) {
      program.behavior.initialChannelPan = ChannelPan{
          .position = panPositionFrom7Bit(songPan),
          .voicePanLaw = PanLaw::ConstantMaximum,
      };
    }
    if (songTempo != 120) {
      program.behavior.initialTempoMicrosecondsPerQuarter = static_cast<u32>(std::llround(60000000.0 / songTempo));
    }
  }
  if (sourceMap != nullptr) {
    sourceMap->header("SonyPS2 Song table", reader.range(songOffset, songEnd - songOffset))
        .kind("sony-ps2-song-table")
        .owner(ObjectRefs::sequence(id));
  }
  program.sectionPlaylist = std::move(playlist);
  return program;
}

std::optional<SequenceProgram> parseSeSequence(ByteReader reader, AssetId id, const SeSequenceLayout& layout,
                                               SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const auto events = inspectSeEvents(reader, layout);
  std::vector<u32> voices;
  for (const auto& event : events) {
    if ((event.opcode & 0xf0) != 0x90 && (event.opcode & 0xf0) != 0xa0) {
      continue;
    }
    const u32 voice = seTrackNumber(event.set, event.timbre, event.key);
    if (std::ranges::find(voices, voice) == voices.end()) {
      voices.push_back(voice);
    }
  }
  if (voices.empty()) {
    return std::nullopt;
  }
  SequenceProgram program = sequenceConfig().makeProgram();
  program.timebase.ppqn = layout.ppqn;
  program.behavior.initialTempoMicrosecondsPerQuarter = 1000000;
  program.behavior.initialLevel = std::min<u8>(layout.volume, 128) / 128.0;
  program.behavior.initialChannelPan = ChannelPan{
      .position = panPositionFrom7Bit(static_cast<u8>(std::clamp<int>(std::abs(layout.pan), 0, 127))),
      .voicePanLaw = PanLaw::ConstantMaximum,
  };
  program.runtime = sequenceRuntime();
  std::map<u32, const SeEvent*> byOffset;
  for (const auto& event : events) {
    byOffset.emplace(event.offset, &event);
  }
  TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = layout.dataEnd,
      .maxCommands = kMaxCommands,
      .sourceHasTracks = false,
      .sequenceAsset = id,
      .sourceMap = sourceMap,
  };
  auto decoded = tracks.decode(0, layout.dataOffset, [&](u32 eventOffset) -> DecodedBytecodeCommand {
    const auto found = byOffset.find(eventOffset);
    if (found == byOffset.end()) {
      Cursor cursor(reader, eventOffset, layout.dataEnd, kCommandKindPrefix, diagnostics);
      return cursor.unsupported("Invalid SonyPS2 SeSeq event address").stop();
    }
    return decodeSeEvent(reader, layout.dataEnd, *found->second, diagnostics);
  });
  for (const u32 voice : voices) {
    TrackProgram copy = decoded;
    copy.sourceTrackNumber = voice;
    program.tracks.push_back(std::move(copy));
  }
  return program;
}

}  // namespace vgmtrans::formats::sony_ps2
