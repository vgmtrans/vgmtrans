#pragma once
#include "VGMSampColl.h"
#include "VGMInstrSet.h"

enum KonamiTMNT2FormatVer : uint8_t;
class RawFile;
struct konami_tmnt2_instr_info {
  uint8_t flags;
  uint8_t length_lo;
  uint8_t length_hi;
  uint8_t start_lo;
  uint8_t start_mid;
  uint8_t start_hi;
  uint8_t volume;
  uint8_t note_dur;
  uint8_t release_dur_and_rate;
  uint8_t default_pan;

  constexpr uint32_t start() const noexcept {
    return (start_hi << 16) + (start_mid << 8) + start_lo;
  }

  constexpr uint32_t length() const noexcept {
    return (length_hi << 8) | length_lo;
  }

  constexpr bool isAdpcm() const noexcept {
    return (flags & 0b0001'0000) > 0;
  }

  constexpr bool isReverse() const noexcept {
    return (flags & 0b0000'1000) != 0;
  }
};

struct konami_tmnt2_drum_info {
  uint8_t pitch_lo;
  uint8_t pitch_hi;
  uint8_t unknown0;
  uint8_t flags;
  uint8_t length_lo;
  uint8_t length_hi;
  uint8_t start_lo;
  uint8_t start_mid;
  uint8_t start_hi;
  uint8_t volume;
  uint8_t note_dur_lo;
  uint8_t note_dur_hi;
  uint8_t release_dur_and_rate;
  uint8_t default_pan;

  constexpr uint32_t start() const noexcept {
    return (start_hi << 16) + (start_mid << 8) + start_lo;
  }

  constexpr uint32_t length() const noexcept {
    return (length_hi << 8) | length_lo;
  }

  constexpr bool isAdpcm() const noexcept {
    return (flags & 0b0001'0000) > 0;
  }

  constexpr bool isReverse() const noexcept {
    return (flags & 0b0000'1000) != 0;
  }
};


double k053260_pitch_cents(uint16_t pitch_word);

class KonamiTMNT2SampleInstrSet
    : public VGMInstrSet {
public:

  KonamiTMNT2SampleInstrSet(RawFile *file,
                       uint32_t offset,
                       uint32_t instrTableAddr,
                       uint32_t drumTableAddr,
                       const std::vector<konami_tmnt2_instr_info>& instrInfos,
                       const std::vector<std::vector<konami_tmnt2_drum_info>>& drumTables,
                       std::string name,
                       KonamiTMNT2FormatVer fmtVer);
  ~KonamiTMNT2SampleInstrSet() override = default;

  void addInstrInfoChildren(VGMItem* instrInfoItem, uint32_t off);
  bool parseInstrPointers() override;

  bool parseMelodicInstrs();
  bool parseDrums();

  const std::vector<konami_tmnt2_instr_info> instrInfos() { return m_instrInfos; }
  const std::vector<std::vector<konami_tmnt2_drum_info>> drumTables() { return m_drumTables; }

private:
  uint32_t m_instrTableAddr;
  uint32_t m_drumTableAddr;
  const std::vector<konami_tmnt2_instr_info> m_instrInfos;
  const std::vector<std::vector<konami_tmnt2_drum_info>> m_drumTables;

  KonamiTMNT2FormatVer m_fmtVer;
};

class KonamiTMNT2SampColl
    : public VGMSampColl {
public:
  struct sample_info {
    uint32_t length;
    uint32_t offset;
    uint8_t volume;
    bool isAdpcm;
    bool isReverse;

    template <class T>
    static sample_info makeSampleInfo(const T& x) {
      return {
        x.length(),
        x.start(),
        0,
        x.isAdpcm(),
        x.isReverse()
      };
    }
  };

  KonamiTMNT2SampColl(
    RawFile* file,
    const std::vector<sample_info>& sampInfos,
    uint32_t offset,
    uint32_t length = 0,
    std::string name = std::string("Konami TMNT2 Sample Collection")
  );
  bool parseHeader() override;
  bool parseSampleInfo() override;

private:
  std::vector<VGMItem*> samplePointers;
  const std::vector<sample_info> m_sampInfos;
};
