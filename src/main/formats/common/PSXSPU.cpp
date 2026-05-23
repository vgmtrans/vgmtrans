// Many thanks to Neill Corlett for his work in analyzing the playstation SPU.
// Most of the code below is based on his work.
// Also, thanks to Antires for his ADPCM decompression routine.

#include "PSXSPU.h"
#include "formats/PS1/PS1Format.h"

using namespace std;


static bool isValidSampleStart(const RawFile* file, uint32_t offset, bool allowShort);
static bool isZero16(const RawFile* f, uint32_t ofs);
static bool isValidFilterShiftByte(uint8_t b);
static bool isValidFlagByte(uint8_t b);

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

    uint32_t nEndOffset = offset() + length();
    if (length() == 0) {
      nEndOffset = endOffset();
    }

    uint32_t i = offset();
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

      if (!isSample)
        break;

      // We've found the start of a sample. Now we determine its length and add it.

      uint32_t extraGunkLength = 0;
      uint8_t filterRangeByte = readByte(i + 16);
      uint8_t keyFlagByte = readByte(i + 16 + 1);

      if (!isValidFlagByte(keyFlagByte) || !isValidFilterShiftByte(filterRangeByte))
        break;

      if (isZero16(rawFile(), i + 16))
        break;

      uint32_t beginOffset = i;
      i += 16;

      //skip through until we reach the chunk with the end flag set
      uint32_t endLoopOffset = 0;  // This will mark the end of the first frame that has an end flag set.
                              // Occasionally, the data for a given sample may extend beyond this point
                              // for some reason. For conversion, we treat this as the end of the sample.
      while (i + 16 <= nEndOffset) {

        // This is uncommon, but seen in poorly-ripped PSFs
        if (isZero16(rawFile(), i)) {
          break;
        }

        uint8_t flagByte = readByte(i + 1);
        uint8_t endFlag = ((flagByte & 1) != 0);
        i += 16;

        if (endFlag) {
          endLoopOffset = endLoopOffset == 0 ? i : endLoopOffset;

          // We found an end flag. Check for vestigial ADPCM frames beyond it
          if (i + 32 < nEndOffset) {
            uint8_t nextFilterShiftByte = readByte(i);
            uint8_t nextFlagByte = readByte(i + 1);
            if (nextFlagByte < 1 || nextFlagByte > 3 || !isValidFilterShiftByte(nextFilterShiftByte)) {
              // Some games (ex: Ogre Battle) have a single weirdly-formatted ADPCM frame before a subsequent sample
              if ((nextFlagByte != 0 || nextFilterShiftByte != 0) && !isZero16(rawFile(), i + 16))
                break;
            }
          } else {
            break;
          }
        }
      }

      // Handle the case of an end frame using the format 00 07 77 77 77 77 77 etc
      while (i + 16 <= nEndOffset) {
        uint8_t flagByte = readByte(i + 1);
        if (flagByte == 7) {
          extraGunkLength += 16;
          i += 16;
        } else {
          break;
        }
      }
      // If endLoopOffset wasn't set, default it to the end of the sample
      endLoopOffset = endLoopOffset >= beginOffset ? endLoopOffset : i;
      PSXSamp *samp = new PSXSamp(this,
                                  beginOffset,
                                  i - beginOffset,
                                  beginOffset,
                                  endLoopOffset - beginOffset - extraGunkLength,
                                  1,
                                  BPS::PCM16,
                                  44100,
                                  fmt::format("Sample {:d}", samples.size()));
      samples.push_back(samp);
    }
    setLength(i - offset());
  }
  else {
    uint32_t sampleIndex = 0;

    if (!isValidSampleStart(rawFile(), offset(), true))
      return false;

    for (std::vector<SizeOffsetPair>::iterator it = vagLocations.begin(); it != vagLocations.end(); ++it) {
      if (it->offset == 0 && it->size == 0)
        continue;

      uint32_t offSampStart = offset() + it->offset;
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

      PSXSamp *samp = new PSXSamp(this,
                                  offset() + it->offset,
                                  it->size,
                                  offset() + it->offset,
                                  offSampEnd - offSampStart,
                                  1,
                                  BPS::PCM16,
                                  44100,
                                  fmt::format("Sample {:d}", sampleIndex));

      samples.push_back(samp);
      sampleIndex++;
    }
  }
  return length() > 0x20;
}

constexpr uint32_t NUM_CHUNKS_READAHEAD      = 10;
constexpr int MAX_FILTER_DIFF_SUM       = NUM_CHUNKS_READAHEAD * 2.5;
constexpr int MAX_RANGE_FILTER_DIFF_SUM = NUM_CHUNKS_READAHEAD * 3.2;
constexpr int MIN_UNIQUE_BYTES_STRICT   = NUM_CHUNKS_READAHEAD * 4;
constexpr int MAX_BYTE_REPETITION       = NUM_CHUNKS_READAHEAD * 5.5;
constexpr uint32_t BACK_SCAN_LIMIT           = 0x5000;

/// Check for a sequence of 16 null bytes - an empty ADPCM frame
static inline bool isZero16(const RawFile* f, uint32_t ofs) {
  if (!f->isValidOffset(ofs + 15)) return false;
  const uint64_t* blk = reinterpret_cast<const uint64_t*>(f->data() + ofs);
  return blk[0] == 0 && blk[1] == 0;
}

static inline bool isValidFilterShiftByte(uint8_t b) {
  uint8_t filter = b >> 4;
  uint8_t shift  = b & 0x0F;
  return filter <= 4 && shift <= 0x0C;
}

static inline bool isValidFlagByte(uint8_t b) {
  return (b & 0xF8) == 0;   // 0x00-0x07 are valid
}


/// Determine whether the offset is the start of a PSX ADPCM sample.
/// when allowShort is false, the algorithm is stricter and reads 10 frames of data (forward pass)
/// when allowShort is true, the algorithm is less strict and allows samples of any size (back scan)
static bool isValidSampleStart(const RawFile* file, uint32_t offset, bool allowShort) {
  if (!isZero16(file, offset)) return false;

  const uint32_t first = offset + 16;
  if (!file->isValidOffset(first + 15)) return false;
  if (file->readShort(first) == 0)      return false;

  uint8_t  chunk[16];
  int byteCount[256]     = {};
  int uniqueBytes        = 0;
  int sumFilterDiff      = 0;
  int sumRangeFilterDiff = 0;
  bool ok                = true;
  uint8_t  prevFirstByte      = 0;
  uint32_t framesSeen         = 0;

  for (uint32_t j = 0; j < NUM_CHUNKS_READAHEAD; ++j) {
    const uint32_t cur = first + j * 16;
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
    const uint8_t filterShift = chunk[0];
    const uint8_t flag = chunk[1];
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
  std::vector<PSXSampColl*> sampColls;
  const size_t len = file->size();

  for (uint32_t i = 0; i + 16 + NUM_CHUNKS_READAHEAD * 16 < len; ++i)
  {
    // Look for a 16-byte silent frame which usually indicates the start of a sample
    if (!isZero16(file, i))
    {
      uint8_t buf[16]; file->readBytes(i, 16, &buf);
      int nz = 15; while (nz && buf[nz] == 0) --nz;
      i += nz;
      continue;
    }

    // Use strict validation for the forward pass (10 frame readahead)
    if (!isValidSampleStart(file, i, false))
      continue;

    // We found a valid sample. Now back scan to check for shorter samples we might have skipped
    uint32_t origOffset = i;
    uint32_t start   = i;
    uint32_t scanned = 16;
    while (start - scanned >= 16 && scanned < BACK_SCAN_LIMIT)
    {
      const uint32_t offset = i - scanned;
      scanned += 16;

      // Look for a 16 null byte frame which usually prefixes a sample
      if (!isZero16(file, offset)) {
        // Make sure the data we scan past is still potentially valid frames
        uint8_t filterShiftByte = file->readByte(offset);
        uint8_t flagByte = file->readByte(offset + 1);
        if (!isValidFilterShiftByte(filterShiftByte) || !isValidFlagByte(flagByte)) {
          break;
        }
        continue;
      }

      if (!isValidSampleStart(file, offset, true))
        break;

      start = offset; // extend the start of the sample collection backward
    }

    auto* coll = new PSXSampColl(PS1Format::name, file, start);
    if (!coll->loadVGMFile()) { delete coll; continue; }

    sampColls.push_back(coll);

    // Sanity check that the detected sampcoll isn't smaller than the back scanned distance
    if ((start + coll->length() - 1) < origOffset) {
      i = origOffset + 32;
    } else {
      i = start + coll->length() - 1;                      // skip parsed area
    }
  }
  return sampColls;
}

//  *******
//  PSXSamp
//  *******

PSXSamp::PSXSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset,
                 uint32_t dataLen, uint8_t nChannels, BPS theBPS,
                 uint32_t theRate, string name, bool bSetloopOnConversion)
    : VGMSamp(sampColl, offset, length, dataOffset, dataLen, nChannels, theBPS, theRate, std::move(name)),
      bSetLoopOnConversion(bSetloopOnConversion) {
  bPSXLoopInfoPrioritizing = true;
}

PSXSamp::~PSXSamp() {
}

double PSXSamp::compressionRatio() const {
  return ((28.0 / 16.0) * 2);
}

std::vector<uint8_t> PSXSamp::decodeToNativePcm() {
  const uint32_t sampleCount = uncompressedSize() / sizeof(int16_t);
  std::vector<uint8_t> samples(sampleCount * sizeof(int16_t));
  auto *uncompBuf = reinterpret_cast<int16_t *>(samples.data());
  VAGBlk theBlock;
  int32_t  prev[2] = {0, 0};

  if (this->bSetLoopOnConversion)
    setLoopStatus(0); //loopStatus is initiated to -1.  We should default it now to not loop

  bool addrOutOfVirtFile = false;
  for (uint32_t k = 0; k < dataLength; k += 0x10)                //for every adpcm chunk
  {
    if (offset() + k + 16 > vgmFile()->endOffset()) {
      L_WARN("\"{}\" unexpected EOF.", name());
      break;
    }
    else if (!addrOutOfVirtFile && k + 16 > length()) {
      L_WARN("\"{}\" unexpected end of PSXSamp.", name());
      addrOutOfVirtFile = true;
    }

    theBlock.range = readByte(offset() + k) & 0xF;
    theBlock.filter = (readByte(offset() + k) & 0xF0) >> 4;
    theBlock.flag.end = readByte(offset() + k + 1) & 1;
    theBlock.flag.looping = (readByte(offset() + k + 1) & 2) > 0;

    //this can be the loop point, but in wd, this info is stored in the instrset
    theBlock.flag.loop = (readByte(offset() + k + 1) & 4) > 0;
    if (this->bSetLoopOnConversion) {
      if (theBlock.flag.loop) {
        this->setLoopOffset(k);
        this->setLoopLength(dataLength - k);
      }
      if (theBlock.flag.end && theBlock.flag.looping) {
        setLoopStatus(1);
      }
    }

    rawFile()->readBytes(offset() + k + 2, 14, theBlock.brr);

    //each decompressed pcm block is 56 bytes (28 samples, 16-bit each)
    decompVAGBlk(uncompBuf + ((k / 16) * 28), &theBlock, prev);
  }

  return samples;
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

inline void PSXSamp::decompVAGBlk(int16_t* pSmp, const VAGBlk* pVBlk, int32_t prev[2]) {
  static constexpr int16_t COEF[5][2] = {
    {   0,   0 }, {  60,   0 },
    { 115, -52 }, {  98, -55 },
    { 122, -60 }
  };

  const uint8_t shift  = pVBlk->range & 0x0F;          // 0–12
  const uint8_t filt   = std::min<uint8_t>(pVBlk->filter, 4);

  const int16_t c0 = COEF[filt][0];
  const int16_t c1 = COEF[filt][1];

  int32_t s1 = prev[0];
  int32_t s2 = prev[1];

  // iterate over every nibble sample in the 14 bytes of compressed samples
  for (int i = 0; i < 28; ++i) {
    // fetch 4-bit nibble and sign-extend to 8 bits
    const uint8_t byte   = static_cast<uint8_t>(pVBlk->brr[i >> 1]);
    const int8_t nibble = (i & 1) ? (byte >> 4) : (byte & 0x0F);
    const int8_t sn     = static_cast<int8_t>(nibble << 4) >> 4;

    // core formula: ((sn << 12) >> shift) + predictor
    int32_t sample      = (static_cast<int32_t>(sn) << 12) >> shift;
    sample         += ((c0 * s1 + c1 * s2) >> 6);

    // saturate to signed 16-bit
    sample = std::clamp(sample, -0x8000, 0x7FFF);

    pSmp[i] = static_cast<int16_t>(sample);

    s2 = s1;
    s1 = sample;
  }

  prev[0] = s1;
  prev[1] = s2;
}
