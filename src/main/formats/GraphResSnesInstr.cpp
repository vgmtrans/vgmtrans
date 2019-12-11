/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "GraphResSnesInstr.h"
#include "SNESDSP.h"

// ********************
// GraphResSnesInstrSet
// ********************

GraphResSnesInstrSet::GraphResSnesInstrSet(RawFile *file, GraphResSnesVersion ver,
                                           uint32_t spcDirAddr,
                                           const std::map<uint8_t, uint16_t> &instrADSRHints,
                                           const std::string &name)
    : VGMInstrSet(GraphResSnesFormat::name, file, spcDirAddr, 0, name),
      version(ver),
      spcDirAddr(spcDirAddr),
      instrADSRHints(instrADSRHints) {}

GraphResSnesInstrSet::~GraphResSnesInstrSet() {}

bool GraphResSnesInstrSet::GetHeaderInfo() {
    return true;
}

bool GraphResSnesInstrSet::GetInstrPointers() {
    usedSRCNs.clear();

    uint16_t addrSampEntryMax = 0xffff;
    for (uint8_t srcn = 0; srcn <= 0x7f; srcn++) {
        uint32_t addrDIRentry = spcDirAddr + (srcn * 4);
        if (!SNESSampColl::IsValidSampleDir(rawfile, addrDIRentry, true)) {
            continue;
        }

        if (addrDIRentry >= addrSampEntryMax) {
            break;
        }

        uint16_t addrSampStart = GetShort(addrDIRentry);
        if (addrSampStart < addrDIRentry + 4) {
            break;
        }

        if (addrSampStart == 0) {
            break;
        }

        if (addrSampStart < addrSampEntryMax) {
            addrSampEntryMax = addrSampStart;
        }

        usedSRCNs.push_back(srcn);

        uint16_t adsr = 0x8fe0;
        if (instrADSRHints.count(srcn)) {
            adsr = instrADSRHints[srcn];
        }

        GraphResSnesInstr *newInstr = new GraphResSnesInstr(this, version, srcn, spcDirAddr, adsr,
                                                            fmt::format("Instrument: {:#x}", srcn));
        aInstrs.push_back(newInstr);
    }

    if (aInstrs.size() == 0) {
        return false;
    }

    std::sort(usedSRCNs.begin(), usedSRCNs.end());
    SNESSampColl *newSampColl =
        new SNESSampColl(GraphResSnesFormat::name, this->rawfile, spcDirAddr, usedSRCNs);
    if (!newSampColl->LoadVGMFile()) {
        delete newSampColl;
        return false;
    }

    return true;
}

// *****************
// GraphResSnesInstr
// *****************

GraphResSnesInstr::GraphResSnesInstr(VGMInstrSet *instrSet, GraphResSnesVersion ver, uint8_t srcn,
                                     uint32_t spcDirAddr, uint16_t adsr, const std::string &name)
    : VGMInstr(instrSet, spcDirAddr + srcn * 4, 4, 0, srcn, name),
      version(ver),
      spcDirAddr(spcDirAddr),
      adsr(adsr) {}

GraphResSnesInstr::~GraphResSnesInstr() {}

bool GraphResSnesInstr::LoadInstr() {
    uint32_t offDirEnt = spcDirAddr + (instrNum * 4);
    if (offDirEnt + 4 > 0x10000) {
        return false;
    }

    uint16_t addrSampStart = GetShort(offDirEnt);

    GraphResSnesRgn *rgn = new GraphResSnesRgn(this, version, instrNum, spcDirAddr, adsr);
    rgn->sampOffset = addrSampStart - spcDirAddr;
    aRgns.push_back(rgn);

    SetGuessedLength();
    return true;
}

// ***************
// GraphResSnesRgn
// ***************

GraphResSnesRgn::GraphResSnesRgn(GraphResSnesInstr *instr, GraphResSnesVersion ver, uint8_t srcn,
                                 uint32_t spcDirAddr, uint16_t adsr)
    : VGMRgn(instr, spcDirAddr + srcn * 4, 4), version(ver) {
    uint8_t adsr1 = adsr >> 8;
    uint8_t adsr2 = adsr & 0xff;

    uint32_t offDirEnt = spcDirAddr + srcn * 4;
    AddSimpleItem(offDirEnt, 2, "SA");
    AddSimpleItem(offDirEnt + 2, 2, "LSA");

    sampNum = srcn;
    unityKey = 57;  // o4a = $1000
    fineTune = 0;
    SNESConvADSR<VGMRgn>(this, adsr1, adsr2, 0);
}

GraphResSnesRgn::~GraphResSnesRgn() {}

bool GraphResSnesRgn::LoadRgn() {
    return true;
}
