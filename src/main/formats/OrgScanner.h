/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once
#include "Scanner.h"

class OrgScanner:
    public VGMScanner {
 public:
  OrgScanner(void);
  virtual ~OrgScanner(void);

  virtual void Scan(RawFile *file, void *info = 0);
  void SearchForOrgSeq(RawFile *file);
};
