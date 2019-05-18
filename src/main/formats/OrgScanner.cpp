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


#include "OrgScanner.h"
#include "OrgSeq.h"

#define SRCH_BUF_SIZE 0x20000

OrgScanner::OrgScanner(void) {
}

OrgScanner::~OrgScanner(void) {
}


void OrgScanner::Scan(RawFile *file, void *info) {
  SearchForOrgSeq(file);
  return;
}

void OrgScanner::SearchForOrgSeq(RawFile *file) {
  uint32_t nFileLength = file->size();
  for (uint32_t i = 0; i + 6 < nFileLength; i++) {
    if ((*file)[i] == 'O' && (*file)[i + 1] == 'r' && (*file)[i + 2] == 'g' && (*file)[i + 3] == '-' &&
        (*file)[i + 4] == '0' && (*file)[i + 5] == '2') {
      if (file->GetShort(i + 6)) {
        OrgSeq *NewOrgSeq = new OrgSeq(file, i);
        if (!NewOrgSeq->LoadVGMFile())
          delete NewOrgSeq;
      }
    }
  }
}
