/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum AkaoSnesVersion : uint8_t;  // see AkaoSnesFormat.h

class AkaoSnesScanner : public VGMScanner {
 public:
  virtual void Scan(RawFile *file, void *info = 0);
  static void SearchForAkaoSnesFromARAM(RawFile *file);

 private:
  static BytePattern ptnReadNoteLengthV1;
  static BytePattern ptnReadNoteLengthV2;
  static BytePattern ptnReadNoteLengthV4;
  static BytePattern ptnVCmdExecFF4;
  static BytePattern ptnVCmdExecRS3;
  static BytePattern ptnReadSeqHeaderV1;
  static BytePattern ptnReadSeqHeaderV2;
  static BytePattern ptnReadSeqHeaderFFMQ;
  static BytePattern ptnReadSeqHeaderV4;
  static BytePattern ptnLoadDIRV1;
  static BytePattern ptnLoadDIRV3;
  static BytePattern ptnLoadInstrV1;
  static BytePattern ptnLoadInstrV2;
  static BytePattern ptnLoadInstrV3;
  static BytePattern ptnReadPercussionTableV4;
};
