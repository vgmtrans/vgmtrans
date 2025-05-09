// Many thanks to Neill Corlett for his work in analyzing the playstation SPU.
// Most of the code below is based on his work.
// Also, thanks to Antires for his ADPCM decompression routine.

#include "PSXSPU.h"
#include "formats/PS1/PS1Format.h"

using namespace std;



// ***********
// PSXSampColl
// ***********

PSXSampColl::PSXSampColl(const string &format, RawFile *rawfile, uint32_t offset, uint32_t length)
    : VGMSampColl(format, rawfile, offset, length, "PSX Sample Collection") {
}

PSXSampColl::PSXSampColl(const string &format, VGMInstrSet *instrset, uint32_t offset, uint32_t length)
    : VGMSampColl(format, instrset->rawFile(), instrset, offset, length, "PSX Sample Collection") {
}

PSXSampColl::PSXSampColl(const string &format,
                         VGMInstrSet *instrset,
                         uint32_t offset,
                         uint32_t length,
                         const std::vector<SizeOffsetPair> &vagLocations)
    : VGMSampColl(format, instrset->rawFile(), instrset, offset, length, "PSX Sample Collection"),
      vagLocations(vagLocations) {
}

bool PSXSampColl::parseSampleInfo() {
  if (vagLocations.size() == 0) {
    //We scan through the sample section, and determine the offsets and size of each sample
    //We do this by searching for series of 16 0x00 value bytes.  These indicate the beginning of a sample,
    //and they will never be found at any other point within the adpcm sample data.

    uint32_t nEndOffset = dwOffset + unLength;
    if (unLength == 0)
      nEndOffset = endOffset();

    uint32_t i = dwOffset;
    while (i + 32 <= nEndOffset) {
      bool isSample = false;

      if (readWord(i) == 0 && readWord(i + 4) == 0 && readWord(i + 8) == 0 && readWord(i + 12) == 0) {
        // most of samples starts with 0s
        isSample = true;
      }
      else {
        // some sample blocks may not start with 0.
        // so here is a dirty hack for it.
        // (Dragon Quest VII, for example)
        int countOfContinue = 0;
        uint8_t continueByte = 0xff;
        bool badBlock = false;
        while (i + (countOfContinue * 16) + 16 <= nEndOffset) {
          uint8_t keyFlagByte = readByte(i + (countOfContinue * 16) + 1);

          if ((keyFlagByte & 0xF8) != 0) {
            badBlock = true;
            break;
          }

          if (continueByte == 0xff) {
            if (keyFlagByte == 0 || keyFlagByte == 2) {
              continueByte = keyFlagByte;
            }
          }

          if (keyFlagByte != continueByte) {
            if (keyFlagByte == 0 || keyFlagByte == 2) {
              badBlock = true;
            }
            break;
          }
          countOfContinue++;
        }
        if (!badBlock && ((continueByte == 0 && countOfContinue >= 16) ||
            (continueByte == 2 && countOfContinue >= 3))) {
          isSample = true;
        }
      }

      if (isSample) {
        uint32_t extraGunkLength = 0;
        uint8_t filterRangeByte = readByte(i + 16);
        uint8_t keyFlagByte = readByte(i + 16 + 1);
        if ((keyFlagByte & 0xF8) != 0)
          break;

        //if (filterRangeByte == 0 && keyFlagByte == 0)	// Breaking on FFXII 309 - Eruyt Village at 61D50 of the WD
        if (readWord(i + 16) == 0 && readWord(i + 20) == 0 && readWord(i + 24) == 0 && readWord(i + 28) == 0)
          break;

        uint32_t beginOffset = i;
        i += 16;

        //skip through until we reach the chunk with the end flag set
        bool loopEnd = false;
        while (i + 16 <= nEndOffset && !loopEnd) {
          loopEnd = ((readByte(i + 1) & 1) != 0);
          i += 16;
        }

        //deal with exceptional cases where we see 00 07 77 77 77 77 77 etc.
        while (i + 16 <= nEndOffset) {
          loopEnd = ((readByte(i + 1) & 1) != 0);
          if (!loopEnd) {
            break;
          }
          extraGunkLength += 16;
          i += 16;
        }

        ostringstream name;
        name << "Sample " << samples.size();
        PSXSamp *samp = new PSXSamp(this,
                                    beginOffset,
                                    i - beginOffset,
                                    beginOffset,
                                    i - beginOffset - extraGunkLength,
                                    1,
                                    16,
                                    44100,
                                    name.str());
        samples.push_back(samp);
      }
      else
        break;
    }
    unLength = i - dwOffset;
  }
  else {
    uint32_t sampleIndex = 0;
    for (std::vector<SizeOffsetPair>::iterator it = vagLocations.begin(); it != vagLocations.end(); ++it) {
      if (it->offset == 0 && it->size == 0)
        continue;

      uint32_t offSampStart = dwOffset + it->offset;
      uint32_t offDataEnd = offSampStart + it->size;
      uint32_t offSampEnd = offSampStart;

      // detect loop end and ignore garbages like 00 07 77 77 77 77 77 etc.
      bool lastBlock;
      do {
        if (offSampEnd + 16 > offDataEnd) {
          offSampEnd = offDataEnd;
          break;
        }

        lastBlock = ((readByte(offSampEnd + 1) & 1) != 0);
        offSampEnd += 16;
      } while (!lastBlock);

      ostringstream name;
      name << "Sample " << sampleIndex;
      PSXSamp *samp = new PSXSamp(this,
                                  dwOffset + it->offset,
                                  it->size,
                                  dwOffset + it->offset,
                                  offSampEnd - offSampStart,
                                  1,
                                  16,
                                  44100,
                                  name.str());
      samples.push_back(samp);
      sampleIndex++;
    }
  }
  return unLength > 0x20;
}

// GENERIC FUNCTION USED FOR SCANNERS
PSXSampColl *PSXSampColl::searchForPSXADPCM(RawFile *file, const string &format) {
  const std::vector<PSXSampColl *> &sampColls = searchForPSXADPCMs(file, format);
  if (sampColls.size() != 0) {
    // pick up one of the SampColls
    size_t bestSampleCount = 0;
    PSXSampColl *bestSampColl = sampColls[0];
    for (size_t i = 0; i < sampColls.size(); i++) {
      if (sampColls[i]->samples.size() > bestSampleCount) {
        bestSampleCount = sampColls[i]->samples.size();
        bestSampColl = sampColls[i];
      }
    }
    return bestSampColl;
  }
  else {
    return nullptr;
  }
}

constexpr u32 NUM_CHUNKS_READAHEAD      = 10;
constexpr int MAX_FILTER_DIFF_SUM       = NUM_CHUNKS_READAHEAD * 2.5;
constexpr int MAX_RANGE_FILTER_DIFF_SUM = NUM_CHUNKS_READAHEAD * 3.2;
constexpr int MIN_UNIQUE_BYTES_STRICT   = NUM_CHUNKS_READAHEAD * 4;
constexpr int MAX_BYTE_REPETITION       = NUM_CHUNKS_READAHEAD * 5.5;
constexpr u32 BACK_SCAN_LIMIT           = 0x5000;

/// Check for a sequence of 16 null bytes - an empty ADPCM frame
static inline bool isZero16(const RawFile* f, u32 ofs) {
  if (!f->isValidOffset(ofs + 15)) return false;
  const u64* blk = reinterpret_cast<const u64*>(f->data() + ofs);
  return blk[0] == 0 && blk[1] == 0;
}

static inline bool isValidFilterShiftByte(u8 b) {
  u8 filter = b >> 4;
  u8 shift  = b & 0x0F;
  return filter <= 4 && shift <= 0x0C;
}

static inline bool isValidFlagByte(u8 b) {
  return (b & 0xF8) == 0;   // 0x00-0x07 are valid
}


/// Determine whether the offset is the start of a PSX ADPCM sample.
/// when allowShort is false, the algorithm is stricter and reads 10 frames of data (forward pass)
/// when allowShort is true, the algorithm is less strict and allows samples of any size (back scan)
static bool isValidSampleStart(const RawFile* file, u32 offset, bool allowShort) {
  if (!isZero16(file, offset)) return false;

  const u32 first = offset + 16;
  if (!file->isValidOffset(first + 15)) return false;
  if (file->readShort(first) == 0)      return false;

  u8  chunk[16];
  int byteCount[256]     = {};
  int uniqueBytes        = 0;
  int sumFilterDiff      = 0;
  int sumRangeFilterDiff = 0;
  bool ok                = true;
  u8  prevFirstByte      = 0;
  u32 framesSeen         = 0;

  for (u32 j = 0; j < NUM_CHUNKS_READAHEAD; ++j) {
    const u32 cur = first + j * 16;
    file->readBytes(cur, 16, &chunk);

    if (isZero16(file, cur)) {
      if (!allowShort)          // disallow any null frames in the readahead range in strict mode
        ok = false;
      if (cur == offset + 16)   // always disallow two consecutive 16 null frames
        ok = false;
      break;
    }
    ++framesSeen;

    // check flag byte and filter/shift byte validity
    const u8 filterShift = chunk[0];
    const u8 flag = chunk[1];
    if (!isValidFlagByte(flag) || !isValidFilterShiftByte(filterShift)) {
      ok = false;
      break;
    }

    // uniqueness statistics
    for (int k = 2; k < 16; ++k) {
      byteCount[chunk[k]]++;
      if (byteCount[chunk[k]] == 1)
        ++uniqueBytes;
    }

    // improbable pattern guard
    if (j == 0 && chunk[0] == 0 && chunk[1] == 0) {
      int zeros = 0;
      for (int k = 2; k < 16; ++k) {
        if (chunk[k] == 0)
          ++zeros;
      }
      if (zeros >= 10) {
        ok = false;
        break;
      }
    }

    // predictor/range continuity
    const int filt = (chunk[0] & 0xF0) >> 4;
    const int rng  =  chunk[0] & 0x0F;
    if (j) {
      const int pf = (prevFirstByte & 0xF0) >> 4;
      const int pr =  prevFirstByte & 0x0F;
      sumFilterDiff      += abs(filt - pf);
      sumRangeFilterDiff += abs(rng  - pr);
    }
    prevFirstByte = chunk[0];
  }

  if (!ok)
    return false;
  if (sumFilterDiff > MAX_FILTER_DIFF_SUM)
    return false;
  if (sumRangeFilterDiff > MAX_RANGE_FILTER_DIFF_SUM)
    return false;

  // dynamic uniqueness: strict needs many; relaxed needs few
  const int minUnique = allowShort ? 8 : MIN_UNIQUE_BYTES_STRICT;
  if (uniqueBytes < minUnique)
    return false;

  for (int k = 0; k < 256; ++k) {
    if (byteCount[k] > MAX_BYTE_REPETITION)
      return false;
  }

  // if strict path, must have looked at all 10 frames
  if (!allowShort && framesSeen < NUM_CHUNKS_READAHEAD)
    return false;

  return true;
}

std::vector<PSXSampColl*> PSXSampColl::searchForPSXADPCMs(RawFile* file, const std::string&) {
  std::vector<PSXSampColl*> out;
  const size_t len = file->size();

  for (u32 i = 0; i + 16 + NUM_CHUNKS_READAHEAD * 16 < len; ++i)
  {
    // Look for a 16-byte silent frame which usually indicates the start of a sample
    if (!isZero16(file, i))
    {
      u8 buf[16]; file->readBytes(i, 16, &buf);
      int nz = 15; while (nz && buf[nz] == 0) --nz;
      i += nz;
      continue;
    }

    // Use strict validation for the forward pass (10 frame readahead)
    if (!isValidSampleStart(file, i, false))
      continue;

    // We found a valid sample. Now back scan to check for shorter samples we might have skipped
    u32 start   = i;
    u32 scanned = 16;
    while (start - scanned >= 16 && scanned < BACK_SCAN_LIMIT)
    {
      const u32 offset = i - scanned;
      scanned += 16;

      // Look for a 16 null byte frame which usually prefixes a sample
      if (!isZero16(file, offset))
        continue;

      if (!isValidSampleStart(file, offset, true))
        break;

      start = offset; // extend the start of the sample collection backward
    }

    auto* coll = new PSXSampColl(PS1Format::name, file, start);
    if (!coll->loadVGMFile()) { delete coll; continue; }

    out.push_back(coll);
    i = start + coll->unLength - 1;                      // skip parsed area
  }
  return out;
}

//  *******
//  PSXSamp
//  *******

PSXSamp::PSXSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset,
                 uint32_t dataLen, uint8_t nChannels, uint16_t theBPS,
                 uint32_t theRate, string name, bool bSetloopOnConversion)
    : VGMSamp(sampColl, offset, length, dataOffset, dataLen, nChannels, theBPS, theRate, std::move(name)),
      bSetLoopOnConversion(bSetloopOnConversion) {
  bPSXLoopInfoPrioritizing = true;
}

PSXSamp::~PSXSamp() {
}

double PSXSamp::compressionRatio() {
  return ((28.0 / 16.0) * 2);
}

void PSXSamp::convertToStdWave(uint8_t *buf) {
  int16_t *uncompBuf = reinterpret_cast<int16_t*>(buf);
  VAGBlk theBlock;
  f32 prev1 = 0;
  f32 prev2 = 0;

  if (this->bSetLoopOnConversion)
    setLoopStatus(0); //loopStatus is initiated to -1.  We should default it now to not loop

  bool addrOutOfVirtFile = false;
  for (uint32_t k = 0; k < dataLength; k += 0x10)                //for every adpcm chunk
  {
    if (dwOffset + k + 16 > vgmFile()->endOffset()) {
      L_WARN("\"{}\" unexpected EOF.", name());
      break;
    }
    else if (!addrOutOfVirtFile && k + 16 > unLength) {
      L_WARN("\"{}\" unexpected end of PSXSamp.", name());
      addrOutOfVirtFile = true;
    }

    theBlock.range = readByte(dwOffset + k) & 0xF;
    theBlock.filter = (readByte(dwOffset + k) & 0xF0) >> 4;
    theBlock.flag.end = readByte(dwOffset + k + 1) & 1;
    theBlock.flag.looping = (readByte(dwOffset + k + 1) & 2) > 0;

    //this can be the loop point, but in wd, this info is stored in the instrset
    theBlock.flag.loop = (readByte(dwOffset + k + 1) & 4) > 0;
    if (this->bSetLoopOnConversion) {
      if (theBlock.flag.loop) {
        this->setLoopOffset(k);
        this->setLoopLength(dataLength - k);
      }
      if (theBlock.flag.end && theBlock.flag.looping) {
        setLoopStatus(1);
      }
    }

    rawFile()->readBytes(dwOffset + k + 2, 14, theBlock.brr);

    //each decompressed pcm block is 56 bytes (28 samples, 16-bit each)
    decompVAGBlk(uncompBuf + ((k / 16) * 28),
                 &theBlock,
                 &prev1,
                 &prev2);
  }
}

uint32_t PSXSamp::getSampleLength(const RawFile *file, uint32_t offset, uint32_t endOffset, bool &loop) {
  uint32_t curOffset = offset;
  while (curOffset < endOffset) {
    uint8_t keyFlagByte = file->readByte(curOffset + 1);

    curOffset += 16;

    if ((keyFlagByte & 1) != 0) {
      loop = (keyFlagByte & 2) != 0;
      break;
    }
  }

  if (curOffset <= endOffset) {
    return curOffset - offset;
  }
  else {
    // address out of range
    return 0;
  }
}

//This next function is taken from Antires's work
void PSXSamp::decompVAGBlk(s16 *pSmp, const VAGBlk* pVBlk, f32 *prev1, f32 *prev2) {
  u32 i, shift;                                //Shift amount for compressed samples
  f32 t;                                       //Temporary sample
  f32 f1, f2;
  f32 p1, p2;
  static const f32 Coeff[5][2] = {
      {0.0, 0.0},
      {60.0 / 64.0, 0.0},
      {115.0 / 64.0, 52.0 / 64.0},
      {98.0 / 64.0, 55.0 / 64.0},
      {122.0 / 64.0, 60.0 / 64.0}};


  //Expand samples ---------------------------
  shift = pVBlk->range + 16;

  for (i = 0; i < 14; i++) {
    pSmp[i * 2] = (static_cast<s32>(pVBlk->brr[i]) << 28) >> shift;
    pSmp[i * 2 + 1] = (static_cast<s32>(pVBlk->brr[i] & 0xF0) << 24) >> shift;
  }

  //Apply ADPCM decompression ----------------
  i = pVBlk->filter;

  if (i) {
    f1 = Coeff[i][0];
    f2 = Coeff[i][1];
    p1 = *prev1;
    p2 = *prev2;

    for (i = 0; i < 28; i++) {
      t = pSmp[i] + (p1 * f1) - (p2 * f2);
      pSmp[i] = static_cast<s16>(t);
      p2 = p1;
      p1 = t;
    }

    *prev1 = p1;
    *prev2 = p2;
  }
  else {
    *prev2 = pSmp[26];
    *prev1 = pSmp[27];
  }
}