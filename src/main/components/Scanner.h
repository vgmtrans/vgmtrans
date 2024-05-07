/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

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
  void InitiateScan(RawFile *file, void *offset = nullptr);
  virtual void Scan(RawFile *file, void *offset = nullptr) = 0;
};
