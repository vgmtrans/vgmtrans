#pragma once
#include "base/Types.h"
#include "Modulation.h"
#include "VGMFile.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

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
  VGMInstrSet(const std::string &format, RawFile *file, u32 offset, u32 length = 0,
              std::string name = "VGMInstrSet", VGMSampColl *sampColl = nullptr);
  ~VGMInstrSet() override;

  bool load() override;
  virtual bool parseHeader();
  virtual bool parseInstrPointers();
  virtual bool loadInstrs();
  virtual void useColl(const VGMColl* coll) {}
  virtual void unuseColl() {}
  virtual bool isViableSampCollMatch(VGMSampColl*) { return true; }

  void prepareForExport(const VGMColl* coll);
  void cleanupAfterExport();

  [[nodiscard]] std::span<VGMInstr* const> instrs() const { return m_instrs; }
  [[nodiscard]] bool hasInstrs() const { return !m_instrs.empty(); }
  [[nodiscard]] size_t instrCount() const { return m_instrs.size(); }
  VGMInstr* instr(size_t index) const { return m_instrs.at(index); }
  std::span<VGMInstr* const> exportInstrs() const;

  VGMInstr *addInstr(u32 offset, u32 length, u32 bank, u32 instrNum,
                     const std::string &instrName = "");
  [[nodiscard]] VGMSampColl* sampColl() const { return m_sampColl; }
  void attachSampColl(VGMSampColl* newSampColl);
  void sinkSampColl(std::unique_ptr<VGMSampColl>&& newSampColl);
  template <class SampCollType, class... Args>
  SampCollType* addSampColl(Args&&... args) {
    auto newSampColl = std::make_unique<SampCollType>(std::forward<Args>(args)...);
    auto* rawSampColl = newSampColl.get();
    sinkSampColl(std::move(newSampColl));
    return rawSampColl;
  }
  void clearSampColl();

protected:
   void reserveInstrs(size_t count) { m_ownedInstrs.reserve(count); m_instrs.reserve(count); }
   VGMInstr* sinkInstr(std::unique_ptr<VGMInstr>&& instr);
   VGMInstr* sinkInstrAsChild(std::unique_ptr<VGMInstr>&& instr);
   VGMInstr* sinkInstrAsChild(VGMItem& parent, std::unique_ptr<VGMInstr>&& instr);
   template <class InstrType, class... Args>
   InstrType* addInstr(Args&&... args) {
     auto instr = std::make_unique<InstrType>(std::forward<Args>(args)...);
     auto* rawInstr = instr.get();
     sinkInstr(std::move(instr));
     return rawInstr;
   }
   std::vector<std::unique_ptr<VGMInstr>> releaseInstrs();
   void clearInstrs();
   void sinkTempInstr(std::unique_ptr<VGMInstr>&& instr);
   void disableAutoAddInstrumentsAsChildren() { m_auto_add_instruments_as_children = false; }

private:
   bool m_auto_add_instruments_as_children{true};
   std::vector<std::unique_ptr<VGMInstr>> m_ownedInstrs;
   std::vector<VGMInstr*> m_instrs;
   std::vector<VGMInstr*> m_exportInstrs;
   std::vector<std::unique_ptr<VGMInstr>> m_tempInstrs;
   VGMSampColl* m_sampColl{};
   std::unique_ptr<VGMSampColl> m_ownedSampColl;
};

// ********
// VGMInstr
// ********

class VGMInstr : public VGMItem {
public:
  VGMInstr(VGMInstrSet *parInstrSet, u32 offset, u32 length, u32 bank,
           u32 instrNum, std::string name = "Instrument",
           float reverb = defaultReverbPercent);
  VGMInstr(const VGMInstr& other) = delete;
  VGMInstr& operator=(const VGMInstr& other) = delete;
  ~VGMInstr() override;

  std::span<VGMRgn* const> regions() const { return m_regions; }

  inline void setBank(u32 bankNum);
  inline void setInstrNum(u32 theInstrNum);

  VGMRgn *sinkRgn(std::unique_ptr<VGMRgn>&& rgn);
  template <class RgnType, class... Args>
  RgnType* addRgn(Args&&... args) {
    auto rgn = std::make_unique<RgnType>(std::forward<Args>(args)...);
    auto* rawRgn = rgn.get();
    sinkRgn(std::move(rgn));
    return rawRgn;
  }
  VGMRgn *addRgn(u32 offset, u32 length, int sampNum, u8 keyLow = 0,
                 u8 keyHigh = 0x7F, u8 velLow = 0, u8 velHigh = 0x7F);

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

  u32 bank;
  u32 instrNum;
  VGMInstrSet *parInstrSet;
  float reverb;


protected:
  void disableAutoAddRegionsAsChildren() { m_auto_add_regions_as_children = false; }
  void deleteRegions();

private:
  bool m_auto_add_regions_as_children{true};
  std::vector<VGMRgn*> m_regions;
  std::vector<std::unique_ptr<VGMRgn>> m_ownedRegions;
  std::vector<SynthModulator> m_modulators;
  std::vector<SynthGenerator> m_generators;
};
