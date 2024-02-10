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
    std::ostringstream ss;
    ss << "Instrument " << i;
    string name = ss.str();
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
                           string name)
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
    ostringstream startStream;
    startStream << "Sample " << i << " Start";
    auto startStr = startStream.str();
    ostringstream endStream;
    endStream << "Sample " << i++ << " End";
    auto endStr = endStream.str();

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

    ostringstream name;
    name << "Sample " << i++;
    auto sample = new DialogicAdpcmSamp(this, begin, end-begin, CPS1_OKIMSM6295_SAMPLE_RATE, name.str());
    sample->SetWaveType(WT_PCM16);
    sample->SetLoopStatus(false);
    sample->unityKey = 0x3C;
    samples.push_back(sample);
  }
  return true;
}


// ******************
// CPS1SampleInstrSet
// ******************

CPS1OPMInstrSet::CPS1OPMInstrSet(RawFile *file,
                               CPSFormatVer version,
                               uint32_t offset,
                               std::string &name)
    : VGMInstrSet(CPS1Format::name, file, offset, 0, name),
      fmt_version(version) {
}

CPS1OPMInstrSet::~CPS1OPMInstrSet(void) {
}

bool CPS1OPMInstrSet::GetInstrPointers() {
  for (int i = 0; i < 128; ++i) {
    auto offset = dwOffset + (i * sizeof(CPS1OPMInstrData));
    if (VGMFile::GetWord(offset) == 0 && VGMFile::GetWord(offset+4) == 0) {
      break;
    }
    std::ostringstream ss;
    ss << "Instrument " << i;
    string name = ss.str();

    auto instr = new CPS1OPMInstr(this, offset, sizeof(CPS1OPMInstrData), 0, i, name);
    aInstrs.push_back(instr);
//    auto offset = dwOffset + (i * 4);
//    if (!(GetByte(offset) & 0x80)) {
//      break;
//    }
//    std::ostringstream ss;
//    ss << "Instrument " << i;
//    string name = ss.str();
//    VGMInstr* instr = new VGMInstr(this, offset, 4, 0, i, name);
//    VGMRgn* rgn = new VGMRgn(instr, offset);
//    instr->unLength = 4;
//    rgn->unLength = 4;
//    // subtract 1 to account for the first OKIM6295 sample ptr always being null
//    rgn->sampNum = GetByte(offset+1) - 1;
//    instr->aRgns.push_back(rgn);
//    aInstrs.push_back(instr);
  }
  return true;
}

std::string CPS1OPMInstrSet::generateOPMFile() {
  std::ostringstream output;
  std::string header = std::string("// Converted using VGMTrans version: ") + VGMTRANS_VERSION + "\n";
  output << header;

  for (size_t i = 0; i < aInstrs.size(); ++i) {
    CPS1OPMInstr* instr = dynamic_cast<CPS1OPMInstr*>(aInstrs[i]);
    if (instr != nullptr) {
      output << instr->toOPMString(i) << "\n";
    }
  }
  return output.str();
}

bool CPS1OPMInstrSet::SaveAsOPMFile(const std::string &filepath) {
  auto content = generateOPMFile();
  pRoot->UI_WriteBufferToFile(filepath, reinterpret_cast<uint8_t*>(const_cast<char*>(content.data())), static_cast<uint32_t>(content.size()));

//  SF2File *sf2file = NULL;

//  if (!assocColls.empty()) {
//    sf2file = assocColls.front()->CreateSF2File();
//  } else {
//    std::ostringstream message;
//    message << name
//            << ": "
//               "Instrument sets that are not part of a collection cannot be "
//               "converted to SF2 at the moment.";
//    pRoot->AddLogItem(new LogItem(message.str(), LOG_LEVEL_ERR, "VGMInstrSet"));
//    return false;
//  }
//
//  if (sf2file != NULL) {
//    bool bResult = sf2file->SaveSF2File(filepath);
//    delete sf2file;
//    return bResult;
//  }
//  return false;
}

//bool SF2File::SaveSF2File(const std::string &filepath) {
//  auto buf = SaveToMem();
//  bool result = pRoot->UI_WriteBufferToFile(filepath, buf, GetSize());
//  delete[] buf;
//  return result;
//}


// ************
// CPS1OPMInstr
// ************

CPS1OPMInstr::CPS1OPMInstr(VGMInstrSet *instrSet,
                     uint32_t offset,
                     uint32_t length,
                     uint32_t theBank,
                     uint32_t theInstrNum,
                     string &name)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum, name) {
}

CPS1OPMInstr::~CPS1OPMInstr(void) {
}

bool CPS1OPMInstr::LoadInstr() {

  this->GetBytes(dwOffset, sizeof(CPS1OPMInstrData), &opmData);
  return true;
}

std::string CPS1OPMInstr::toOPMString(int num) {
  return opmData.convertToOPMData(name).toOPMString(num);
}
