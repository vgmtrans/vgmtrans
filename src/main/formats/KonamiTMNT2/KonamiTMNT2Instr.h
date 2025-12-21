#pragma once
#include "VGMSampColl.h"
#include "VGMInstrSet.h"

enum KonamiTMNT2FormatVer : uint8_t;
class RawFile;
struct konami_tmnt2_instr_info {
  u8 flags;
  u8 length_lo;
  u8 length_hi;
  u8 start_lo;
  u8 start_mid;
  u8 start_hi;
  u8 volume;
  u8 note_dur;
  u8 release_dur_and_rate;
  u8 default_pan;

  constexpr u32 start() const noexcept {
    return (start_hi << 16) + (start_mid << 8) + start_lo;
  }

  constexpr u32 length() const noexcept {
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
  u8 pitch_lo;
  u8 pitch_hi;
  u8 unknown0;
  u8 flags;
  u8 length_lo;
  u8 length_hi;
  u8 start_lo;
  u8 start_mid;
  u8 start_hi;
  u8 volume;
  u8 note_dur_lo;
  u8 note_dur_hi;
  u8 release_dur_and_rate;
  u8 default_pan;

  constexpr u32 start() const noexcept {
    return (start_hi << 16) + (start_mid << 8) + start_lo;
  }

  constexpr u32 length() const noexcept {
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
                       u32 offset,
                       u32 instrTableAddr,
                       u32 drumTableAddr,
                       const std::vector<konami_tmnt2_instr_info>& instrInfos,
                       const std::vector<std::vector<konami_tmnt2_drum_info>>& drumTables,
                       std::string name,
                       KonamiTMNT2FormatVer fmtVer);
  ~KonamiTMNT2SampleInstrSet() override = default;

  void addInstrInfoChildren(VGMItem* instrInfoItem, u32 off);
  bool parseInstrPointers() override;

  bool parseMelodicInstrs();
  bool parseDrums();

  const std::vector<konami_tmnt2_instr_info> instrInfos() { return m_instrInfos; }
  const std::vector<std::vector<konami_tmnt2_drum_info>> drumTables() { return m_drumTables; }

private:
  u32 m_instrTableAddr;
  u32 m_drumTableAddr;
  const std::vector<konami_tmnt2_instr_info> m_instrInfos;
  const std::vector<std::vector<konami_tmnt2_drum_info>> m_drumTables;

  KonamiTMNT2FormatVer m_fmtVer;
};

class KonamiTMNT2SampColl
    : public VGMSampColl {
public:
  struct sample_info {
    u32 length;
    u32 offset;
    u8 volume;
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
    u32 offset,
    u32 length = 0,
    std::string name = std::string("Konami TMNT2 Sample Collection")
  );
  bool parseHeader() override;
  bool parseSampleInfo() override;

private:
  std::vector<VGMItem*> samplePointers;
  const std::vector<sample_info> m_sampInfos;
};
