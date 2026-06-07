/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMInstrSet.h"

#include "base/Types.h"
#include "Format.h"
#include "Helper.h"
#include "LogManager.h"
#include "Root.h"
#include "VGMColl.h"
#include "VGMRgn.h"
#include "VGMSamp.h"
#include "VGMSampColl.h"

#include <utility>

#include <spdlog/fmt/fmt.h>

// ***********
// VGMInstrSet
// ***********

VGMInstrSet::VGMInstrSet(const std::string &format, RawFile *file, u32 offset, u32 length,
                         std::string name, VGMSampColl *sampColl)
    : VGMFile(format, file, offset, length, std::move(name)),
      m_sampColl(sampColl) {
}

VGMInstrSet::~VGMInstrSet() = default;

VGMInstr *VGMInstrSet::addInstr(u32 offset, u32 length, u32 bank,
                                u32 instrNum, const std::string &instrName) {
  return addInstr<VGMInstr>(
      this, offset, length, bank, instrNum,
      instrName.empty() ? fmt::format("Instrument {}", instrCount()) : instrName);
}

VGMInstr* VGMInstrSet::sinkInstr(std::unique_ptr<VGMInstr>&& instr) {
  auto* rawInstr = instr.get();
  m_instrs.push_back(rawInstr);
  m_ownedInstrs.emplace_back(std::move(instr));
  return rawInstr;
}

VGMInstr* VGMInstrSet::sinkInstrAsChild(std::unique_ptr<VGMInstr>&& instr) {
  return sinkInstrAsChild(*this, std::move(instr));
}

VGMInstr* VGMInstrSet::sinkInstrAsChild(VGMItem& parent, std::unique_ptr<VGMInstr>&& instr) {
  auto* rawInstr = instr.get();
  m_instrs.push_back(rawInstr);
  parent.sinkChild(std::move(instr));
  return rawInstr;
}

std::vector<std::unique_ptr<VGMInstr>> VGMInstrSet::releaseInstrs() {
  m_instrs.clear();
  return std::exchange(m_ownedInstrs, {});
}

void VGMInstrSet::clearInstrs() {
  m_instrs.clear();
  m_ownedInstrs.clear();
}

bool VGMInstrSet::load() {
  if (!parseHeader())
    return false;
  if (!parseInstrPointers()) {
    clearInstrs();
    return false;
  }
  if (!loadInstrs()) {
    clearInstrs();
    return false;
  }

  if (m_auto_add_instruments_as_children) {
    for (auto& instr : m_ownedInstrs) {
      sinkChild(std::move(instr));
    }
    m_ownedInstrs.clear();
  }

  if (length() == 0) {
    setGuessedLength();
  }

  if (m_sampColl != nullptr) {
    if (!m_sampColl->load()) {
      L_WARN("Failed to load VGMSampColl");
    } else {
      m_sampColl->transferChildren(this);
    }
  }

  return true;
}

bool VGMInstrSet::parseHeader() {
  return true;
}

bool VGMInstrSet::parseInstrPointers() {
  return true;
}

bool VGMInstrSet::loadInstrs() {
  for (auto* instr : m_instrs) {
    if (!instr->loadInstr())
      return false;
  }
  return true;
}

void VGMInstrSet::prepareForExport(const VGMColl* coll) {
  cleanupAfterExport();
  m_exportInstrs = m_instrs;
  if (coll != nullptr) {
    if (coll->seq() == nullptr) {
      L_DEBUG("Collection is missing a sequence. This is unusual.");
    }
    useColl(coll);
  }
}

void VGMInstrSet::cleanupAfterExport() {
  unuseColl();
  m_exportInstrs.clear();
  m_tempInstrs.clear();
}

const std::vector<VGMInstr*>& VGMInstrSet::exportInstrs() const {
  return m_exportInstrs.empty() ? m_instrs : m_exportInstrs;
}

void VGMInstrSet::sinkTempInstr(std::unique_ptr<VGMInstr>&& instr) {
  assert(instr != nullptr);
  m_exportInstrs.push_back(instr.get());
  m_tempInstrs.emplace_back(std::move(instr));
}

void VGMInstrSet::attachSampColl(VGMSampColl* newSampColl) {
  m_ownedSampColl.reset();
  m_sampColl = newSampColl;
}

void VGMInstrSet::sinkSampColl(std::unique_ptr<VGMSampColl>&& newSampColl) {
  m_sampColl = newSampColl.get();
  m_ownedSampColl = std::move(newSampColl);
}

void VGMInstrSet::clearSampColl() {
  m_ownedSampColl.reset();
  m_sampColl = nullptr;
}

// ********
// VGMInstr
// ********

VGMInstr::VGMInstr(VGMInstrSet *instrSet, u32 offset, u32 length, u32 bank,
                   u32 instrNum, std::string name, float reverb)
    : VGMItem(instrSet, offset, length, std::move(name), Type::Instrument),
      bank(bank), instrNum(instrNum), parInstrSet(instrSet), reverb(reverb) {
}

VGMInstr::~VGMInstr() = default;

void VGMInstr::setBank(u32 bankNum) {
  bank = bankNum;
}

void VGMInstr::setInstrNum(u32 theInstrNum) {
  instrNum = theInstrNum;
}

VGMRgn *VGMInstr::sinkRgn(std::unique_ptr<VGMRgn>&& rgn) {
  auto* rawRgn = rgn.get();
  m_regions.emplace_back(rawRgn);
  if (m_auto_add_regions_as_children) {
    sinkChild(std::move(rgn));
  } else {
    m_ownedRegions.emplace_back(std::move(rgn));
  }
  return rawRgn;
}

VGMRgn *VGMInstr::addRgn(u32 offset, u32 length, int sampNum, u8 keyLow,
                         u8 keyHigh, u8 velLow, u8 velHigh) {
  return addRgn<VGMRgn>(this, offset, length, keyLow, keyHigh, velLow, velHigh, sampNum);
}

void VGMInstr::deleteRegions() {
  m_ownedRegions.clear();
  m_regions.clear();
}

// Modulator methods

void VGMInstr::addModulator(ModSource source, ModDest destination, ModAmount amount) {
  if (!amount.valid()) {
    return;
  }

  m_modulators.emplace_back(source, destination, amount.value());
}

void VGMInstr::addModulator(ModDest destination, ModAmount amount) {
  if (!amount.valid()) {
    return;
  }

  m_modulators.emplace_back(destination, amount.value());
}

bool VGMInstr::updateModulatorAmount(ModSource source, ModDest destination, ModAmount amount) {
  if (!amount.valid()) {
    return false;
  }

  for (auto& modulator : m_modulators) {
    if (modulator.source.has_value() && *modulator.source == source &&
        modulator.destination == destination) {
      modulator.amount = amount.value();
      return true;
    }
  }
  return false;
}

bool VGMInstr::updateModulatorAmount(ModDest destination, ModAmount amount) {
  if (!amount.valid()) {
    return false;
  }

  for (auto& modulator : m_modulators) {
    if (!modulator.source.has_value() && modulator.destination == destination) {
      modulator.amount = amount.value();
      return true;
    }
  }
  return false;
}

void VGMInstr::addStandardVibratoHandling(double maxDepthCents,
                                          double minHertz,
                                          double maxHertz,
                                          std::optional<DelayRange> delayRange) {
  addStandardVibratoHandling(VibratoModulationSpec { maxDepthCents, minHertz, maxHertz, delayRange });
}

void VGMInstr::addStandardVibratoHandling(const VibratoModulationSpec& spec) {
  // nullify default channel pressure to vib lfo pitch modulator
  addModulator(ModSource::ChannelPressure, ModDest::VibLfoToPitch, ModAmount::fromCents(0));
  addModulator(ModDest::VibLfoToPitch, ModAmount::fromCents(spec.maxDepthCents));
  addGenerator(ModDest::VibLfoFreq, ModAmount::fromHertz(spec.minHertz));
  addModulator(ModDest::VibLfoFreq,
               ModAmount::fromHertzRange(spec.minHertz, spec.maxHertz));
  if (spec.delayRange.has_value()) {
    const double minDelaySeconds = clampSecondsRangeMinimum(spec.delayRange->minSeconds);
    addGenerator(ModDest::VibLfoDelay, ModAmount::fromSeconds(minDelaySeconds));
    addModulator(ModDest::VibLfoDelay,
                 ModAmount::fromSecondsRange(minDelaySeconds, spec.delayRange->maxSeconds));
  }
}

void VGMInstr::updateStandardVibratoHandling(double maxDepthCents,
                                             double minHertz,
                                             double maxHertz) {
  updateStandardVibratoHandling(VibratoModulationSpec { maxDepthCents, minHertz, maxHertz, });
}

void VGMInstr::updateStandardVibratoHandling(const VibratoModulationSpec& spec) {
  updateModulatorAmount(ModDest::VibLfoToPitch,
                        ModAmount::fromCents(spec.maxDepthCents));
  updateModulatorAmount(ModDest::VibLfoFreq,
                        ModAmount::fromHertzRange(spec.minHertz, spec.maxHertz));
}

void VGMInstr::addStandardTremoloHandling(double maxDepthDb,
                                         double minHertz,
                                         double maxHertz,
                                         TremoloGainMode gainMode) {
  addStandardTremoloHandling(TremoloModulationSpec { maxDepthDb, minHertz, maxHertz, gainMode });
}

void VGMInstr::addStandardTremoloHandling(const TremoloModulationSpec& spec) {
  addGenerator(ModDest::ModLfoFreq, ModAmount::fromHertz(spec.minHertz));
  addModulator(ModDest::ModLfoFreq,
               ModAmount::fromHertzRange(spec.minHertz, spec.maxHertz));
  if (spec.delayRange.has_value()) {
    const double minDelaySeconds = clampSecondsRangeMinimum(spec.delayRange->minSeconds);
    addGenerator(ModDest::ModLfoDelay, ModAmount::fromSeconds(minDelaySeconds));
    addModulator(ModDest::ModLfoDelay,
                 ModAmount::fromSecondsRange(minDelaySeconds, spec.delayRange->maxSeconds));
  }
  addModulator(ModDest::ModLfoToVol,
               ModAmount::fromDecibels(spec.maxDepthDb));
  if (spec.gainMode == TremoloGainMode::NoBoost) {
    addModulator(ModDest::InitialAtten,
                 ModAmount::fromDecibels(spec.maxDepthDb));
  }
}

// Generator methods

void VGMInstr::addGenerator(ModDest destination, ModAmount amount) {
  if (!amount.valid()) {
    return;
  }

  m_generators.push_back({destination, amount.value()});
}

void VGMInstr::addGlobalVibratoFrequency(double hertz) {
  addGenerator(ModDest::VibLfoFreq, ModAmount::fromHertz(hertz));
}

void VGMInstr::addGlobalTremoloFrequency(double hertz) {
  addGenerator(ModDest::ModLfoFreq, ModAmount::fromHertz(hertz));
}
