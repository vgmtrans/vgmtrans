#pragma once
#include "Loader.h"
#include <unzip.h>

using namespace std;

class TiXmlElement;
class VirtFile;

enum LoadMethod {
	LM_APPEND,
	LM_APPEND_SWAP16,
	LM_DEINTERLACE
};

typedef struct _MAMERomGroupEntry
{
	_MAMERomGroupEntry() : file(NULL) {}
	template < class T > bool GetAttribute( const std::string& attrName, T* out )
	{
		string strValue = attributes[attrName];
		if (strValue.empty())
			return false;			//Attribute name does not exist.

		FromString(strValue, out);
		return true;
	}
	bool GetHexAttribute( const std::string& attrName, U32* out );

	LoadMethod loadmethod;
	string type;
	string encryption;
	map<const std::string, std::string> attributes;
	list<string> roms;
	VirtFile* file;
} MAMERomGroupEntry;

typedef struct _MAMEGameEntry
{
	_MAMEGameEntry() {}
	MAMERomGroupEntry* GetRomGroupOfType(const string& strType);

	string name;
	string format;
	float fmt_version;
	string fmt_version_str;
	//map<const std::string, const std::string> attributes;
	list<MAMERomGroupEntry> romgroupentries;
} MAMEGameEntry;

typedef map<string, MAMEGameEntry*> GameMap;


class MAMELoader :
	public VGMLoader
{
public:
	MAMELoader();
	~MAMELoader();
	virtual PostLoadCommand Apply(RawFile* theFile);
private:
	VirtFile* LoadRomGroup(MAMERomGroupEntry* romgroupentry, const string& format, unzFile& cur_file);
	void DeleteBuffers(list<pair<BYTE*, UINT>>& buffers);
private:
	int LoadXML();
	MAMEGameEntry* LoadGameEntry(TiXmlElement* gameElmt);
	int LoadRomGroupEntry(TiXmlElement* romgroupElmt, MAMEGameEntry* gameentry);

private:
	GameMap gamemap;
	bool bLoadedXml;
};