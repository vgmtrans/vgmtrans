/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class TriAcePS1Seq;
class TriAcePS1InstrSet;

class TriAcePS1Scanner : public VGMScanner {
   public:
    TriAcePS1Scanner(void);

   public:
    ~TriAcePS1Scanner(void);

    virtual void Scan(RawFile *file, void *info = 0);
    void SearchForSLZSeq(RawFile *file);
    void SearchForInstrSet(RawFile *file, std::vector<TriAcePS1InstrSet *> &instrsets);
    TriAcePS1Seq *TriAceSLZDecompress(RawFile *file, uint32_t cfOff);
};
