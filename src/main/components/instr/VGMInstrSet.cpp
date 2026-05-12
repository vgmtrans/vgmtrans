/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMInstrSet.h"
#include <spdlog/fmt/fmt.h>
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMRgn.h"
#include "VGMColl.h"
#include "Root.h"
#include "Format.h"
#include "LogManager.h"
#include "helper.h"

// ***********
// VGMInstrSet
// ***********

VGMInstrSet::VGMInstrSet(const std::string &format, RawFile *file, uint32_t offset, uint32_t length,
                         std::string name, VGMSampColl *theSampColl)
    : VGMFile(format, file, offset, length, std::move(name)),
      sampColl(theSampColl) {
}

VGMInstrSet::~VGMInstrSet() {
  deleteVect(m_tempInstrs);
  delete sampColl;
}

VGMInstr *VGMInstrSet::addInstr(uint32_t offset, uint32_t length, uint32_t bank,
                                uint32_t instrNum, const std::string &instrName) {
  VGMInstr *instr =
      new VGMInstr(this, offset, length, bank, instrNum,
                   instrName.empty() ? fmt::format("Instrument {}", aInstrs.size()) : instrName);
  aInstrs.push_back(instr);
  return instr;
}

bool VGMInstrSet::loadVGMFile(bool useMatcher) {
  bool val = load();
  if (!val) {
    return false;
  }

  if (useMatcher) {
    if (auto fmt = format(); fmt) {
      fmt->onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>(this));
    }
  }

  return val;
}

bool VGMInstrSet::load() {
  if (!parseHeader())
    return false;
  if (!parseInstrPointers()) {
    deleteVect(aInstrs);
    return false;
  }
  if (!loadInstrs()) {
    deleteVect(aInstrs);
    return false;
  }

  if (m_auto_add_instruments_as_children)
    addChildren(aInstrs);

  if (length() == 0) {
    setGuessedLength();
  }

  if (sampColl != nullptr) {
    if (!sampColl->load()) {
      L_WARN("Failed to load VGMSampColl");
    } else {
      sampColl->transferChildren(this);
    }
  }

  rawFile()->addContainedVGMFile(
      std::make_shared<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>>(this));
  pRoot->addVGMFile(this);
  return true;
}

bool VGMInstrSet::parseHeader() {
  return true;
}

bool VGMInstrSet::parseInstrPointers() {
  return true;
}

bool VGMInstrSet::loadInstrs() {
  size_t nInstrs = aInstrs.size();
  for (size_t i = 0; i < nInstrs; i++) {
    if (!aInstrs[i]->loadInstr())
      return false;
  }
  return true;
}

void VGMInstrSet::prepareForExport(const VGMColl* coll) {
  cleanupAfterExport();
  m_exportInstrs = aInstrs;
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
  deleteVect(m_tempInstrs);
}

const std::vector<VGMInstr*>& VGMInstrSet::exportInstrs() const {
  return m_exportInstrs.empty() ? aInstrs : m_exportInstrs;
}

void VGMInstrSet::addTempInstr(VGMInstr* instr) {
  assert(instr != nullptr);
  m_tempInstrs.push_back(instr);
  m_exportInstrs.push_back(instr);
}

// ********
// VGMInstr
// ********

VGMInstr::VGMInstr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
                   uint32_t theInstrNum, std::string name, float reverb)
    : VGMItem(instrSet, offset, length, std::move(name), Type::Instrument),
      bank(theBank), instrNum(theInstrNum), parInstrSet(instrSet), reverb(reverb) {
}

void VGMInstr::setBank(uint32_t bankNum) {
  bank = bankNum;
}

void VGMInstr::setInstrNum(uint32_t theInstrNum) {
  instrNum = theInstrNum;
}

VGMRgn *VGMInstr::addRgn(VGMRgn *rgn) {
  m_regions.emplace_back(rgn);
  if (m_auto_add_regions_as_children)
    addChild(rgn);
  return rgn;
}

VGMRgn *VGMInstr::addRgn(uint32_t offset, uint32_t length, int sampNum, uint8_t keyLow,
                         uint8_t keyHigh, uint8_t velLow, uint8_t velHigh) {
  VGMRgn *newRgn = new VGMRgn(this, offset, length, keyLow, keyHigh, velLow, velHigh, sampNum);
  m_regions.emplace_back(newRgn);
  if (m_auto_add_regions_as_children)
    addChild(newRgn);
  return newRgn;
}

void VGMInstr::deleteRegions() {
  deleteVect(m_regions);
}

// Modulator methods

void VGMInstr::addModulator(ModSource source, ModDest destination, ModAmount amount) {
  if (!amount.valid()) {
    return;
  }

  m_modulators.push_back({source, destination, amount.value()});
}

bool VGMInstr::updateModulatorAmount(ModSource source, ModDest destination, ModAmount amount) {
  if (!amount.valid()) {
    return false;
  }

  for (auto& modulator : m_modulators) {
    if (modulator.source == source && modulator.destination == destination) {
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
  addModulator(ModSource::ModWheel, ModDest::VibLfoToPitch, ModAmount::fromCents(maxDepthCents));
  // nullify default channel pressure to vib lfo pitch modulator
  addModulator(ModSource::ChannelPressure, ModDest::VibLfoToPitch, ModAmount::fromCents(0));
  addGenerator(ModDest::VibLfoFreq, ModAmount::fromHertz(minHertz));
  addModulator(ModSource::ChannelPressure, ModDest::VibLfoFreq, ModAmount::fromHertzRange(minHertz, maxHertz));
  if (delayRange.has_value()) {
    const double minDelaySeconds = clampSecondsRangeMinimum(delayRange->minSeconds);
    addGenerator(ModDest::VibLfoDelay, ModAmount::fromSeconds(minDelaySeconds));
    addModulator(ModSource::ChorusSend,
                 ModDest::VibLfoDelay,
                 ModAmount::fromSecondsRange(minDelaySeconds, delayRange->maxSeconds));
  }
}

void VGMInstr::updateStandardVibratoHandling(double maxDepthCents,
                                             double minHertz,
                                             double maxHertz) {
  updateModulatorAmount(ModSource::ModWheel,
                        ModDest::VibLfoToPitch,
                        ModAmount::fromCents(maxDepthCents));
  updateModulatorAmount(ModSource::ChannelPressure,
                        ModDest::VibLfoFreq,
                        ModAmount::fromHertzRange(minHertz, maxHertz));
}

void VGMInstr::addStandardTremoloHandling(double maxDepthDb,
                                         double minHertz,
                                         double maxHertz,
                                         TremoloGainMode gainMode) {
  addGenerator(ModDest::ModLfoFreq, ModAmount::fromHertz(minHertz));
  addModulator(ModSource::ChannelPressure, ModDest::ModLfoFreq, ModAmount::fromHertzRange(minHertz, maxHertz));
  addModulator(ModSource::ChorusSend, ModDest::ModLfoToVol, ModAmount::fromDecibels(maxDepthDb));
  if (gainMode == TremoloGainMode::NoBoost) {
    addModulator(ModSource::ChorusSend, ModDest::InitialAtten, ModAmount::fromDecibels(maxDepthDb));
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
