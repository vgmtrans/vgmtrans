#include <spdlog/fmt/fmt.h>
#include "CPS1Instr.h"
#include "CPS2Format.h"
#include "VGMRgn.h"
#include "OkiAdpcm.h"
#include "version.h"
#include "Root.h"

// ******************
// CPS1SampleInstrSet
// ******************

CPS1SampleInstrSet::CPS1SampleInstrSet(RawFile *file,
                                       CPS1FormatVer version,
                                       uint32_t offset,
                                       std::string name)
    : VGMInstrSet(CPS1Format::name, file, offset, 0, std::move(name)),
      fmt_version(version) {
}

bool CPS1SampleInstrSet::parseInstrPointers() {
  switch (fmt_version) {
    case CPS1_V100:
    case CPS1_V200:
    case CPS1_V350:
    case CPS1_V500:
    case CPS1_V502:
      for (int i = 1; i < 128; ++i) {
        std::string name = fmt::format("Instrument {:03d}", i);
        VGMInstr* instr = new VGMInstr(this, 0, 0, 0, i, name, 0);
        VGMRgn* rgn = new VGMRgn(instr, 0);
        rgn->sampNum = i - 1;
        rgn->release_time = 10;
        instr->addRgn(rgn);
        aInstrs.push_back(instr);
      }
      break;

    case CPS1_V425:
      for (int i = 0; i < 128; ++i) {
        auto offset = dwOffset + (i * 4);
        if (!(readByte(offset) & 0x80)) {
          break;
        }
        std::string name = fmt::format("Instrument {}", i);
        VGMInstr* instr = new VGMInstr(this, offset, 4, 0, i, name, 0);
        VGMRgn* rgn = new VGMRgn(instr, offset);
        instr->unLength = 4;
        rgn->unLength = 4;
        // subtract 1 to account for the first OKIM6295 sample ptr always being null
        rgn->sampNum = readByte(offset+1) - 1;
        rgn->release_time = 10;
        instr->addRgn(rgn);
        aInstrs.push_back(instr);
      }
      break;
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
    : VGMSampColl(CPS1Format::name, file, offset, length, std::move(name)),
      instrset(theinstrset) {
}


bool CPS1SampColl::parseHeader() {
  auto header = addHeader(8, 0x400-8, "Sample Pointers");

  int i = 1;
  for (int offset = 8; offset < 0x400; offset += 8) {
    if (readWord(offset) == 0xFFFFFFFF) {
      break;
    }
    auto startStr = fmt::format("Sample {} Start", i);
    auto endStr = fmt::format("Sample {} End", i);
    i += 1;

    header->addChild(offset, 3, startStr);
    header->addChild(offset+3, 3, endStr);
  }
  return true;
}

bool CPS1SampColl::parseSampleInfo() {
  constexpr int PTR_SIZE = 3;

  int i = 1;
  for (int offset = 8; offset < 0x400; offset += 8) {
    auto sampAddr = readShort(offset);

    VGMSamp* sample;
    if (sampAddr == 0xFFFF || sampAddr == 0) {
      auto name = fmt::format("Empty Sample {:03d}", i);
      sample = new EmptySamp(this);
    } else {
      auto name = fmt::format("Sample {:03d}", i);
      auto begin = readWordBE(offset) >> 8;
      auto end = readWordBE(offset+PTR_SIZE) >> 8;
      sample = new DialogicAdpcmSamp(this, begin, end > begin ? end-begin : 0, CPS1_OKIMSM6295_SAMPLE_RATE, CPS1_OKI_GAIN, name);
    }
    i += 1;
    sample->setWaveType(WT_PCM16);
    sample->setLoopStatus(false);
    sample->unityKey = 0x3C;
    samples.push_back(sample);
  }
  return true;
}

// ******************
// CPS1SampleInstrSet
// ******************

CPS1OPMInstrSet::CPS1OPMInstrSet(RawFile *file,
                               CPS1FormatVer version,
                               u8 masterVol,
                               u32 offset,
                               u32 length,
                               const std::string& name)
    : VGMInstrSet(CPS1Format::name, file, offset, length, name),
      fmt_version(version), masterVol(masterVol) {
}

bool CPS1OPMInstrSet::parseInstrPointers() {
  int numInstrs;
  size_t instrSize;
  switch (fmt_version) {
    case CPS1_V200:
      instrSize = sizeof(CPS1OPMInstrDataV2_00);
      break;
    case CPS1_V500:
    case CPS1_V502:
      instrSize = sizeof(CPS1OPMInstrDataV5_02);
      break;
    case CPS1_V100:
    case CPS1_V350:
    case CPS1_V425:
      instrSize = sizeof(CPS1OPMInstrDataV4_25);
      break;
  }
  numInstrs = std::min(unLength / static_cast<u32>(instrSize), 128U);

  for (int i = 0; i < numInstrs; ++i) {
    auto offset = dwOffset + (i * instrSize);
    if (readWord(offset) == 0 && readWord(offset+4) == 0) {
      break;
    }

    auto name = fmt::format("Instrument {}", i);

    switch (fmt_version) {
      case CPS1_V200: {
        auto instr = new CPS1OPMInstr<CPS1OPMInstrDataV2_00>(this, masterVol, offset, instrSize, 0,
  i, name);
        aInstrs.push_back(instr);
        break;
      }
      case CPS1_V500:
      case CPS1_V502: {
        auto instr = new CPS1OPMInstr<CPS1OPMInstrDataV5_02>(this, masterVol, offset, instrSize, 0,
  i, name);
        aInstrs.push_back(instr);
        break;
      }
      case CPS1_V100:
      case CPS1_V350:
      case CPS1_V425: {
        auto instr = new CPS1OPMInstr<CPS1OPMInstrDataV4_25>(this, masterVol, offset, instrSize, 0,
        i, name);
        aInstrs.push_back(instr);
        instr->addChild(new VGMItem(this, offset, 1, "Transpose"));
        instr->addChild(new VGMItem(this, offset+1, 1, "LFO_ENABLE_AND_WF"));
        instr->addChild(new VGMItem(this, offset+2, 1, "LFRQ"));
        instr->addChild(new VGMItem(this, offset+3, 1, "PMD"));
        instr->addChild(new VGMItem(this, offset+4, 1, "AMD"));
        instr->addChild(new VGMItem(this, offset+5, 1, "FL_CON"));
        instr->addChild(new VGMItem(this, offset+6, 1, "PMS_AMS"));
        instr->addChild(new VGMItem(this, offset+7, 1, "SLOT_MASK"));
        instr->addChild(new VGMItem(this, offset+8, 12, "Driver-specific Volume Params"));
        instr->addChild(new VGMItem(this, offset+20, 4, "DT1_MUL"));
        instr->addChild(new VGMItem(this, offset+24, 4, "KS_AR"));
        instr->addChild(new VGMItem(this, offset+28, 4, "AMSEN_D1R"));
        instr->addChild(new VGMItem(this, offset+32, 4, "DT2_D2R"));
        instr->addChild(new VGMItem(this, offset+36, 4, "D1L_RR"));
        break;
      }
    }
  }
  return true;
}

std::string CPS1OPMInstrSet::generateOPMFile() {
  std::ostringstream output;
  std::string header = std::string("// Converted using VGMTrans version: ") + VGMTRANS_VERSION + "\n";
  output << header;

  for (size_t i = 0; i < aInstrs.size(); ++i) {
    switch (fmt_version) {
      case CPS1_V200:
        if (auto* instr = dynamic_cast<CPS1OPMInstr<CPS1OPMInstrDataV2_00>*>(aInstrs[i]); instr != nullptr) {
          output << instr->toOPMString(i) << '\n';
        }
        break;
      case CPS1_V500:
      case CPS1_V502:
        if (auto* instr = dynamic_cast<CPS1OPMInstr<CPS1OPMInstrDataV5_02>*>(aInstrs[i]); instr != nullptr) {
          output << instr->toOPMString(i) << '\n';
        }
        break;
      case CPS1_V100:
      case CPS1_V350:
      case CPS1_V425:
        if (auto* instr = dynamic_cast<CPS1OPMInstr<CPS1OPMInstrDataV4_25>*>(aInstrs[i]); instr != nullptr) {
          output << instr->toOPMString(i) << '\n';
        }
        break;
    }
  }
  return output.str();
}

bool CPS1OPMInstrSet::saveAsOPMFile(const std::string &filepath) {
  auto content = generateOPMFile();
  pRoot->UI_writeBufferToFile(filepath, reinterpret_cast<uint8_t*>(const_cast<char*>(content.data())), static_cast<uint32_t>(content.size()));
  return true;
}
