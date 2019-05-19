/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Vab.h"
#include "PSXSPU.h"
#include "PS1Format.h"

using namespace std;

Vab::Vab(RawFile *file, uint32_t offset) : VGMInstrSet(PS1Format::name, file, offset) {}

Vab::~Vab(void) {}

bool Vab::GetHeaderInfo() {
    uint32_t nEndOffset = GetEndOffset();
    uint32_t nMaxLength = nEndOffset - dwOffset;

    if (nMaxLength < 0x20) {
        return false;
    }

    name = L"VAB";

    VGMHeader *vabHdr = AddHeader(dwOffset, 0x20, L"VAB Header");
    vabHdr->AddSimpleItem(dwOffset + 0x00, 4, L"ID");
    vabHdr->AddSimpleItem(dwOffset + 0x04, 4, L"Version");
    vabHdr->AddSimpleItem(dwOffset + 0x08, 4, L"VAB ID");
    vabHdr->AddSimpleItem(dwOffset + 0x0c, 4, L"Total Size");
    vabHdr->AddSimpleItem(dwOffset + 0x10, 2, L"Reserved");
    vabHdr->AddSimpleItem(dwOffset + 0x12, 2, L"Number of Programs");
    vabHdr->AddSimpleItem(dwOffset + 0x14, 2, L"Number of Tones");
    vabHdr->AddSimpleItem(dwOffset + 0x16, 2, L"Number of VAGs");
    vabHdr->AddSimpleItem(dwOffset + 0x18, 1, L"Master Volume");
    vabHdr->AddSimpleItem(dwOffset + 0x19, 1, L"Master Pan");
    vabHdr->AddSimpleItem(dwOffset + 0x1a, 1, L"Bank Attributes 1");
    vabHdr->AddSimpleItem(dwOffset + 0x1b, 1, L"Bank Attributes 2");
    vabHdr->AddSimpleItem(dwOffset + 0x1c, 4, L"Reserved");

    GetBytes(dwOffset, 0x20, &hdr);

    return true;
}

bool Vab::GetInstrPointers() {
    uint32_t nEndOffset = GetEndOffset();
    uint32_t nMaxLength = nEndOffset - dwOffset;

    uint32_t offProgs = dwOffset + 0x20;
    uint32_t offToneAttrs = offProgs + (16 * 128);

    uint16_t numPrograms = GetShort(dwOffset + 0x12);
    uint16_t numTones = GetShort(dwOffset + 0x14);
    uint16_t numVAGs = GetShort(dwOffset + 0x16);

    uint32_t offVAGOffsets = offToneAttrs + (32 * 16 * numPrograms);

    VGMHeader *progsHdr = AddHeader(offProgs, 16 * 128, L"Program Table");
    VGMHeader *toneAttrsHdr = AddHeader(offToneAttrs, 32 * 16, L"Tone Attributes Table");

    if (numPrograms > 128) {
        std::wstringstream message;
        message << L"Too many programs (" << numPrograms << L")  Offset: 0x" << std::hex
                << dwOffset;
        pRoot->AddLogItem(new LogItem(message.str(), LOG_LEVEL_ERR, L"VAB"));
        return false;
    }
    if (numVAGs > 255) {
        std::wstringstream message;
        message << L"Too many VAGs (" << numVAGs << L")  Offset: 0x" << std::hex << dwOffset;
        pRoot->AddLogItem(new LogItem(message.str(), LOG_LEVEL_ERR, L"VAB"));
        return false;
    }

    // Load each instruments.
    //
    // Rule 1. Valid instrument pointers are not always sequentially located from 0 to (numProgs -
    // 1). Number of tones can be 0. That's an empty instrument. We need to ignore it. See Clock
    // Tower PSF for example.
    //
    // Rule 2. Do not load programs more than number of programs. Even if a program table value is
    // provided. Otherwise an out-of-order access can be caused in Tone Attributes Table. See the
    // swimming event BGM of Aitakute... ~your smiles in my heart~ for example. (github issue #115)
    uint32_t numProgramsLoaded = 0;
    for (uint32_t progIndex = 0; progIndex < 128 && numProgramsLoaded < numPrograms; progIndex++) {
        uint32_t offCurrProg = offProgs + (progIndex * 16);
        uint32_t offCurrToneAttrs = offToneAttrs + (uint32_t)(aInstrs.size() * 32 * 16);

        if (offCurrToneAttrs + (32 * 16) > nEndOffset) {
            break;
        }

        uint8_t numTonesPerInstr = GetByte(offCurrProg);
        if (numTonesPerInstr > 32) {
            wchar_t log[512];
            swprintf(log, 512, L"Too many tones (%u) in Program #%u.", numTonesPerInstr, progIndex);
            pRoot->AddLogItem(new LogItem(log, LOG_LEVEL_WARN, L"Vab"));
        } else if (numTonesPerInstr != 0) {
            VabInstr *newInstr = new VabInstr(this, offCurrToneAttrs, 0x20 * 16, 0, progIndex);
            aInstrs.push_back(newInstr);
            GetBytes(offCurrProg, 0x10, &newInstr->attr);

            VGMHeader *hdr = progsHdr->AddHeader(offCurrProg, 0x10, L"Program");
            hdr->AddSimpleItem(offCurrProg + 0x00, 1, L"Number of Tones");
            hdr->AddSimpleItem(offCurrProg + 0x01, 1, L"Volume");
            hdr->AddSimpleItem(offCurrProg + 0x02, 1, L"Priority");
            hdr->AddSimpleItem(offCurrProg + 0x03, 1, L"Mode");
            hdr->AddSimpleItem(offCurrProg + 0x04, 1, L"Pan");
            hdr->AddSimpleItem(offCurrProg + 0x05, 1, L"Reserved");
            hdr->AddSimpleItem(offCurrProg + 0x06, 2, L"Attribute");
            hdr->AddSimpleItem(offCurrProg + 0x08, 4, L"Reserved");
            hdr->AddSimpleItem(offCurrProg + 0x0c, 4, L"Reserved");

            newInstr->masterVol = GetByte(offCurrProg + 0x01);

            toneAttrsHdr->unLength = offCurrToneAttrs + (32 * 16) - offToneAttrs;

            numProgramsLoaded++;
        }
    }

    if ((offVAGOffsets + 2 * 256) <= nEndOffset) {
        wchar_t name[256];
        std::vector<SizeOffsetPair> vagLocations;
        uint32_t totalVAGSize = 0;
        VGMHeader *vagOffsetHdr = AddHeader(offVAGOffsets, 2 * 256, L"VAG Pointer Table");

        uint32_t vagStartOffset = GetShort(offVAGOffsets) * 8;
        vagOffsetHdr->AddSimpleItem(offVAGOffsets, 2, L"VAG Size /8 #0");
        totalVAGSize = vagStartOffset;

        for (uint32_t i = 0; i < numVAGs; i++) {
            uint32_t vagOffset;
            uint32_t vagSize;

            if (i == 0) {
                vagOffset = vagStartOffset;
                vagSize = GetShort(offVAGOffsets + (i + 1) * 2) * 8;
            } else {
                vagOffset = vagStartOffset + vagLocations[i - 1].offset + vagLocations[i - 1].size;
                vagSize = GetShort(offVAGOffsets + (i + 1) * 2) * 8;
            }

            swprintf(name, 256, L"VAG Size /8 #%u", i + 1);
            vagOffsetHdr->AddSimpleItem(offVAGOffsets + (i + 1) * 2, 2, name);

            if (vagOffset + vagSize <= nEndOffset) {
                vagLocations.push_back(SizeOffsetPair(vagOffset, vagSize));
                totalVAGSize += vagSize;
            } else {
                wchar_t log[512];
                swprintf(log, 512, L"VAG #%u pointer (offset=0x%08X, size=%u) is invalid.", i + 1,
                         vagOffset, vagSize);
                pRoot->AddLogItem(new LogItem(log, LOG_LEVEL_WARN, L"Vab"));
            }
        }
        unLength = (offVAGOffsets + 2 * 256) - dwOffset;

        // single VAB file?
        uint32_t offVAGs = offVAGOffsets + 2 * 256;
        if (dwOffset == 0 && vagLocations.size() != 0) {
            // load samples as well
            PSXSampColl *newSampColl =
                new PSXSampColl(format, this, offVAGs, totalVAGSize, vagLocations);
            if (newSampColl->LoadVGMFile()) {
                pRoot->AddVGMFile(newSampColl);
                // this->sampColl = newSampColl;
            } else {
                delete newSampColl;
            }
        }
    }

    return true;
}

// ********
// VabInstr
// ********

VabInstr::VabInstr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
                   uint32_t theInstrNum, const wstring &name)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum, name), masterVol(127) {}

VabInstr::~VabInstr(void) {}

bool VabInstr::LoadInstr() {
    int8_t numRgns = attr.tones;
    for (int i = 0; i < numRgns; i++) {
        VabRgn *rgn = new VabRgn(this, dwOffset + i * 0x20);
        if (!rgn->LoadRgn()) {
            delete rgn;
            return false;
        }
        aRgns.push_back(rgn);
    }
    return true;
}

// ******
// VabRgn
// ******

VabRgn::VabRgn(VabInstr *instr, uint32_t offset) : VGMRgn(instr, offset) {}

bool VabRgn::LoadRgn() {
    VabInstr *instr = (VabInstr *)parInstr;
    unLength = 0x20;
    GetBytes(dwOffset, 0x20, &attr);

    AddGeneralItem(dwOffset, 1, L"Priority");
    AddGeneralItem(dwOffset + 1, 1, L"Mode (use reverb?)");
    AddVolume((GetByte(dwOffset + 2) * instr->masterVol) / (127.0 * 127.0), dwOffset + 2, 1);
    AddPan(GetByte(dwOffset + 3), dwOffset + 3);
    AddUnityKey(GetByte(dwOffset + 4), dwOffset + 4);
    AddGeneralItem(dwOffset + 5, 1, L"Pitch Tune");
    AddKeyLow(GetByte(dwOffset + 6), dwOffset + 6);
    AddKeyHigh(GetByte(dwOffset + 7), dwOffset + 7);
    AddGeneralItem(dwOffset + 8, 1, L"Vibrato Width");
    AddGeneralItem(dwOffset + 9, 1, L"Vibrato Time");
    AddGeneralItem(dwOffset + 10, 1, L"Portamento Width");
    AddGeneralItem(dwOffset + 11, 1, L"Portamento Holding Time");
    AddGeneralItem(dwOffset + 12, 1, L"Pitch Bend Min");
    AddGeneralItem(dwOffset + 13, 1, L"Pitch Bend Max");
    AddGeneralItem(dwOffset + 14, 1, L"Reserved");
    AddGeneralItem(dwOffset + 15, 1, L"Reserved");
    AddGeneralItem(dwOffset + 16, 2, L"ADSR1");
    AddGeneralItem(dwOffset + 18, 2, L"ADSR2");
    AddGeneralItem(dwOffset + 20, 2, L"Parent Program");
    AddSampNum(GetShort(dwOffset + 22) - 1, dwOffset + 22, 2);
    AddGeneralItem(dwOffset + 24, 2, L"Reserved");
    AddGeneralItem(dwOffset + 26, 2, L"Reserved");
    AddGeneralItem(dwOffset + 28, 2, L"Reserved");
    AddGeneralItem(dwOffset + 30, 2, L"Reserved");
    ADSR1 = attr.adsr1;
    ADSR2 = attr.adsr2;
    if ((int)sampNum < 0)
        sampNum = 0;

    if (keyLow > keyHigh) {
        std::wstringstream message;
        message << L"Low Key (" << keyLow << L") is higher than High Key (" << keyHigh
                << L")  Offset: 0x" << std::hex << dwOffset;
        pRoot->AddLogItem(new LogItem(message.str(), LOG_LEVEL_ERR, L"VAB (VabRgn)"));
        return false;
    }

    // gocha: AFAIK, the valid range of pitch is 0-127. It must not be negative.
    // If it exceeds 127, driver clips the value and it will become 127. (In Hokuto no Ken, at
    // least) I am not sure if the interpretation of this value depends on a driver or VAB version.
    // The following code takes the byte as signed, since it could be a typical extended
    // implementation.
    int8_t ft = (int8_t)GetByte(dwOffset + 5);
    double cents = ft * 100.0 / 128.0;
    SetFineTune((int16_t)cents);

    PSXConvADSR<VabRgn>(this, ADSR1, ADSR2, false);
    return true;
}
