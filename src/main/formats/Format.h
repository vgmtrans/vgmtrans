/**
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */
#pragma once

#include "Scanner.h"

#include <map>
#include <string>
#include <variant>
#include <vector>

class VGMColl;
class Matcher;
class VGMScanner;

#define DECLARE_FORMAT(_name_)               \
  _name_##Format _name_##FormatRegisterThis; \
  const std::string _name_##Format::name = #_name_;

#define BEGIN_FORMAT(_name_)                                    \
  class _name_##Format : public Format {                      \
    public:                                                  \
      static const _name_##Format _name_##FormatRegisterThis; \
      static const std::string name;                          \
      _name_##Format() : Format(#_name_) { init(); }          \
      const std::string& getName() override { return name; }

#define END_FORMAT() \
  }                  \
  ;

#define USING_SCANNER(scanner) \
  VGMScanner* newScanner() override { return new scanner(this); }

#define USING_MATCHER(matcher) \
  Matcher* newMatcher() override { return new matcher(this); }

#define USING_MATCHER_WITH_ARG(matcher, arg) \
  Matcher* newMatcher() override { return new matcher(this, arg); }

#define USING_COLL(coll) \
  VGMColl* newCollection() override { return new coll(); }

#define USES_COLLECTION_FOR_SEQ_CONVERSION() \
  bool usesCollectionDataForSeqConversion() override { return true; }

class Format;
class VGMFile;
class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;

using FormatMap = std::map<std::string, Format *>;

class Format {
public:
  Format(const std::string &formatName);
  virtual ~Format();

  static Format *formatFromName(const std::string &name);
  static std::vector<Format*> formats();

  virtual bool init();
  virtual const std::string &getName() = 0;
  virtual VGMScanner *newScanner() { return nullptr; }
  VGMScanner &getScanner() const { return *scanner; }
  virtual Matcher *newMatcher() { return nullptr; }
  virtual VGMColl *newCollection();
  virtual bool onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
  virtual bool onCloseFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
  virtual bool onMatch(std::vector<VGMFile *> &) { return true; }
  virtual bool usesCollectionDataForSeqConversion() { return false; }

  Matcher *matcher;
  VGMScanner *scanner;

protected:
    static FormatMap &registry();
};
