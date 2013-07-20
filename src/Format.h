#pragma once
//#include "Root.h"

using namespace std;

class VGMColl;
class VGMScanner;
class Matcher;
class VGMScanner;

#if 0
#define FORMAT_NAME(name)				\
	virtual string GetName() {return name;}
#endif

#define DECLARE_FORMAT(_name_)						\
_name_##Format _name_##FormatRegisterThis;			\
const string _name_##Format::name = #_name_;

//#define DECLARE_FORMAT(_name_)	const string _name_##Format::name = #_name_;



//static _name_##Format _className_##FormatRegisterThis;
#define BEGIN_FORMAT(_name_)				\
class _name_##Format : public Format			\
{											\
public:										\
	static const _name_##Format _name_##FormatRegisterThis;		\
	static const string name;									\
	_name_##Format() : Format(#_name_) { Init(); }				\
	virtual const string& GetName() { return name; }

#define END_FORMAT()						\
};

#define USING_SCANNER(theScanner)			\
	virtual VGMScanner* NewScanner() {return new theScanner;}

#define USING_MATCHER(matcher)				\
	virtual Matcher* NewMatcher() {return new matcher(this);}

#define USING_MATCHER_WITH_ARG(matcher, arg)				\
	virtual Matcher* NewMatcher() {return new matcher(this, arg);}

#define USING_COLL(coll)					\
	virtual VGMColl* NewCollection() {return new coll();}


/*#define SIMPLE_INIT()					\
	virtual int Init(void)					\
	{										\
		pRoot->AddScanner(NewScanner());	\
		matcher = NewMatcher(this);			\
		return true;						\
	}*/

class Format;
class VGMFile;

typedef map<string, Format*> FormatMap;

class Format
{
protected:
	static FormatMap& registry();
public:
	Format(const string& scannerName);
	virtual ~Format(void);

	static Format* GetFormatFromName(const string& name);

	virtual int Init(void);
	virtual const string& GetName() = 0;
	//virtual string GetName() = 0;
	//virtual ULONG GetFormatID() = 0;
	virtual VGMScanner* NewScanner() {return NULL;}
			VGMScanner& GetScanner() {return *scanner;}
	virtual Matcher* NewMatcher() {return NULL;}
	virtual VGMColl* NewCollection();
	virtual int OnNewFile(VGMFile* file);
	virtual int OnCloseFile(VGMFile* file);
	//virtual int OnNewSeq(VGMSeq* seq);
	//virtual int OnNewInstrSet(VGMInstrSet* instrset);
	//virtual int OnNewSampColl(VGMSampColl* sampcoll);
	virtual int OnMatch(vector<VGMFile*>& files) {return true;}

public:
	Matcher* matcher;
	VGMScanner* scanner;
};
