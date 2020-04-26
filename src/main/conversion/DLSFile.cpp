/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMInstrSet.h"
#include "VGMSamp.h"
#include "Root.h"

using namespace std;

// void WriteLIST(vector<uint8_t> & buf, uint32_t listName, uint32_t listSize)
//{
//	PushTypeOnVectBE<uint32_t>(buf, 0x4C495354);	//write "LIST"
//	PushTypeOnVect<uint32_t>(buf, listSize);
//	PushTypeOnVectBE<uint32_t>(buf, listName);
//}
//
//
////Adds a null byte and ensures 16 bit alignment of a text string
// void AlignName(string &name)
//{
//	name += (char)0x00;
//	if (name.size() % 2)						//if the size of the name string is
//odd 		name += (char)0x00;						//add another null byte
//}

//  *******
//  DLSFile
//  *******

DLSFile::DLSFile(string dls_name) : RiffFile(dls_name, "DLS ") {}

DLSFile::~DLSFile(void) {
    DeleteVect(aInstrs);
    DeleteVect(aWaves);
}

DLSInstr *DLSFile::AddInstr(unsigned long bank, unsigned long instrNum) {
    aInstrs.insert(aInstrs.end(), new DLSInstr(bank, instrNum, "Unnamed Instrument"));
    return aInstrs.back();
}

DLSInstr *DLSFile::AddInstr(unsigned long bank, unsigned long instrNum, string name) {
    aInstrs.insert(aInstrs.end(), new DLSInstr(bank, instrNum, name));
    return aInstrs.back();
}

void DLSFile::DeleteInstr(unsigned long bank, unsigned long instrNum) {}

DLSWave *DLSFile::AddWave(uint16_t formatTag, uint16_t channels, int samplesPerSec,
                          int aveBytesPerSec, uint16_t blockAlign, uint16_t bitsPerSample,
                          uint32_t waveDataSize, unsigned char *waveData, string name) {
    aWaves.insert(aWaves.end(),
                  new DLSWave(formatTag, channels, samplesPerSec, aveBytesPerSec, blockAlign,
                              bitsPerSample, waveDataSize, waveData, name));
    return aWaves.back();
}

// GetSize returns total DLS size, including the "RIFF" header size
uint32_t DLSFile::GetSize(void) {
    uint32_t size = 0;
    size += 12;             // "RIFF" + size + "DLS "
    size += COLH_SIZE;      // COLH chunk (collection chunk - tells how many instruments)
    size += LIST_HDR_SIZE;  //"lins" list (list of instruments - contains all the "ins " lists)

    for (uint32_t i = 0; i < aInstrs.size(); i++)
        size += aInstrs[i]->GetSize();                   // each "ins " list
    size += 16;                                          // "ptb" + size + cbSize + cCues
    size += (uint32_t)aWaves.size() * sizeof(uint32_t);  // each wave gets a poolcue
    size += LIST_HDR_SIZE;  //"wvp" list (wave pool - contains all the "wave" lists)
    for (uint32_t i = 0; i < aWaves.size(); i++)
        size += aWaves[i]->GetSize();  // each "wave" list
    size += LIST_HDR_SIZE;             //"INFO" list
    size += 8;                         //"INAM" + size
    size += (uint32_t)name.size();     // size of name string

    return size;
}

int DLSFile::WriteDLSToBuffer(vector<uint8_t> &buf) {
    uint32_t theDWORD;

    PushTypeOnVectBE<uint32_t>(buf, 0x52494646);   //"RIFF"
    PushTypeOnVect<uint32_t>(buf, GetSize() - 8);  // size
    PushTypeOnVectBE<uint32_t>(buf, 0x444C5320);   //"DLS "

    PushTypeOnVectBE<uint32_t>(buf, 0x636F6C68);              //"colh "
    PushTypeOnVect<uint32_t>(buf, 4);                         // size
    PushTypeOnVect<uint32_t>(buf, (uint32_t)aInstrs.size());  // cInstruments - number of
                                                              // instruments
    theDWORD = 4;  // account for 4 "lins" bytes
    for (uint32_t i = 0; i < aInstrs.size(); i++)
        theDWORD += aInstrs[i]->GetSize();  // each "ins " list
    WriteLIST(buf, 0x6C696E73, theDWORD);   // Write the "lins" LIST
    for (uint32_t i = 0; i < aInstrs.size(); i++)
        aInstrs[i]->Write(buf);  // Write each "ins " list

    PushTypeOnVectBE<uint32_t>(buf, 0x7074626C);  //"ptb"
    theDWORD = 8;
    theDWORD += (uint32_t)aWaves.size() * sizeof(uint32_t);  // each wave gets a poolcue
    PushTypeOnVect<uint32_t>(buf, theDWORD);                 // size
    PushTypeOnVect<uint32_t>(buf, 8);                        // cbSize
    PushTypeOnVect<uint32_t>(buf, (uint32_t)aWaves.size());  // cCues
    theDWORD = 0;
    for (uint32_t i = 0; i < (uint32_t)aWaves.size(); i++) {
        PushTypeOnVect<uint32_t>(buf, theDWORD);  // write the poolcue for each sample
        // hFile->Write(&theDWORD, sizeof(uint32_t));			//write the poolcue for each
        // sample
        theDWORD += aWaves[i]->GetSize();  // increment the offset to the next wave
    }

    theDWORD = 4;
    for (uint32_t i = 0; i < aWaves.size(); i++)
        theDWORD += aWaves[i]->GetSize();  // each "wave" list
    WriteLIST(buf, 0x7776706C, theDWORD);  // Write the "wvp" LIST
    for (uint32_t i = 0; i < aWaves.size(); i++)
        aWaves[i]->Write(buf);  // Write each "wave" list

    theDWORD = 12 + (uint32_t)name.size();        //"INFO" + "INAM" + size + the string size
    WriteLIST(buf, 0x494E464F, theDWORD);         // write the "INFO" list
    PushTypeOnVectBE<uint32_t>(buf, 0x494E414D);  //"INAM"
    PushTypeOnVect<uint32_t>(buf, (uint32_t)name.size());  // size
    PushBackStringOnVector(buf, name);                     // The Instrument Name string

    return true;
}

// I should probably make this function part of a parent class for both Midi and DLS file
bool DLSFile::SaveDLSFile(const std::string &filepath) {
    vector<uint8_t> dlsBuf;
    WriteDLSToBuffer(dlsBuf);
    return pRoot->UI_WriteBufferToFile(filepath, &dlsBuf[0], (uint32_t)dlsBuf.size());
}

//  *******
//  DLSInstr
//  ********

DLSInstr::DLSInstr(uint32_t bank, uint32_t instrument)
    : ulBank(bank), ulInstrument(instrument), name("Unnamed Instrument") {
    RiffFile::AlignName(name);
}

DLSInstr::DLSInstr(uint32_t bank, uint32_t instrument, string instrName)
    : ulBank(bank), ulInstrument(instrument), name(instrName) {
    RiffFile::AlignName(name);
}

DLSInstr::DLSInstr(uint32_t bank, uint32_t instrument, string instrName, vector<DLSRgn *> listRgns)
    : ulBank(bank), ulInstrument(instrument), name(instrName) {
    RiffFile::AlignName(name);
    aRgns = listRgns;
}

DLSInstr::~DLSInstr() {
    DeleteVect(aRgns);
}

uint32_t DLSInstr::GetSize(void) {
    uint32_t size = 0;
    size += LIST_HDR_SIZE;  //"ins " list
    size += INSH_SIZE;      // insh chunk
    size += LIST_HDR_SIZE;  //"lrgn" list
    for (uint32_t i = 0; i < aRgns.size(); i++)
        size += aRgns[i]->GetSize();  // each "rgn2" list
    size += LIST_HDR_SIZE;            //"INFO" list
    size += 8;                        //"INAM" + size
    size += (uint32_t)name.size();    // size of name string

    return size;
}

void DLSInstr::Write(vector<uint8_t> &buf) {
    uint32_t theDWORD;

    theDWORD = GetSize() - 8;
    RiffFile::WriteLIST(buf, 0x696E7320, theDWORD);         // write "ins " list
    PushTypeOnVectBE<uint32_t>(buf, 0x696E7368);            //"insh"
    PushTypeOnVect<uint32_t>(buf, INSH_SIZE - 8);           // size
    PushTypeOnVect<uint32_t>(buf, (uint32_t)aRgns.size());  // cRegions
    PushTypeOnVect<uint32_t>(buf, ulBank);                  // ulBank
    PushTypeOnVect<uint32_t>(buf, ulInstrument);            // ulInstrument

    theDWORD = 4;
    for (uint32_t i = 0; i < aRgns.size(); i++)
        theDWORD += aRgns[i]->GetSize();             // get the size of each "rgn2" list
    RiffFile::WriteLIST(buf, 0x6C72676E, theDWORD);  // write the "lrgn" list
    for (uint32_t i = 0; i < aRgns.size(); i++)
        aRgns[i]->Write(buf);  // write each "rgn2" list

    theDWORD = 12 + (uint32_t)name.size();           //"INFO" + "INAM" + size + the string size
    RiffFile::WriteLIST(buf, 0x494E464F, theDWORD);  // write the "INFO" list
    PushTypeOnVectBE<uint32_t>(buf, 0x494E414D);     //"INAM"
    theDWORD = (uint32_t)name.size();
    PushTypeOnVect<uint32_t>(buf, theDWORD);  // size
    PushBackStringOnVector(buf, name);        // The Instrument Name string
}

DLSRgn *DLSInstr::AddRgn(void) {
    aRgns.insert(aRgns.end(), new DLSRgn());
    return aRgns.back();
}

DLSRgn *DLSInstr::AddRgn(DLSRgn rgn) {
    DLSRgn *newRgn = new DLSRgn();
    *newRgn = rgn;
    aRgns.insert(aRgns.end(), newRgn);
    return aRgns.back();
}

//  ******
//  DLSRgn
//  ******

DLSRgn::~DLSRgn(void) {
    if (Wsmp)
        delete Wsmp;
    if (Art)
        delete Art;
}

uint32_t DLSRgn::GetSize(void) {
    uint32_t size = 0;
    size += LIST_HDR_SIZE;  //"rgn2" list
    size += RGNH_SIZE;      // rgnh chunk
    if (Wsmp)
        size += Wsmp->GetSize();
    size += WLNK_SIZE;
    if (Art)
        size += Art->GetSize();
    return size;
}

void DLSRgn::Write(vector<uint8_t> &buf) {
    RiffFile::WriteLIST(buf, 0x72676E32, (uint32_t)(GetSize() - 8));  // write "rgn2" list
    PushTypeOnVectBE<uint32_t>(buf, 0x72676E68);                      //"rgnh"
    PushTypeOnVect<uint32_t>(buf, (uint32_t)(RGNH_SIZE - 8));         // size
    PushTypeOnVect<uint16_t>(buf, usKeyLow);                          // usLow  (key)
    PushTypeOnVect<uint16_t>(buf, usKeyHigh);                         // usHigh (key)
    PushTypeOnVect<uint16_t>(buf, usVelLow);                          // usLow  (vel)
    PushTypeOnVect<uint16_t>(buf, usVelHigh);                         // usHigh (vel)
    PushTypeOnVect<uint16_t>(buf, 1);                                 // fusOptions
    PushTypeOnVect<uint16_t>(buf, 0);                                 // usKeyGroup

    // new for dls2
    PushTypeOnVect<uint16_t>(buf, 1);  // NO CLUE

    if (Wsmp)
        Wsmp->Write(buf);  // write the "wsmp" chunk

    PushTypeOnVectBE<uint32_t>(buf, 0x776C6E6B);   //"wlnk"
    PushTypeOnVect<uint32_t>(buf, WLNK_SIZE - 8);  // size
    PushTypeOnVect<uint16_t>(buf, fusOptions);     // fusOptions
    PushTypeOnVect<uint16_t>(buf, usPhaseGroup);   // usPhaseGroup
    PushTypeOnVect<uint32_t>(buf, channel);        // ulChannel
    PushTypeOnVect<uint32_t>(buf, tableIndex);     // ulTableIndex

    if (Art)
        Art->Write(buf);
}

DLSArt *DLSRgn::AddArt(void) {
    Art = new DLSArt();
    return Art;
}

DLSWsmp *DLSRgn::AddWsmp(void) {
    Wsmp = new DLSWsmp();
    return Wsmp;
}

void DLSRgn::SetRanges(uint16_t keyLow, uint16_t keyHigh, uint16_t velLow, uint16_t velHigh) {
    usKeyLow = keyLow;
    usKeyHigh = keyHigh;
    usVelLow = velLow;
    usVelHigh = velHigh;
}

void DLSRgn::SetWaveLinkInfo(uint16_t options, uint16_t phaseGroup, uint32_t theChannel,
                             uint32_t theTableIndex) {
    fusOptions = options;
    usPhaseGroup = phaseGroup;
    channel = theChannel;
    tableIndex = theTableIndex;
}

//  ******
//  DLSArt
//  ******

DLSArt::~DLSArt() {
    DeleteVect(aConnBlocks);
}

uint32_t DLSArt::GetSize(void) {
    uint32_t size = 0;
    size += LIST_HDR_SIZE;  //"lar2" list
    size += 16;             //"art2" chunk + size + cbSize + cConnectionBlocks
    for (uint32_t i = 0; i < aConnBlocks.size(); i++)
        size += aConnBlocks[i]->GetSize();  // each connection block
    return size;
}

void DLSArt::Write(vector<uint8_t> &buf) {
    RiffFile::WriteLIST(buf, 0x6C617232, GetSize() - 8);           // write "lar2" list
    PushTypeOnVectBE<uint32_t>(buf, 0x61727432);                   //"art2"
    PushTypeOnVect<uint32_t>(buf, GetSize() - LIST_HDR_SIZE - 8);  // size
    PushTypeOnVect<uint32_t>(buf, 8);                              // cbSize
    PushTypeOnVect<uint32_t>(buf, (uint32_t)aConnBlocks.size());   // cConnectionBlocks
    for (uint32_t i = 0; i < aConnBlocks.size(); i++)
        aConnBlocks[i]->Write(buf);  // each connection block
}

void DLSArt::AddADSR(long attack_time, uint16_t atk_transform, long decay_time, long sustain_lev,
                     long release_time, uint16_t rls_transform) {
    aConnBlocks.insert(aConnBlocks.end(),
                       new ConnectionBlock(CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_ATTACKTIME,
                                           atk_transform, attack_time));
    aConnBlocks.insert(aConnBlocks.end(),
                       new ConnectionBlock(CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_DECAYTIME,
                                           CONN_TRN_NONE, decay_time));
    aConnBlocks.insert(aConnBlocks.end(),
                       new ConnectionBlock(CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_SUSTAINLEVEL,
                                           CONN_TRN_NONE, sustain_lev));
    aConnBlocks.insert(aConnBlocks.end(),
                       new ConnectionBlock(CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_RELEASETIME,
                                           rls_transform, release_time));
}

void DLSArt::AddPan(long pan) {
    aConnBlocks.insert(aConnBlocks.end(), new ConnectionBlock(CONN_SRC_NONE, CONN_SRC_NONE,
                                                              CONN_DST_PAN, CONN_TRN_NONE, pan));
}

//  ***************
//  ConnectionBlock
//  ***************

void ConnectionBlock::Write(vector<uint8_t> &buf) {
    PushTypeOnVect<uint16_t>(buf, usSource);       // usSource
    PushTypeOnVect<uint16_t>(buf, usControl);      // usControl
    PushTypeOnVect<uint16_t>(buf, usDestination);  // usDestination
    PushTypeOnVect<uint16_t>(buf, usTransform);    // usTransform
    PushTypeOnVect<int32_t>(buf, lScale);          // lScale
}

//  *******
//  DLSWsmp
//  *******

uint32_t DLSWsmp::GetSize(void) {
    uint32_t size = 0;
    size += 28;  // all the variables minus the loop info
    if (cSampleLoops)
        size += 16;  // plus the loop info
    return size;
}

void DLSWsmp::Write(vector<uint8_t> &buf) {
    PushTypeOnVectBE<uint32_t>(buf, 0x77736D70);   //"wsmp"
    PushTypeOnVect<uint32_t>(buf, GetSize() - 8);  // size
    PushTypeOnVect<uint32_t>(buf, 20);             // cbSize (size of structure without loop record)
    PushTypeOnVect<uint16_t>(buf, usUnityNote);    // usUnityNote
    PushTypeOnVect<int16_t>(buf, sFineTune);       // sFineTune
    PushTypeOnVect<int32_t>(buf, lAttenuation);    // lAttenuation
    PushTypeOnVect<uint32_t>(buf, 1);              // fulOptions
    PushTypeOnVect<uint32_t>(buf, cSampleLoops);   // cSampleLoops
    if (cSampleLoops)                              // if it loops, write the loop structure
    {
        PushTypeOnVect<uint32_t>(buf, 16);
        PushTypeOnVect<uint32_t>(buf, ulLoopType);  // ulLoopType
        PushTypeOnVect<uint32_t>(buf, ulLoopStart);
        PushTypeOnVect<uint32_t>(buf, ulLoopLength);
    }
}

void DLSWsmp::SetLoopInfo(Loop &loop, VGMSamp *samp) {
    const int origFormatBytesPerSamp = samp->bps / 8;
    double compressionRatio = samp->GetCompressionRatio();

    // If the sample loops, but the loop length is 0, then assume the length should
    // extend to the end of the sample.
    if (loop.loopStatus && loop.loopLength == 0)
        loop.loopLength = samp->dataLength - loop.loopStart;

    cSampleLoops = loop.loopStatus;
    ulLoopType = loop.loopType;
    // In DLS, the value is in number of samples
    // if the val is a raw offset of the original format, multiply it by the compression ratio
    ulLoopStart = (loop.loopStartMeasure == LM_BYTES)
                      ? (uint32_t)((loop.loopStart * compressionRatio) / origFormatBytesPerSamp)
                      : loop.loopStart;

    ulLoopLength = (loop.loopLengthMeasure == LM_BYTES)
                       ? (uint32_t)((loop.loopLength * compressionRatio) / origFormatBytesPerSamp)
                       : loop.loopLength;
}

void DLSWsmp::SetPitchInfo(uint16_t unityNote, short fineTune, long attenuation) {
    usUnityNote = unityNote;
    sFineTune = fineTune;
    lAttenuation = attenuation;
}

//  *******
//  DLSWave
//  *******

DLSWave::~DLSWave() {
    delete Wsmp;
    delete[] data;
}

uint32_t DLSWave::GetSize() {
    uint32_t size = 0;
    size += LIST_HDR_SIZE;  //"wave" list
    size += 8;              //"fmt " chunk + size
    size += 18;             // fmt chunk data
    if (Wsmp)
        size += Wsmp->GetSize();
    size += 8;                      //"data" chunk + size
    size += this->GetSampleSize();  // dataSize;			    //size of sample data

    size += LIST_HDR_SIZE;          //"INFO" list
    size += 8;                      //"INAM" + size
    size += (uint32_t)name.size();  // size of name string
    return size;
}

void DLSWave::Write(vector<uint8_t> &buf) {
    uint32_t theDWORD;

    RiffFile::WriteLIST(buf, 0x77617665, GetSize() - 8);  // write "wave" list
    PushTypeOnVectBE<uint32_t>(buf, 0x666D7420);          //"fmt "
    PushTypeOnVect<uint32_t>(buf, 18);                    // size
    PushTypeOnVect<uint16_t>(buf, wFormatTag);            // wFormatTag
    PushTypeOnVect<uint16_t>(buf, wChannels);             // wChannels
    PushTypeOnVect<uint32_t>(buf, dwSamplesPerSec);       // dwSamplesPerSec
    PushTypeOnVect<uint32_t>(buf, dwAveBytesPerSec);      // dwAveBytesPerSec
    PushTypeOnVect<uint16_t>(buf, wBlockAlign);           // wBlockAlign
    PushTypeOnVect<uint16_t>(buf, wBitsPerSample);        // wBitsPerSample

    PushTypeOnVect<uint16_t>(buf, 0);  // cbSize DLS2 specific.  I don't know anything else

    if (Wsmp)
        Wsmp->Write(buf);  // write the "wsmp" chunk

    PushTypeOnVectBE<uint32_t>(buf, 0x64617461);  //"data"
    PushTypeOnVect<uint32_t>(buf,
                             dataSize);  // size: this is the ACTUAL size, not the even-aligned size
    buf.insert(buf.end(), data, data + dataSize);  // Write the sample
    if (dataSize % 2)
        buf.push_back(0);
    theDWORD = 12 + (uint32_t)name.size();           //"INFO" + "INAM" + size + the string size
    RiffFile::WriteLIST(buf, 0x494E464F, theDWORD);  // write the "INFO" list
    PushTypeOnVectBE<uint32_t>(buf, 0x494E414D);     //"INAM"
    PushTypeOnVect<uint32_t>(buf, (uint32_t)name.size());  // size
    PushBackStringOnVector(buf, name);
}