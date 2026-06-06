/**
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */
#pragma once

#include "Scanner.h"

#include <map>
#include <memory>
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
  std::unique_ptr<VGMScanner> createScanner() override { return std::make_unique<scanner>(this); }

#define USING_MATCHER(matcher) \
  std::unique_ptr<Matcher> createMatcher() override { return std::make_unique<matcher>(this); }

#define USING_MATCHER_WITH_ARG(matcher, arg) \
  std::unique_ptr<Matcher> createMatcher() override { return std::make_unique<matcher>(this, arg); }

#define USING_COLL(coll) \
  std::unique_ptr<VGMColl> createCollection() override { return std::make_unique<coll>(); }

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
  virtual std::unique_ptr<VGMScanner> createScanner() { return nullptr; }
  VGMScanner &getScanner() const { return *scanner; }
  virtual std::unique_ptr<Matcher> createMatcher();
  virtual std::unique_ptr<VGMColl> createCollection();
  virtual bool onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
  virtual bool onCloseFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
  virtual bool onMatch(std::vector<VGMFile *> &) { return true; }
  virtual bool usesCollectionDataForSeqConversion() { return false; }

  std::unique_ptr<Matcher> matcher;
  std::unique_ptr<VGMScanner> scanner;

protected:
    static FormatMap &registry();
};
