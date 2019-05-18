/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 

#include <cassert>
#include <cwchar>
#include <cmath>
#include <algorithm>
#include <climits>
#include <stdio.h>
#include <cstdint>

#include <fstream>
#include <vector>
#include <list>
#include <map>
#include <string>
#include <cstring>
#include <sstream>
#include <unordered_set>
#include <iterator>
#include <iostream>
#include <iomanip>
#include <ctype.h>
#include "portable.h"
#define countof(arr) sizeof(arr) / sizeof(arr[0])


#include "KonamiGXScanner.h"
#include "KonamiGXSeq.h"
#include "MAMELoader.h"

void KonamiGXScanner::Scan(RawFile *file, void *info) {
  MAMEGameEntry *gameentry = (MAMEGameEntry *) info;
  MAMERomGroupEntry *seqRomGroupEntry = gameentry->GetRomGroupOfType("soundcpu");
  MAMERomGroupEntry *sampsRomGroupEntry = gameentry->GetRomGroupOfType("shared");
  if (!seqRomGroupEntry || !sampsRomGroupEntry)
    return;
  uint32_t seq_table_offset;
  //uint32_t instr_table_offset;
  //uint32_t samp_table_offset;
  if (!seqRomGroupEntry->file || !sampsRomGroupEntry->file ||
      !seqRomGroupEntry->GetHexAttribute("seq_table", &seq_table_offset))// ||
    //!seqRomGroupEntry->GetHexAttribute("samp_table", &samp_table_offset))
    return;

  LoadSeqTable(seqRomGroupEntry->file, seq_table_offset);
  return;
}


void KonamiGXScanner::LoadSeqTable(RawFile *file, uint32_t offset) {
  uint32_t nFileLength;
  nFileLength = file->size();
  while (offset < nFileLength) {
    uint32_t seqOffset = file->GetWordBE(offset);
    if (seqOffset == 0 || seqOffset >= nFileLength)
      break;
    KonamiGXSeq *newSeq = new KonamiGXSeq(file, seqOffset);
    if (!newSeq->LoadVGMFile())
      delete newSeq;
    offset += 12;
  }
}
