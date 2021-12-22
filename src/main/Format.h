/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once
#include <vector>
#include <map>
#include <string>

class VGMColl;
class VGMScanner;
class Matcher;
class VGMScanner;

#define DECLARE_FORMAT(_name_)               \
  _name_##Format _name_##FormatRegisterThis; \
  const std::string _name_##Format::name = #_name_;

#define BEGIN_FORMAT(_name_)                                \
  class _name_##Format : public Format {                    \
  public:                                                   \
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

typedef std::map<std::string, Format *> FormatMap;

class Format {
protected:
  static FormatMap &registry();

public:
  Format(const std::string &scannerName);
  virtual ~Format(void);

  static Format *GetFormatFromName(const std::string &name);

  virtual bool Init(void);
  virtual const std::string &GetName() = 0;
  virtual VGMScanner *NewScanner() { return NULL; }
  VGMScanner &GetScanner() { return *scanner; }
  virtual Matcher *NewMatcher() { return NULL; }
  virtual VGMColl *NewCollection();
  virtual bool OnNewFile(VGMFile *file);
  virtual bool OnCloseFile(VGMFile *file);
  virtual bool OnMatch(std::vector<VGMFile *> &files) { return true; }

public:
  Matcher *matcher;
  VGMScanner *scanner;
};
