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


#include "KonamiPS1Scanner.h"
#include "KonamiPS1Seq.h"

void KonamiPS1Scanner::Scan(RawFile *file, void *info) {
    uint32_t offset = 0;
    int numSeqFiles = 0;
    while (offset < file->size()) {
        if (KonamiPS1Seq::IsKDT1Seq(file, offset)) {
            std::wstring name = file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());
            if (numSeqFiles >= 1) {
                std::wstringstream postfix;
                postfix << L" (" << (numSeqFiles + 1) << L")";
                name += postfix.str();
            }

            KonamiPS1Seq *newSeq = new KonamiPS1Seq(file, offset, name);
            if (newSeq->LoadVGMFile()) {
                offset += newSeq->unLength;
                numSeqFiles++;
                continue;
            }
            else {
                delete newSeq;
            }
        }
        offset++;
    }
}
