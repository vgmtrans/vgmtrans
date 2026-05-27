/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "MidiMerge.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <numeric>
#include <set>
#include <unordered_map>
#include <utility>

#include "Types.h"
#include "ConversionContext.h"
#include "LogManager.h"
#include "MidiFile.h"
#include "Options.h"
#include "SF2Conversion.h"
#include "SF2File.h"
#include "SynthFile.h"
#include "VGMColl.h"
#include "VGMInstrSet.h"
#include "VGMSeq.h"

namespace conversion {

namespace {

u8 normalizeSf2Bank(u32 bank) {
  const u16 bank16 = static_cast<u16>(bank);
  if (bank16 > 128) {
    return static_cast<u8>((bank16 >> 8) & 0x7F);
  }
  return static_cast<u8>(bank16 & 0x7F);
}

u64 rescaleTick(u32 tick, u16 srcPPQN, u16 dstPPQN) {
  if (srcPPQN == 0 || dstPPQN == 0 || srcPPQN == dstPPQN) {
    return tick;
  }

  return (static_cast<u64>(tick) * static_cast<u64>(dstPPQN) + (srcPPQN / 2u)) / srcPPQN;
}

u32 getMidiDurationTicks(const MidiFile& midi) {
  u32 maxTick = 0;

  for (const MidiTrack* track : midi.aTracks) {
    if (!track) {
      continue;
    }

    for (const MidiEvent* event : track->aEvents) {
      if (event) {
        maxTick = std::max(maxTick, event->absTime);
      }
    }
  }

  for (const MidiEvent* event : midi.globalTrack.aEvents) {
    if (event) {
      maxTick = std::max(maxTick, event->absTime);
    }
  }

  return maxTick;
}

void retimeTrack(MidiTrack* track, u16 srcPPQN, u16 dstPPQN, u32 startTick) {
  if (!track) {
    return;
  }

  for (MidiEvent* event : track->aEvents) {
    if (!event) {
      continue;
    }

    event->absTime = static_cast<u32>(rescaleTick(event->absTime, srcPPQN, dstPPQN) + startTick);
  }
}

bool applyBankOffsetToTrack(MidiTrack* track,
                            u8 bankOffset,
                            u32 startTick,
                            const ConversionContext& context) {
  if (!track) {
    return true;
  }

  const bool useMmaBanks = context.bankSelectStyle == BankSelectStyle::MMA;
  std::array<bool, 16> usedChannels {};
  std::array<u16, 16> sourceBanks {};
  auto updateSourceBank = [useMmaBanks](u16& sourceBank,
                                        MidiEventType eventType,
                                        u8 dataByte) {
    if (!useMmaBanks) {
      if (eventType == MIDIEVENT_BANKSELECT) {
        sourceBank = dataByte;
      }
      return;
    }

    if (eventType == MIDIEVENT_BANKSELECT) {
      sourceBank = static_cast<u16>((dataByte << 7) | (sourceBank & 0x7F));
    } else {
      sourceBank = static_cast<u16>((sourceBank & 0x3F80) | dataByte);
    }
  };
  auto remapBankByte = [useMmaBanks, bankOffset](u16 sourceBank,
                                                 MidiEventType eventType,
                                                 u8& dataByte) {
    const u16 remappedBank = static_cast<u16>(sourceBank + bankOffset);
    if (useMmaBanks) {
      if (remappedBank > 0x3FFF) {
        L_ERROR("Bank remap overflowed the MIDI bank-select range.");
        return false;
      }
      dataByte = static_cast<u8>(
          eventType == MIDIEVENT_BANKSELECT ? ((remappedBank >> 7) & 0x7F) : (remappedBank & 0x7F));
      return true;
    }

    if (remappedBank > 127) {
      L_ERROR("Bank remap overflowed the MIDI/SF2 bank range.");
      return false;
    }
    dataByte = static_cast<u8>(eventType == MIDIEVENT_BANKSELECT ? remappedBank : 0);
    return true;
  };

  for (MidiEvent* event : track->aEvents) {
    if (!event) {
      continue;
    }

    if (!event->isMetaEvent() && !event->isSysexEvent()) {
      usedChannels[event->channel & 0x0F] = true;
    }

    const MidiEventType eventType = event->eventType();
    if (eventType != MIDIEVENT_BANKSELECT && eventType != MIDIEVENT_BANKSELECTFINE) {
      continue;
    }

    auto* controller = dynamic_cast<ControllerEvent*>(event);
    if (!controller) {
      continue;
    }

    const u8 channel = controller->channel & 0x0F;
    updateSourceBank(sourceBanks[channel], eventType, controller->dataByte);
    if (!remapBankByte(sourceBanks[channel], eventType, controller->dataByte)) {
      return false;
    }
  }

  std::vector<MidiEvent*> injectedBankEvents;
  injectedBankEvents.reserve(32);
  u8 remappedBankMsb = 0;
  u8 remappedBankLsb = 0;
  if (!remapBankByte(0, MIDIEVENT_BANKSELECT, remappedBankMsb) ||
      !remapBankByte(0, MIDIEVENT_BANKSELECTFINE, remappedBankLsb)) {
    return false;
  }

  for (u8 channel = 0; channel < 16; ++channel) {
    if (!usedChannels[channel]) {
      continue;
    }
    injectedBankEvents.push_back(new BankSelectEvent(track, channel, startTick, remappedBankMsb));
    injectedBankEvents.push_back(new BankSelectFineEvent(track, channel, startTick, remappedBankLsb));
  }

  // Keep injected bank selects ahead of same-tick program changes when priorities are equal
  track->aEvents.insert(track->aEvents.begin(), injectedBankEvents.begin(), injectedBankEvents.end());

  return true;
}

class ExportPrepGuard {
public:
  ExportPrepGuard(std::vector<VGMInstrSet*> instrsets, const VGMColl* coll)
      : m_instrsets(std::move(instrsets)) {
    for (VGMInstrSet* instrset : m_instrsets) {
      if (instrset) {
        instrset->prepareForExport(coll);
      }
    }
  }

  ~ExportPrepGuard() {
    for (VGMInstrSet* instrset : m_instrsets) {
      if (instrset) {
        instrset->cleanupAfterExport();
      }
    }
  }

private:
  std::vector<VGMInstrSet*> m_instrsets;
};

bool computeMaxCollectionBank(const VGMColl* coll, u8& outMaxBank) {
  if (!coll) {
    L_ERROR("Missing collection while planning bank remap.");
    return false;
  }

  const auto instrsets = coll->instrSets();
  if (instrsets.empty()) {
    L_ERROR("Collection has no instrument sets, so a merged SF2 cannot be produced.");
    return false;
  }

  ExportPrepGuard guard(instrsets, coll);
  u8 maxBank = 0;

  for (VGMInstrSet* instrset : instrsets) {
    if (!instrset) {
      continue;
    }
    for (VGMInstr* instr : instrset->exportInstrs()) {
      if (!instr) {
        continue;
      }
      maxBank = std::max(maxBank, normalizeSf2Bank(instr->bank));
    }
  }

  outMaxBank = maxBank;
  return true;
}

}  // namespace

bool planChunkBankOffsets(const std::vector<MidiMergeEntry>& entries,
                          std::vector<u8>& bankOffsets) {
  if (entries.empty()) {
    L_ERROR("No sequences were provided for bank planning.");
    return false;
  }

  bankOffsets.clear();
  bankOffsets.reserve(entries.size());

  u16 nextBankBase = 0;
  std::unordered_map<const VGMColl*, u8> collToBankBase;

  for (const MidiMergeEntry& entry : entries) {
    const VGMColl* coll = entry.collection;
    if (!coll) {
      L_ERROR("Each stitched sequence must belong to a collection in order to build SF2.");
      return false;
    }

    auto existing = collToBankBase.find(coll);
    if (existing != collToBankBase.end()) {
      bankOffsets.push_back(existing->second);
      continue;
    }

    u8 maxBank = 0;
    if (!computeMaxCollectionBank(coll, maxBank)) {
      return false;
    }

    if (nextBankBase + maxBank > 127) {
      L_ERROR("Not enough MIDI bank space (0-127) to assign unique banks per chunk.");
      return false;
    }

    const u8 assignedBase = static_cast<u8>(nextBankBase);
    collToBankBase.emplace(coll, assignedBase);
    bankOffsets.push_back(assignedBase);
    nextBankBase += static_cast<u16>(maxBank) + 1;
  }

  return true;
}

std::unique_ptr<MidiFile> mergeMidiSequences(const std::vector<MidiMergeEntry>& entries,
                                             const MidiMergeOptions& options,
                                             MidiMergeResult* result) {
  if (entries.empty()) {
    L_ERROR("No sequences were provided for MIDI merge.");
    return nullptr;
  }

  const VGMColl* firstCollection = entries[0].collection;
  if (!firstCollection) {
    L_ERROR("The first stitched entry has no collection.");
    return nullptr;
  }

  VGMSeq* firstSequence = firstCollection->seq();
  if (!firstSequence) {
    L_ERROR("The first stitched collection has no sequence.");
    return nullptr;
  }

  if (!options.startTimes.empty() && options.startTimes.size() != entries.size()) {
    L_ERROR("The number of custom start times must match the number of sequences.");
    return nullptr;
  }

  if (!options.bankOffsets.empty() && options.bankOffsets.size() != entries.size()) {
    L_ERROR("The number of bank offsets must match the number of sequences.");
    return nullptr;
  }

  struct ConvertedPart {
    std::unique_ptr<MidiFile> midi;
    u16 ppqn = 0;
    u32 durationTicks = 0;
  };

  std::vector<ConvertedPart> parts;
  parts.reserve(entries.size());

  const auto context = ConversionContext::fromOptions(ConversionOptions::the(), SynthTarget::SoundFont);

  u16 targetPPQN = 0;

  for (size_t i = 0; i < entries.size(); ++i) {
    const MidiMergeEntry& entry = entries[i];
    const VGMColl* coll = entry.collection;
    if (!coll) {
      L_ERROR("Encountered an entry with no collection while preparing merge.");
      return nullptr;
    }

    VGMSeq* seq = coll->seq();
    if (!seq) {
      L_ERROR("Encountered a collection with no sequence while preparing merge.");
      return nullptr;
    }

    std::unique_ptr<MidiFile> midi(seq->convertToMidi(coll, context));
    if (!midi) {
      L_ERROR("Failed to convert one of the source sequences to MIDI.");
      return nullptr;
    }

    u16 ppqn = static_cast<u16>(midi->ppqn());
    if (ppqn == 0) {
      ppqn = 48;
      midi->setPPQN(ppqn);
    }

    if (targetPPQN == 0) {
      targetPPQN = ppqn;
    } else {
      const u64 lcm = std::lcm(static_cast<u64>(targetPPQN), static_cast<u64>(ppqn));
      if (lcm <= 1920) {
        targetPPQN = static_cast<u16>(lcm);
      } else {
        targetPPQN = std::max(targetPPQN, ppqn);
      }
    }

    ConvertedPart part;
    part.durationTicks = getMidiDurationTicks(*midi);
    part.ppqn = ppqn;
    part.midi = std::move(midi);
    parts.push_back(std::move(part));
  }

  if (targetPPQN == 0) {
    targetPPQN = 48;
  }

  std::vector<u32> startTicks(entries.size(), 0);
  if (!options.startTimes.empty()) {
    startTicks = options.startTimes;
  } else {
    u64 cursor = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
      if (cursor > std::numeric_limits<u32>::max()) {
        L_ERROR("Merged MIDI exceeds maximum 32-bit tick duration.");
        return nullptr;
      }
      startTicks[i] = static_cast<u32>(cursor);
      const u64 durationInTarget = rescaleTick(parts[i].durationTicks, parts[i].ppqn, targetPPQN);
      cursor += durationInTarget;
      cursor += options.sequentialGapTicks;
    }
    if (cursor > std::numeric_limits<u32>::max()) {
      L_ERROR("Merged MIDI exceeds maximum 32-bit tick duration.");
      return nullptr;
    }
  }

  auto mergedMidi = std::make_unique<MidiFile>(firstSequence);
  mergedMidi->setPPQN(targetPPQN);

  for (size_t i = 0; i < parts.size(); ++i) {
    MidiFile* source = parts[i].midi.get();
    const u16 sourcePPQN = parts[i].ppqn;
    const u32 startTick = startTicks[i];
    const u8 bankOffset = options.bankOffsets.empty() ? 0 : options.bankOffsets[i];

    retimeTrack(&source->globalTrack, sourcePPQN, targetPPQN, startTick);
    for (MidiEvent* event : source->globalTrack.aEvents) {
      if (!event) {
        continue;
      }

      event->prntTrk = &mergedMidi->globalTrack;
      mergedMidi->globalTrack.aEvents.push_back(event);
    }
    source->globalTrack.aEvents.clear();

    for (MidiTrack*& track : source->aTracks) {
      if (!track) {
        continue;
      }

      retimeTrack(track, sourcePPQN, targetPPQN, startTick);
      if (!options.bankOffsets.empty() &&
          !applyBankOffsetToTrack(track, bankOffset, startTick, context)) {
        return nullptr;
      }

      track->parentSeq = mergedMidi.get();
      mergedMidi->aTracks.push_back(track);
      track = nullptr;
    }
    source->aTracks.clear();
  }

  if (result) {
    result->ppqn = targetPPQN;
    result->startTimes = std::move(startTicks);
    result->bankOffsets = options.bankOffsets;
  }

  return mergedMidi;
}

bool saveMergedSoundfont(const std::vector<MidiMergeEntry>& entries,
                         const std::vector<u8>& bankOffsets,
                         const std::filesystem::path& filepath) {
  if (entries.empty()) {
    L_ERROR("No sequences were provided for merged SF2 export.");
    return false;
  }

  if (bankOffsets.size() != entries.size()) {
    L_ERROR("Bank offset count does not match sequence count for SF2 export.");
    return false;
  }

  auto mergedSynth = std::make_unique<SynthFile>("Merged Chunk Soundfont");
  const auto context = ConversionContext::fromOptions(ConversionOptions::the(), SynthTarget::SoundFont);
  std::set<const VGMColl*> processedColls;

  for (size_t i = 0; i < entries.size(); ++i) {
    const VGMColl* coll = entries[i].collection;
    if (!coll) {
      L_ERROR("Each stitched sequence must belong to a collection in order to build SF2.");
      return false;
    }

    if (processedColls.contains(coll)) {
      continue;
    }
    processedColls.insert(coll);

    const auto instrsets = coll->instrSets();
    const auto& sampcolls = coll->sampColls();

    if (instrsets.empty()) {
      L_ERROR("Collection has no instrument sets, so a merged SF2 cannot be produced.");
      return false;
    }

    ExportPrepGuard guard(instrsets, coll);
    std::unique_ptr<SynthFile> partSynth(createSynthFile(instrsets, sampcolls));
    if (!partSynth) {
      L_ERROR("Failed to build a temporary SynthFile for one stitched chunk.");
      return false;
    }

    const u8 bankOffset = bankOffsets[i];
    for (SynthInstr* instr : partSynth->vInstrs) {
      if (!instr) {
        continue;
      }
      const u16 remapped = static_cast<u16>(normalizeSf2Bank(instr->ulBank)) + bankOffset;
      if (remapped > 127) {
        L_ERROR("Bank remap overflowed while assembling the merged SF2.");
        return false;
      }
      instr->ulBank = remapped;
    }

    const u32 waveOffset = static_cast<u32>(mergedSynth->vWaves.size());

    for (SynthInstr* instr : partSynth->vInstrs) {
      if (!instr) {
        continue;
      }
      for (SynthRgn* rgn : instr->vRgns) {
        if (rgn) {
          rgn->tableIndex += waveOffset;
        }
      }
      mergedSynth->vInstrs.push_back(instr);
    }
    for (SynthWave* wave : partSynth->vWaves) {
      if (wave) {
        mergedSynth->vWaves.push_back(wave);
      }
    }
    partSynth->vInstrs.clear();
    partSynth->vWaves.clear();
  }

  if (mergedSynth->vInstrs.empty() || mergedSynth->vWaves.empty()) {
    L_ERROR("Merged SF2 has no instruments or no samples.");
    return false;
  }

  SF2File sf2(mergedSynth.get(), context);
  const bool success = sf2.saveSF2File(filepath);
  if (!success) {
    L_ERROR("Failed to save merged SF2 file.");
  }
  return success;
}

}  // namespace conversion
