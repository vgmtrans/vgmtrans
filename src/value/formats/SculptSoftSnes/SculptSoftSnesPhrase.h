/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "value/formats/SculptSoftSnes/SculptSoftSnes.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/SequenceVm.h"
#include <algorithm>
#include <map>
#include <utility>

namespace vgmtrans::formats::sculpt_soft_snes {

struct Phrase {
  core::Address start;
  core::Address end;
  u8 count = 0;
  u16 transpose = 0;
  u16 volumeScale = 0x100;
  std::vector<u8> instruments;
};

struct PhraseFrame {
  Phrase phrase;
  u8 iteration = 0;
  u8 instrumentIndex = 0;
  u8 savedVolume = 0;
};

[[nodiscard]] inline u8 scaledVolume(u8 volume, u16 scale) {
  return static_cast<u8>(std::min<u32>(127, (u32(volume) * scale + 128) >> 8));
}

// Phrase calls temporarily scale volume and transpose pitch. Patch changes
// persist after return; replacements apply to the innermost phrase.
class PhraseStack {
public:
  u16 transpose = 0;

  void clear() {
    frames_.clear();
    transpose = 0;
  }
  u8 instrument(u8 value) {
    if (!frames_.empty()) {
      auto& frame = frames_.back();
      if (!frame.phrase.instruments.empty()) {
        const u8 replacement = frame.phrase.instruments[frame.instrumentIndex];
        frame.instrumentIndex = static_cast<u8>((frame.instrumentIndex + 1) % frame.phrase.instruments.size());
        if (replacement != 0xff) {
          value = replacement;
        }
      }
    }
    return value;
  }
  [[nodiscard]] core::Effects call(Phrase phrase, u8& volume, core::VmApi& vm) {
    if (frames_.size() == 5) {
      vm.diagnostic(core::Diagnostic{.severity = core::Severity::Warning,
                                     .message = "SculptSoftSnes phrase stack exceeds five entries",
                                     .range = vm.sourceRange()});
      return vm.end();
    }
    const u8 savedVolume = volume;
    volume = scaledVolume(volume, phrase.volumeScale);
    transpose = static_cast<u16>(transpose + phrase.transpose);
    const auto target = phrase.start;
    frames_.push_back(PhraseFrame{.phrase = std::move(phrase), .savedVolume = savedVolume});
    return vm.call(target);
  }

  [[nodiscard]] std::optional<core::Effects> boundary(u8& volume, core::VmApi& vm) {
    if (frames_.empty() || frames_.back().phrase.end.value != vm.sourceRange().offset) {
      return std::nullopt;
    }
    auto& frame = frames_.back();
    ++frame.iteration;
    if (frame.phrase.count == 0) {
      return vm.loopCandidate(frame.phrase.start);
    }
    if (frame.iteration != frame.phrase.count) {
      return vm.finiteBranch(frame.phrase.start);
    }
    volume = frame.savedVolume;
    transpose = static_cast<u16>(transpose - frame.phrase.transpose);
    frames_.pop_back();
    return vm.return_();
  }

private:
  std::vector<PhraseFrame> frames_;
};

// Phrase returns are address comparisons, not opcodes. Decode each bounded
// phrase once per entry/end pair, retaining the first interpretation of each
// command. Only the late decoder changes the fine-pitch stream state.
template <class Playback, class DecodeCommand>
[[nodiscard]] core::TrackProgram decodePhraseTrack(const core::TrackDecodeScope& scope, u32 number, u32 start,
                                                   std::vector<core::Diagnostic>* diagnostics,
                                                   DecodeCommand decodeCommand) {
  using namespace core;
  struct Decoded {
    DecodedBytecodeCommand command;
    bool fine = false;
    bool nextFine = false;
  };
  std::map<u32, Decoded> commands;
  std::vector<std::pair<u32, u32>> pending{{start, kAramSize}};
  std::set<u32> ends;
  std::set<std::pair<u32, u32>> visited;
  while (!pending.empty() && commands.size() < kCommandLimit) {
    const auto [begin, end] = pending.back();
    pending.pop_back();
    if (!visited.emplace(begin, end).second) {
      continue;
    }
    bool fine = false;
    for (u32 offset = begin; offset < end && commands.size() < kCommandLimit;) {
      auto existing = commands.find(offset);
      if (existing == commands.end()) {
        const bool initialFine = fine;
        std::vector<Phrase> phrases;
        auto command = decodeCommand(offset, fine, phrases);
        for (const auto& phrase : phrases) {
          pending.emplace_back(phrase.start.value, phrase.end.value);
          ends.insert(phrase.end.value);
        }
        existing = commands.emplace(offset, Decoded{std::move(command), initialFine, fine}).first;
      } else if (existing->second.fine != fine) {
        if (diagnostics) {
          diagnostics->push_back(Diagnostic{.severity = Severity::Warning,
                                            .message = "Conflicting SculptSoftSnes fine-pitch stream interpretations",
                                            .range = scope.reader.range(offset, 1)});
        }
        break;
      }
      // Reuse the decoded state change; do not interpret the opcode a second time.
      fine = existing->second.nextFine;
      const auto next = existing->second.command.flow.discoveryContinuation();
      if (!next || next->value <= offset) {
        break;
      }
      offset = next->value;
    }
  }
  for (const u32 end : ends) {
    commands.try_emplace(end, Decoded{
        .command = {.range = scope.reader.range(end, 0),
                    .flow = {.continuation = Address{end}, .defaultTransition = CommandTransition::return_()},
                    .presentation = {.label = "Phrase End",
                                     .kind = "sculpt-soft-snes-phrase-end",
                                     .semantic = SequenceSemantic::Return,
                                     .playback = CommandPlaybackStatus::AffectsControlFlow}},
    });
  }
  auto session = scope.begin(number, start);
  for (auto& [address, decoded] : commands) {
    auto& command = decoded.command;
    // Fine-pitch bytes are inside a command stream; ordinary commands and
    // synthetic phrase ends must check the exclusive boundary before executing.
    if (!decoded.fine || !command.execution.body) {
      auto body = std::move(command.execution.body);
      command.execution.body = [body = std::move(body)](void* state) {
        auto& playback = *static_cast<Playback*>(state);
        if (const auto boundary = playback.phraseBoundary()) {
          return *boundary;
        }
        return body ? body(state) : playback.vm.end();
      };
    }
    session.findOrAppend(std::move(command), address);
  }
  return session.finish();
}

template <class Cursor>
Phrase readPhrase(Cursor& cursor, std::set<u8>* referencedPrograms) {
  Phrase phrase;
  phrase.start = cursor.addressLe("start", core::SemanticOperandRole::CallTarget);
  phrase.end = cursor.addressLe("end");
  phrase.count = cursor.u8("plays (0 = loop)");
  phrase.transpose = cursor.u16le("pitch offset (1/20 semitone)");
  phrase.volumeScale = cursor.u16le("volume multiplier (8.8)");
  const u8 instruments = cursor.u8("instrument replacements");
  for (u32 i = 0; i < instruments; ++i) {
    const u8 replacement = cursor.u8("replacement", core::SemanticOperandRole::Instrument);
    phrase.instruments.push_back(replacement);
    if (referencedPrograms && replacement != 0xff) {
      referencedPrograms->insert(replacement);
    }
  }
  return phrase;
}

}  // namespace vgmtrans::formats::sculpt_soft_snes
