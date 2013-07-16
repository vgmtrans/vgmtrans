#include "stdafx.h"
#include "Format.h"
#include "Matcher.h"
#include "VGMColl.h"

FormatMap& Format::registry()
{
	static FormatMap registry;
	return registry;
}

Format::Format(const string& formatName) :
	scanner(NULL),
	matcher(NULL)
{
	registry().insert( make_pair(formatName, this) );
}

Format::~Format(void)
{
	if (scanner != NULL)
		delete scanner;
	if (matcher != NULL)
		delete matcher;
}

Format* Format::GetFormatFromName(const string& name)
{
	FormatMap::iterator findIt = registry().find(name);
	if (findIt == registry().end())
		return NULL;
	return (*findIt).second;
}

int Format::OnNewFile(VGMFile* file)
{
	if (!matcher)
		return false;
	return matcher->OnNewFile(file);
	//switch (file->GetFileType())
	//{
	//case FILETYPE_SEQ:
	//	return OnNewSeq((VGMSeq*)file);
	//case FILETYPE_INSTRSET:
	//	return OnNewInstrSet((VGMInstrSet*)file);
	//case FILETYPE_SAMPCOLL:
	//	return OnNewSampColl((VGMSampColl*)file);
	//}
}

VGMColl* Format::NewCollection()
{
	return new VGMColl();
}

int Format::OnCloseFile(VGMFile* file)
{
	if (!matcher)
		return false;
	return matcher->OnCloseFile(file);
}

int Format::Init(void)
{
	scanner = NewScanner();
	matcher = NewMatcher();
	return true;
}
