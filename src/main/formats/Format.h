/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <variant>

class VGMColl;
class VGMScanner;
class Matcher;
class VGMScanner;

#define DECLARE_FORMAT(_name_)                 \
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
    }                \
    ;

#define USING_SCANNER(theScanner) \
    virtual VGMScanner *NewScanner() { return new theScanner; }

#define USING_MATCHER(matcher) \
    virtual Matcher *NewMatcher() { return new matcher(this); }

#define USING_MATCHER_WITH_ARG(matcher, arg) \
    virtual Matcher *NewMatcher() { return new matcher(this, arg); }

#define USING_COLL(coll) \
    virtual VGMColl *NewCollection() { return new coll(); }

/*#define SIMPLE_INIT()					\
        virtual int Init(void)					\
        {										\
                pRoot->AddScanner(NewScanner());	\
                matcher = NewMatcher(this);			\
                return true;						\
        }*/

class Format;
class VGMFile;
class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;

typedef std::map<std::string, Format *> FormatMap;

class Format {
   protected:
    static FormatMap &registry();

   public:
    Format(const std::string &scannerName);
    virtual ~Format();

    static Format *GetFormatFromName(const std::string &name);

    virtual bool Init();
    virtual const std::string &GetName() = 0;
    // virtual string GetName() = 0;
    // virtual uint32_t GetFormatID() = 0;
    virtual VGMScanner *NewScanner() { return nullptr; }
    VGMScanner &GetScanner() const { return *scanner; }
    virtual Matcher *NewMatcher() { return nullptr; }
    virtual VGMColl *NewCollection();
    virtual bool OnNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
    virtual bool OnCloseFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file);
    // virtual int OnNewSeq(VGMSeq* seq);
    // virtual int OnNewInstrSet(VGMInstrSet* instrset);
    // virtual int OnNewSampColl(VGMSampColl* sampcoll);
    virtual bool OnMatch(std::vector<VGMFile *> &) { return true; }

   public:
    Matcher *matcher;
    VGMScanner *scanner;
};
