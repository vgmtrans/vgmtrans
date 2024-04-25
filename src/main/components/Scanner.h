#pragma once
#include "ExtensionDiscriminator.h"

class RawFile;

#define USE_EXTENSION(extension)            \
    ExtensionDiscriminator::instance().AddExtensionScannerAssoc(extension, this);
//	virtual bool UseExtension() {return theExtensionDiscriminator.AddExtensionScannerAssoc(extension, this);}

class VGMScanner {
 public:
  VGMScanner(void);
 public:
  virtual ~VGMScanner(void);

  virtual bool Init();
  //virtual bool UseExtension() {return true;}
  void InitiateScan(RawFile *file, void *offset = 0);
  virtual void Scan(RawFile *file, void *offset = 0) = 0;
};
