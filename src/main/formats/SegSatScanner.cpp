/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 





#include "SegSatSeq.h"

#define SRCH_BUF_SIZE 0x20000

SegSatScanner::SegSatScanner(void) {
}

SegSatScanner::~SegSatScanner(void) {
}


void SegSatScanner::Scan(RawFile *file, void *info) {
  uint32_t nFileLength;
  uint8_t *buf;
  uint32_t j;
  uint32_t progPos = 0;

  buf = new uint8_t[SRCH_BUF_SIZE];
  nFileLength = file->size();
  file->GetBytes(0, SRCH_BUF_SIZE, buf);
  j = 0;
  for (uint32_t i = 0; i + 4 < nFileLength; i++) {
    if (j + 4 > SRCH_BUF_SIZE) {
      memcpy(buf, buf + j, 3);
      file->GetBytes(i + 3, j, buf + 3);
      j = 0;
    }
    if (buf[j] == 0x01 && buf[j + 1] == 0 && buf[j + 2] == 0 && buf[j + 3] == 0 && buf[j + 4] == 6
        && (buf[j + 5] & 0xF0) == 0/* && buf[j+6] == 0x30*/) {

      SegSatSeq *newSegSatSeq = new SegSatSeq(file, i + 5);//this, pDoc, pDoc->GetWord(i+24 + k*8)-0x8000000);

      if (!newSegSatSeq->LoadVGMFile())
        delete newSegSatSeq;
    }
    j++;
  }

  delete[] buf;
  return;
}