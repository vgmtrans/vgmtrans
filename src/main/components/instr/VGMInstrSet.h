#pragma once
#include "common.h"
#include "Modulation.h"
#include "VGMFile.h"
#include <optional>

class VGMSampColl;
class VGMInstr;
class VGMRgn;
class VGMSamp;
class VGMRgnItem;
class VGMColl;

constexpr float defaultReverbPercent = 0.25;

// ***********
// VGMInstrSet
// ***********

class VGMInstrSet : public VGMFile {
public:
  VGMInstrSet(const std::string &format, RawFile *file, uint32_t offset, uint32_t length = 0,
              std::string name = "VGMInstrSet", VGMSampColl *theSampColl = nullptr);
  ~VGMInstrSet() override;

  bool loadVGMFile(bool useMatcher = true) override;
  bool load() override;
  virtual bool parseHeader();
  virtual bool parseInstrPointers();
  virtual bool loadInstrs();
  virtual void useColl(const VGMColl* coll) {}
  virtual void unuseColl() {}
  virtual bool isViableSampCollMatch(VGMSampColl*) { return true; }

  void prepareForExport(const VGMColl* coll);
  void cleanupAfterExport();

  const std::vector<VGMInstr*>& exportInstrs() const;

  VGMInstr *addInstr(uint32_t offset, uint32_t length, uint32_t bank, uint32_t instrNum,
                     const std::string &instrName = "");

  std::vector<VGMInstr *> aInstrs;
  VGMSampColl *sampColl;

protected:
   void addTempInstr(VGMInstr* instr);
   void disableAutoAddInstrumentsAsChildren() { m_auto_add_instruments_as_children = false; }

private:
   bool m_auto_add_instruments_as_children{true};
   std::vector<VGMInstr*> m_exportInstrs;
   std::vector<VGMInstr*> m_tempInstrs;
};

// ********
// VGMInstr
// ********

class VGMInstr : public VGMItem {
public:
  VGMInstr(VGMInstrSet *parInstrSet, uint32_t offset, uint32_t length, uint32_t bank,
           uint32_t instrNum, std::string name = "Instrument",
           float reverb = defaultReverbPercent);

  const std::vector<VGMRgn*>& regions() { return m_regions; }

  inline void setBank(uint32_t bankNum);
  inline void setInstrNum(uint32_t theInstrNum);

  VGMRgn *addRgn(VGMRgn *rgn);
  VGMRgn *addRgn(uint32_t offset, uint32_t length, int sampNum, uint8_t keyLow = 0,
                 uint8_t keyHigh = 0x7F, uint8_t velLow = 0, uint8_t velHigh = 0x7F);

  // Modulator support
  void addModulator(ModSource source, ModDest destination, ModAmount amount);
  void addModulator(ModDest destination, ModAmount amount);
  bool updateModulatorAmount(ModSource source, ModDest destination, ModAmount amount);
  bool updateModulatorAmount(ModDest destination, ModAmount amount);
  void addStandardVibratoHandling(double maxDepthCents,
                                  double minHertz,
                                  double maxHertz,
                                  std::optional<DelayRange> delayRange = std::nullopt);
  void addStandardVibratoHandling(const VibratoModulationSpec& spec);
  void updateStandardVibratoHandling(double maxDepthCents,
                                     double minHertz,
                                     double maxHertz);
  void updateStandardVibratoHandling(const VibratoModulationSpec& spec);
  void addStandardTremoloHandling(double maxDepthDb,
                                  double minHertz,
                                  double maxHertz,
                                  TremoloGainMode gainMode);
  void addStandardTremoloHandling(const TremoloModulationSpec& spec);
  [[nodiscard]] const std::vector<SynthModulator>& modulators() const { return m_modulators; }

  // Generator support
  void addGenerator(ModDest destination, ModAmount amount);
  // Helpers for adding specific-generators
  void addGlobalVibratoFrequency(double hertz);
  void addGlobalTremoloFrequency(double hertz);
  [[nodiscard]] const std::vector<SynthGenerator>& generators() const { return m_generators; }

  virtual bool loadInstr() { return true; }

  uint32_t bank;
  uint32_t instrNum;
  VGMInstrSet *parInstrSet;
  float reverb;


protected:
  void disableAutoAddRegionsAsChildren() { m_auto_add_regions_as_children = false; }
  void deleteRegions();

private:
  bool m_auto_add_regions_as_children{true};
  std::vector<VGMRgn*> m_regions;
  std::vector<SynthModulator> m_modulators;
  std::vector<SynthGenerator> m_generators;
};
