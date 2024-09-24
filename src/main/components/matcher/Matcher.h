/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <variant>

class Format;
class RawFile;
class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;

// *******
// Matcher
// *******

class Matcher {
public:
  explicit Matcher(Format *format);
  virtual ~Matcher() = default;

  virtual void onFinishedScan(RawFile *rawfile) {}

  virtual bool onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
  virtual bool onCloseFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);

protected:
  virtual bool onNewSeq(VGMSeq *) { return false; }
  virtual bool onNewInstrSet(VGMInstrSet *) { return false; }
  virtual bool onNewSampColl(VGMSampColl *) { return false; }
  virtual bool onCloseSeq(VGMSeq *) { return false; }
  virtual bool onCloseInstrSet(VGMInstrSet *) { return false; }
  virtual bool onCloseSampColl(VGMSampColl *) { return false; }

  Format *fmt;
};
