/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class OrgScanner : public VGMScanner {
 public:
  OrgScanner();
  virtual ~OrgScanner();

  virtual void scan(RawFile *file, void *info = 0);
  void searchForOrgSeq(RawFile *file);
};
