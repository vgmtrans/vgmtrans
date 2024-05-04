#include <spdlog/fmt/fmt.h>
#include "CPS1Instr.h"
#include "CPS2Format.h"
#include "VGMRgn.h"
#include "OkiAdpcm.h"

// ******************
// CPS1SampleInstrSet
// ******************

CPS1SampleInstrSet::CPS1SampleInstrSet(RawFile *file,
                                       CPSFormatVer version,
                                       uint32_t offset,
                                       std::string &name)
    : VGMInstrSet(CPS1Format::name, file, offset, 0, name),
      fmt_version(version) {
}

CPS1SampleInstrSet::~CPS1SampleInstrSet(void) {
}

bool CPS1SampleInstrSet::GetInstrPointers() {
  for (int i = 0; i < 128; ++i) {
    auto offset = dwOffset + (i * 4);
    if (!(GetByte(offset) & 0x80)) {
      break;
    }
    std::string name = fmt::format("Instrument {}", i);
    VGMInstr* instr = new VGMInstr(this, offset, 4, 0, i, name);
    VGMRgn* rgn = new VGMRgn(instr, offset);
    instr->unLength = 4;
    rgn->unLength = 4;
    // subtract 1 to account for the first OKIM6295 sample ptr always being null
    rgn->sampNum = GetByte(offset+1) - 1;
    instr->aRgns.push_back(rgn);
    aInstrs.push_back(instr);
  }
  return true;
}

// **************
// CPS1SampColl
// **************

CPS1SampColl::CPS1SampColl(RawFile *file,
                           CPS1SampleInstrSet *theinstrset,
                           uint32_t offset,
                           uint32_t length,
                           std::string name)
    : VGMSampColl(CPS1Format::name, file, offset, length, name),
      instrset(theinstrset) {
}


bool CPS1SampColl::GetHeaderInfo() {
  auto header = AddHeader(8, 0x400-8, "Sample Pointers");

  int i = 1;
  for (int offset = 8; offset < 0x400; offset += 8) {
    if (GetWord(offset) == 0xFFFFFFFF) {
      break;
    }
    auto startStr = fmt::format("Sample {} Start", i);
    auto endStr = fmt::format("Sample {} End", i);
    i += 1;

    header->AddSimpleItem(offset, 3, startStr);
    header->AddSimpleItem(offset+3, 3, endStr);
  }
  return true;
}

bool CPS1SampColl::GetSampleInfo() {
  constexpr int PTR_SIZE = 3;

  int i = 1;
  for (int offset = 8; offset < 0x400; offset += 8) {
    auto sampAddr = GetShort(offset);
    if (sampAddr == 0xFFFF) {
      break;
    }
    auto begin = GetWordBE(offset) >> 8;
    auto end = GetWordBE(offset+PTR_SIZE) >> 8;

    auto name = fmt::format("Sample {}", i);
    i += 1;
    auto sample = new DialogicAdpcmSamp(this, begin, end-begin, CPS1_OKIMSM6295_SAMPLE_RATE, name);
    sample->SetWaveType(WT_PCM16);
    sample->SetLoopStatus(false);
    sample->unityKey = 0x3C;
    samples.push_back(sample);
  }
  return true;
}