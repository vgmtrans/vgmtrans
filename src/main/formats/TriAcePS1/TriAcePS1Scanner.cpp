/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "base/Types.h"
#include "ScannerManager.h"
#include "TriAcePS1InstrSet.h"
#include "TriAcePS1Seq.h"
#include "VGMColl.h"

#include <memory>

namespace vgmtrans::scanners {
ScannerRegistration<TriAcePS1Scanner> s_triace_ps1("TriAcePS1");
}

#define DEFAULT_UFSIZE 0x100000

void TriAcePS1Scanner::scan(RawFile *file, void *info) {
  searchForSLZSeq(file);
  return;
}

void TriAcePS1Scanner::searchForSLZSeq(RawFile *file) {
  size_t nFileLength = file->size();
  for (u32 i = 0; i + 0x40 < nFileLength; i++) {
    u32 sig1 = file->readWordBE(i);
    u8 slzMode;

    slzMode = sig1 & 0xFF;
    sig1 >>= 8;
    if (sig1 != 0x534C5A)    // "SLZ" in ASCII
      continue;
    if (slzMode > 0x03)
      continue;    // only SLZ v0-3 is supported
    // Note: SLZ v2 is used by a few tracks in Valkyrie Profile.

    u16 headerBytes = file->readShort(i + 0x11);

    if (headerBytes != 0xFFFF)        //First two bytes of the sequence is always 0xFFFF
      continue;

    u32 size1 = file->readWord(i + 4);     //unknown.  compressed size or something
    u32 size2 = file->readWord(i + 8);     //uncompressed file size (size of resulting file after decompression)
    u32 size3 = file->readWord(i + 12);    //unknown compressed file size or something

    if (size1 > 0x30000 || size2 > 0x30000 || size3 > 0x30000)    //sanity check.  Sequences won't be > 0x30000 bytes
      continue;
    if (size1 > size2 || size3 > size2)    //the compressed file size ought to be smaller than the decompressed size
      continue;

    TriAcePS1Seq *seq = decompressTriAceSLZFile(file, i);
    if (!seq)
      return;
    if (i < 4 || file->readWord(i - 4) == 0)
      return;

    // The size of the compressed file is 4 bytes behind the SLZ sig.
    // We need this to calculate the offset of the InstrSet, which comes immediately after the SLZ'd sequence.
    std::vector<TriAcePS1InstrSet *> instrsets;
    searchForInstrSet(file, instrsets);

    //u32 cfSize = file->GetWord(i-4);
    //TriAcePS1InstrSet* instrset = new TriAcePS1InstrSet(file, i-4 + cfSize);
    //if (!instrset->LoadVGMFile())
    //{
    //	delete instrset;
    //	return;
    //}
    if (!instrsets.size())
      return;

    std::string name = file->tag.hasTitle() ? file->tag.title : file->stem();
    VGMColl *coll = new VGMColl(name);
    coll->useSeq(seq);
    for (u32 setIdx = 0; setIdx < instrsets.size(); setIdx++)
      coll->addInstrSet(instrsets[setIdx]);
    if (!coll->load()) {
      delete coll;
    }
  }
}

void TriAcePS1Scanner::searchForInstrSet(RawFile *file, std::vector<TriAcePS1InstrSet *> &instrsets) {
  size_t nFileLength = file->size();
  for (u32 i = 4; i + 0x800 < nFileLength; i++) {
    u8 precedingByte = file->readByte(i + 3);
    if (precedingByte != 0)
      continue;

    // The 32 byte value at offset 8 seems to be bank #.  In practice, i don't think it is ever > 1
    if (file->readWord(i + 8) > 0xFF)
      continue;

    u16 instrSectSize = file->readShort(i + 4);
    // The instrSectSize should be more than the size of one instrdata block and not insanely large
    if (instrSectSize <= 0x20 || instrSectSize > 0x4000)
      continue;
    // Make sure the size doesn't point beyond the end of the file
    if (i + instrSectSize > file->size() - 0x100)
      continue;

    u32 instrSetSize = file->readWord(i);
    if (instrSetSize < 0x1000)
      continue;
    // The entire InstrSet size must be larger than the instrdata region, of course
    if (instrSetSize <= instrSectSize)
      continue;

    // First 0x10 bytes of sample section should be 0s
    if (file->readWord(i + instrSectSize) != 0 || file->readWord(i + instrSectSize + 4) != 0 ||
        file->readWord(i + instrSectSize + 8) != 0 || file->readWord(i + instrSectSize + 12) != 0)
      continue;
    // Last 4 bytes of Instr section should be 0xFFFFFFFF
    if (file->readWord(i + instrSectSize - 4) != 0xFFFFFFFF)
      continue;

    TriAcePS1InstrSet *instrset = new TriAcePS1InstrSet(file, i);
    if (!instrset->loadVGMFile()) {
      delete instrset;
      continue;
    }
    instrsets.push_back(instrset);
  }
  return;
}


// file is RawFile containing the compressed seq.  cfOff is the compressed file offset.
TriAcePS1Seq *TriAcePS1Scanner::decompressTriAceSLZFile(RawFile *file, u32 cfOff) {
  u8 cmode = file->readByte(cfOff + 3);                //compression mode
  // Header +4 is the compressed file size; +12 is the compressed block size.
  u32 ufSize =
      file->readWord(cfOff + 8);            //uncompressed file size (size of resulting file after decompression)

  if (ufSize == 0)
    ufSize = DEFAULT_UFSIZE;

  u8 *uf = new u8[ufSize];

  u32 ufOff = 0;
  cfOff += 0x10;
  if (cmode == 0) {
    ufOff += file->readBytes(cfOff, ufSize, uf);
  } else {
    // The decompression code is based on CUE's SLZ decompressor.
    // compression mode: 0/STORE, 1/LZSS, 2/LZSS+RLE, 3/LZSS16
    bool bDone = false;
    int bits = (cmode == 3) ? 16 : 8;
    while (ufOff < ufSize && !bDone) {
      u16 cFlags;
      if (bits == 8)
        cFlags = file->readByte(cfOff++);
      else {
        cFlags = file->readShort(cfOff);
        cfOff += 2;
      }
      for (int i = 0; (i < bits) && (ufOff < ufSize); i++, cFlags >>= 1) {
        if (cFlags & 1)  //uncompressed byte, just copy it over
        {
          uf[ufOff++] = file->readByte(cfOff++);
          if (bits == 16)
            uf[ufOff++] = file->readByte(cfOff++);
        }
        else  //compressed section
        {
          u8 byte1 = file->readByte(cfOff);
          u8 byte2 = file->readByte(cfOff + 1);
          if (byte1 == 0 && byte2 == 0) {
            bDone = true;
            break;
          }
          cfOff += 2;

          u8 bytesToRead;
          if (cmode == 2 && byte2 >= 0xF0) {
            if (byte2 == 0xF0) {
              bytesToRead = byte1 + 0x13;
              byte1 = file->readByte(cfOff++);
            } else {
              bytesToRead = (byte2 & 0x0F) + 3;
            }
            for (; bytesToRead > 0; bytesToRead--)
              uf[ufOff++] = byte1;
          } else {
            u32 backPtr = ufOff - (((byte2 & 0x0F) << 8) + byte1);
            bytesToRead = (byte2 >> 4) + 3;

            for (; bytesToRead > 0; bytesToRead--)
              uf[ufOff++] = uf[backPtr++];
          }
        }
      }
    }
  }
  if (ufOff > ufSize) {
    L_ERROR("Offset is out of bounds");
  }

  // If we had to use DEFAULT_UFSIZE because the uncompressed file size was not given (Valkyrie Profile),
  // then create a new buffer of the correct size now that we know it, and delete the old one.
  if (ufSize == DEFAULT_UFSIZE) {
    u8 *newUF = new u8[ufOff];
    memcpy(newUF, uf, ufOff);
    delete[] uf;
    uf = newUF;
  }
  // pRoot->UI_WriteBufferToFile("uncomp.raw", uf, ufOff);

  // Create the new virtual file, and analyze the sequence
  std::string name = file->tag.hasTitle() ? file->tag.title : file->stem();
  auto newVirtFile = new VirtFile(uf, ufOff, fmt::format("{} Sequence", name), file->path());

  TriAcePS1Seq *newSeq = new TriAcePS1Seq(newVirtFile, 0, name);
  bool bLoadSucceed = newSeq->loadVGMFile();

  newVirtFile->setUseLoaders(false);
  newVirtFile->setUseScanners(false);
  pRoot->loadRawFile(newVirtFile);

  if (bLoadSucceed)
    return newSeq;
  else {
    delete newSeq;
    return NULL;
  }
}
