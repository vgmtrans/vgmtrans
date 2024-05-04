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
      _name_##Format() : Format(#_name_) { Init(); }          \
      virtual const std::string &GetName() { return name; }

#define END_FORMAT() \
  }                  \
  ;

#define USING_SCANNER(theScanner) \
  virtual VGMScanner *NewScanner() { return new theScanner; }

#define USING_MATCHER(matcher) \
  virtual Matcher *NewMatcher() { return new matcher(this); }

#define USING_MATCHER_WITH_ARG(matcher, arg) \
  virtual Matcher *NewMatcher() { return new matcher(this, arg); }

#define USING_COLL(coll) \
  virtual VGMColl *NewCollection() { return new coll(); }

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

  static Format *GetFormatFromName(const std::string &name);

  virtual bool Init();
  virtual const std::string &GetName() = 0;
  virtual VGMScanner *NewScanner() { return nullptr; }
  VGMScanner &GetScanner() const { return *scanner; }
  virtual Matcher *NewMatcher() { return nullptr; }
  virtual VGMColl *NewCollection();
  virtual bool OnNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
  virtual bool OnCloseFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
  virtual bool OnMatch(std::vector<VGMFile *> &) { return true; }

  Matcher *matcher;
  VGMScanner *scanner;

protected:
    static FormatMap &registry();
};
