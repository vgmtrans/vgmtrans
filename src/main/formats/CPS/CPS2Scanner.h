#pragma once
#include "Scanner.h"

struct MAMEGame;

enum CPSFormatVer: uint8_t {
  VER_UNDEFINED,
  VER_CPS1_200,
  VER_CPS1_200ff,  //Final Fight is laballed 2.00, but has unique 3 byte seq table header
  VER_CPS1_350,
  VER_CPS1_425,
  VER_CPS1_500,
  VER_CPS1_502,
  VER_100,
  VER_101,
  VER_103,
  VER_104,
  VER_105A,
  VER_105C,
  VER_105,
  VER_106B,
  VER_115C,
  VER_115,
  VER_116B,
  VER_116,
  VER_130,
  VER_131,
  VER_140,
  VER_171,
  VER_180,
  VER_200,
  VER_201B,
  VER_210,
  VER_211,
  VER_CPS3
};

CPSFormatVer GetVersionEnum(std::string &versionStr);

class CPS2Scanner : public VGMScanner {
public:
  virtual void Scan(RawFile* file, void *info = 0);
};


