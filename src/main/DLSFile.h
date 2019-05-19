/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "common.h"
#include "RiffFile.h"

struct Loop;
class VGMSamp;

/*
   Articulation connection graph definitions
*/

/* Generic Sources */
#define CONN_SRC_NONE 0x0000
#define CONN_SRC_LFO 0x0001
#define CONN_SRC_KEYONVELOCITY 0x0002
#define CONN_SRC_KEYNUMBER 0x0003
#define CONN_SRC_EG1 0x0004
#define CONN_SRC_EG2 0x0005
#define CONN_SRC_PITCHWHEEL 0x0006

/* Midi Controllers 0-127 */
#define CONN_SRC_CC1 0x0081
#define CONN_SRC_CC7 0x0087
#define CONN_SRC_CC10 0x008a
#define CONN_SRC_CC11 0x008b

/* Registered Parameter Numbers */
#define CONN_SRC_RPN0 0x0100
#define CONN_SRC_RPN1 0x0101
#define CONN_SRC_RPN2 0x0102

/* Generic Destinations */
#define CONN_DST_NONE 0x0000
#define CONN_DST_ATTENUATION 0x0001
#define CONN_DST_RESERVED 0x0002
#define CONN_DST_PITCH 0x0003
#define CONN_DST_PAN 0x0004

/* LFO Destinations */
#define CONN_DST_LFO_FREQUENCY 0x0104
#define CONN_DST_LFO_STARTDELAY 0x0105

/* EG1 Destinations */
#define CONN_DST_EG1_ATTACKTIME 0x0206
#define CONN_DST_EG1_DECAYTIME 0x0207
#define CONN_DST_EG1_RESERVED 0x0208
#define CONN_DST_EG1_RELEASETIME 0x0209
#define CONN_DST_EG1_SUSTAINLEVEL 0x020a

/* EG2 Destinations */
#define CONN_DST_EG2_ATTACKTIME 0x030a
#define CONN_DST_EG2_DECAYTIME 0x030b
#define CONN_DST_EG2_RESERVED 0x030c
#define CONN_DST_EG2_RELEASETIME 0x030d
#define CONN_DST_EG2_SUSTAINLEVEL 0x030e

#define CONN_TRN_NONE 0x0000
#define CONN_TRN_CONCAVE 0x0001

#define F_INSTRUMENT_DRUMS 0x80000000

#define COLH_SIZE (4 + 8)
#define INSH_SIZE (12 + 8)
#define RGNH_SIZE (14 + 8)  //(12+8)
#define WLNK_SIZE (12 + 8)
#define LIST_HDR_SIZE 12

class DLSInstr;
class DLSRgn;
class DLSArt;
class connectionBlock;
class DLSWsmp;
class DLSWave;

class DLSFile : public RiffFile {
   public:
    DLSFile(std::string dls_name = "Instrument Set");
    ~DLSFile(void);

    DLSInstr *AddInstr(unsigned long bank, unsigned long instrNum);
    DLSInstr *AddInstr(unsigned long bank, unsigned long instrNum, std::string Name);
    void DeleteInstr(unsigned long bank, unsigned long instrNum);
    DLSWave *AddWave(uint16_t formatTag, uint16_t channels, int samplesPerSec, int aveBytesPerSec,
                     uint16_t blockAlign, uint16_t bitsPerSample, uint32_t waveDataSize,
                     uint8_t *waveData, std::string name = "Unnamed Wave");
    void SetName(std::string dls_name);

    uint32_t GetSize(void);

    int WriteDLSToBuffer(std::vector<uint8_t> &buf);
    bool SaveDLSFile(const std::wstring &filepath);

   public:
    std::vector<DLSInstr *> aInstrs;
    std::vector<DLSWave *> aWaves;
};

class DLSInstr {
   public:
    DLSInstr(void);
    DLSInstr(uint32_t bank, uint32_t instrument);
    DLSInstr(uint32_t bank, uint32_t instrument, std::string instrName);
    DLSInstr(uint32_t bank, uint32_t instrument, std::string instrName,
             std::vector<DLSRgn *> listRgns);
    ~DLSInstr(void);

    void AddRgnList(std::vector<DLSRgn> &RgnList);
    DLSRgn *AddRgn(void);
    DLSRgn *AddRgn(DLSRgn rgn);

    uint32_t GetSize(void);
    void Write(std::vector<uint8_t> &buf);

   public:
    uint32_t ulBank;
    uint32_t ulInstrument;

    std::vector<DLSRgn *> aRgns;
    std::string name;
};

class DLSRgn {
   public:
    DLSRgn(void) : Wsmp(NULL), Art(NULL) {}
    DLSRgn(uint16_t keyLow, uint16_t keyHigh, uint16_t velLow, uint16_t velHigh)
        : usKeyLow(keyLow),
          usKeyHigh(keyHigh),
          usVelLow(velLow),
          usVelHigh(velHigh),
          Wsmp(NULL),
          Art(NULL) {}
    DLSRgn(uint16_t keyLow, uint16_t keyHigh, uint16_t velLow, uint16_t velHigh, DLSArt &art);
    ~DLSRgn(void);

    DLSArt *AddArt(void);
    DLSArt *AddArt(std::vector<connectionBlock *> connBlocks);
    DLSWsmp *AddWsmp(void);
    DLSWsmp *AddWsmp(DLSWsmp wsmp);
    void SetRanges(uint16_t keyLow = 0, uint16_t keyHigh = 0x7F, uint16_t velLow = 0,
                   uint16_t velHigh = 0x7F);
    void SetWaveLinkInfo(uint16_t options, uint16_t phaseGroup, uint32_t theChannel,
                         uint32_t theTableIndex);

    uint32_t GetSize(void);
    void Write(std::vector<uint8_t> &buf);

   public:
    uint16_t usKeyLow;
    uint16_t usKeyHigh;
    uint16_t usVelLow;
    uint16_t usVelHigh;

    uint16_t fusOptions;
    uint16_t usPhaseGroup;
    uint32_t channel;
    uint32_t tableIndex;

    DLSWsmp *Wsmp;
    DLSArt *Art;
};

class ConnectionBlock {
   public:
    ConnectionBlock(void);
    ConnectionBlock(uint16_t source, uint16_t control, uint16_t destination, uint16_t transform,
                    int32_t scale)
        : usSource(source),
          usControl(control),
          usDestination(destination),
          usTransform(transform),
          lScale(scale) {}
    ~ConnectionBlock(void) {}

    uint32_t GetSize(void) { return 12; }
    void Write(std::vector<uint8_t> &buf);

   private:
    uint16_t usSource;
    uint16_t usControl;
    uint16_t usDestination;
    uint16_t usTransform;
    int32_t lScale;
};

class DLSArt {
   public:
    DLSArt(void) {}
    DLSArt(std::vector<ConnectionBlock> &connectionBlocks);
    ~DLSArt(void);

    void AddADSR(long attack_time, uint16_t atk_transform, long decay_time, long sustain_lev,
                 long release_time, uint16_t rls_transform);
    void AddPan(long pan);

    uint32_t GetSize(void);
    void Write(std::vector<uint8_t> &buf);

   private:
    std::vector<ConnectionBlock *> aConnBlocks;
};

class DLSWsmp {
   public:
    DLSWsmp(void) {}
    DLSWsmp(uint16_t unityNote, int16_t fineTune, int32_t attenuation, char sampleLoops,
            uint32_t loopType, uint32_t loopStart, uint32_t loopLength)
        : usUnityNote(unityNote),
          sFineTune(fineTune),
          lAttenuation(attenuation),
          cSampleLoops(sampleLoops),
          ulLoopType(loopType),
          ulLoopStart(loopStart),
          ulLoopLength(loopLength) {}
    ~DLSWsmp(void) {}

    void SetLoopInfo(Loop &loop, VGMSamp *samp);
    void SetPitchInfo(uint16_t unityNote, short fineTune, long attenuation);

    uint32_t GetSize(void);
    void Write(std::vector<uint8_t> &buf);

   private:
    unsigned short usUnityNote;
    short sFineTune;
    long lAttenuation;
    char cSampleLoops;

    uint32_t ulLoopType;
    uint32_t ulLoopStart;
    uint32_t ulLoopLength;
};

class DLSWave {
   public:
    DLSWave(void) : Wsmp(NULL), data(NULL), name("Untitled Wave") { RiffFile::AlignName(name); }
    DLSWave(uint16_t formatTag, uint16_t channels, int samplesPerSec, int aveBytesPerSec,
            uint16_t blockAlign, uint16_t bitsPerSample, uint32_t waveDataSize,
            unsigned char *waveData, std::string waveName = "Untitled Wave")
        : wFormatTag(formatTag),
          wChannels(channels),
          dwSamplesPerSec(samplesPerSec),
          dwAveBytesPerSec(aveBytesPerSec),
          wBlockAlign(blockAlign),
          wBitsPerSample(bitsPerSample),
          dataSize(waveDataSize),
          data(waveData),
          Wsmp(NULL),
          name(waveName) {
        RiffFile::AlignName(name);
    }
    ~DLSWave(void);

    //	This function will always return an even value, to maintain the alignment
    // necessary for the RIFF format.
    unsigned long GetSampleSize(void) {
        if (dataSize % 2)
            return dataSize + 1;
        else
            return dataSize;
    }
    uint32_t GetSize(void);
    void Write(std::vector<uint8_t> &buf);

   private:
    DLSWsmp *Wsmp;

    unsigned short wFormatTag;
    unsigned short wChannels;
    uint32_t dwSamplesPerSec;
    uint32_t dwAveBytesPerSec;
    unsigned short wBlockAlign;
    unsigned short wBitsPerSample;

    unsigned long dataSize;
    unsigned char *data;

    std::string name;
};
