/**
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */
#pragma once

#include <variant>
#include <string>
#include <map>
#include <vector>

class VGMColl;
class VGMScanner;
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
      virtual const std::string& getName() { return name; }

#define END_FORMAT() \
  }                  \
  ;

#define USING_SCANNER(theScanner) \
  virtual VGMScanner* newScanner() { return new theScanner; }

#define USING_MATCHER(matcher) \
  virtual Matcher* newMatcher() { return new matcher(this); }

#define USING_MATCHER_WITH_ARG(matcher, arg) \
  virtual Matcher* newMatcher() { return new matcher(this, arg); }

#define USING_COLL(coll) \
  virtual VGMColl* newCollection() { return new coll(); }

class Format;
class VGMFile;
class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;

using FormatMap = std::map<std::string, Format *>;

class Format {
public:
  Format(const std::string &scannerName);
  virtual ~Format();

  static Format *getFormatFromName(const std::string &name);

  virtual bool init();
  virtual const std::string &getName() = 0;
  virtual VGMScanner *newScanner() { return nullptr; }
  VGMScanner &getScanner() const { return *scanner; }
  virtual Matcher *newMatcher() { return nullptr; }
  virtual VGMColl *newCollection();
  virtual bool onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
  virtual bool onCloseFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
  virtual bool onMatch(std::vector<VGMFile *> &) { return true; }

  Matcher *matcher;
  VGMScanner *scanner;

protected:
    static FormatMap &registry();
};
