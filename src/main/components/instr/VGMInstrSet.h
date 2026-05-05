#pragma once
#include "common.h"
#include "InstrumentModulation.h"
#include "VGMFile.h"

class VGMSampColl;
class VGMInstr;
class VGMRgn;
class VGMSamp;
class VGMRgnItem;
class VGMColl;

constexpr float defaultReverbPercent = 0.25;

// BipolarAroundNominal:
//   SF2 modLfoToVolume is centered around nominal gain. Tremolo can boost above the note's normal volume.
// NoBoost:
//   Adds a matching initialAttenuation offset. Loudest tremolo point is nominal gain; all other points attenuate.
enum class TremoloGainMode {
  BipolarAroundNominal,
  NoBoost,
};

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
  void addModulator(ModSource source, ModDest destination, int32_t amount);
  void addModulator(ModSource source, ModDest destination, ParamAmount amount);

  // Helpers for adding modulators using more-intuitive units
  void addPitchModulator(ModSource source, ModDest destination, double cents);
  void addFrequencyRangeModulator(ModSource source, ModDest destination, double minHertz, double maxHertz);
  void addDelayModulator(ModSource source, ModDest destination, double seconds);
  void addAttenuationModulator(ModSource source, ModDest destination, double decibels);
  void addStandardVibratoHandling(double maxDepthCents, double minHertz, double maxHertz);
  void addStandardTremoloHandling(double maxDepthDb,
                                  double minHertz,
                                  double maxHertz,
                                  TremoloGainMode gainMode);
  [[nodiscard]] const std::vector<InstrumentModulator>& modulators() const { return m_modulators; }

  // Generator support
  void addGlobalGenerator(ModDest destination, int32_t amount);
  void addGlobalGenerator(ModDest destination, ParamAmount amount);

  // Helpers for adding generators using more-intuitive units.
  void addPitchGenerator(ModDest destination, double cents);
  void addFrequencyGenerator(ModDest destination, double hertz);
  void addDelayGenerator(ModDest destination, double seconds);
  void addAttenuationGenerator(ModDest destination, double decibels);
  void addGlobalVibratoFrequency(double hertz);
  void addGlobalTremoloFrequency(double hertz);
  [[nodiscard]] const std::vector<InstrumentGenerator>& globalGenerators() const { return m_globalGenerators; }

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
  std::vector<InstrumentModulator> m_modulators;
  std::vector<InstrumentGenerator> m_globalGenerators;
};
