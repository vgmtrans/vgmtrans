#include <array>
#include <set>
#include <string>
#include <vector>
#include "VGMSeq.h"
#include "SeqTrack.h"
#include "PSDSEFormat.h"
#include "PSDSEHeader.h"
#include "RawFile.h"

class PSDSESeq : public VGMSeq {
public:
  PSDSESeq(RawFile* file, uint32_t offset, uint32_t length = 0, std::string name = "PSDSE Sequence");
  ~PSDSESeq() override = default;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  uint16_t version = 0;
  [[nodiscard]] uint16_t defaultBankId() const { return m_defaultBankId; }
  void addPatchReference(uint16_t bank, uint8_t program);
  [[nodiscard]] bool referencesPatch(uint32_t bank, uint32_t program) const;
  [[nodiscard]] bool hasPatchReferences() const { return !m_referencedPatches.empty(); }

private:
  Endianness m_endianness = Endianness::Little;
  uint16_t m_defaultBankId = 0;
  std::set<uint32_t> m_referencedPatches;
};

class PSDSETrack : public SeqTrack {
public:
  PSDSETrack(PSDSESeq* parentSeq, long offset, long length, uint8_t channel);
  void resetVars() override;
  bool readEvent() override;

protected:
  void setChannelAndGroupFromTrkNum(int trackNum) override;

private:
  struct LfoSettings {
    uint8_t mode = 0;
    uint8_t destination = 0;
    uint8_t waveform = 0;
    int32_t amplitude = 0;
    uint16_t periodMs = 0;
    uint16_t delayMs = 0;
    uint16_t fadeMs = 0;
  };

  uint16_t readEventU16LE();
  int16_t readEventS16LE();
  int16_t readEventS16BE();
  void addDseTuningEvent(uint32_t offset, uint32_t length, const std::string& name);
  void readLfoSetup(LfoSettings& lfo);
  void readLfoEnvelope(LfoSettings& lfo);
  void addLfoEvent(uint32_t offset, uint32_t length, const std::string& name, uint8_t slot, const LfoSettings& lfo);

  uint8_t m_channel;
  int8_t currentOctave;
  uint32_t lastNoteDuration;
  uint32_t lastWaitDuration;
  uint8_t noteDurationMultiplier;
  uint16_t currentBankId;
  uint8_t currentProgram;
  bool initialBankPending;
  int16_t dseTuning;
  int16_t tuningFadeOffset;
  bool tieNextNote;
  std::array<LfoSettings, 4> lfos;
  uint8_t selectedLfo;
};
