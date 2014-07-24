#include "stdafx.h"
#include "SPCLoader.h"
#include "Root.h"

SPCLoader::SPCLoader(void)
{
}

SPCLoader::~SPCLoader(void)
{
}

PostLoadCommand SPCLoader::Apply(RawFile* file)
{
	if (file->size() < 0x10180) {
		return KEEP_IT;
	}

	char signature[34] = { 0 };
	file->GetBytes(0, 33, signature);
	if (memcmp(signature, "SNES-SPC700 Sound File Data", 27) != 0 || file->GetShort(0x21) != 0x1a1a) {
		return KEEP_IT;
	}

	uint8_t * spcData = new uint8_t[0x10000];
	memcpy(spcData, file->buf.data + 0x100, 0x10000);

	VirtFile * spcFile = new VirtFile(spcData, 0x10000, file->GetFileName());

	std::vector<uint8_t> dsp(file->buf.data + 0x10100, file->buf.data + 0x10100 + 0x80);
	spcFile->tag.binaries[L"dsp"] = dsp;

	// Parse [ID666](http://vspcplay.raphnet.net/spc_file_format.txt) if available.
	if (file->GetByte(0x23) == 0x1a) {
		char s[256];

		file->GetBytes(0x2e, 32, s);
		s[32] = '\0';
		spcFile->tag.title = string2wstring(std::string(s));

		file->GetBytes(0x4e, 32, s);
		s[32] = '\0';
		spcFile->tag.album = string2wstring(std::string(s));

		file->GetBytes(0x7e, 32, s);
		s[32] = '\0';
		spcFile->tag.comment = string2wstring(std::string(s));

		if (file->GetByte(0xd2) < 0x30) {
			// binary format
			file->GetBytes(0xb0, 32, s);
			s[32] = '\0';
			spcFile->tag.artist = string2wstring(std::string(s));

			spcFile->tag.length = (double) (file->GetWord(0xa9) & 0xffffff);
		}
		else {
			// text format
			file->GetBytes(0xb1, 32, s);
			s[32] = '\0';
			spcFile->tag.artist = string2wstring(std::string(s));

			file->GetBytes(0xa9, 3, s);
			s[3] = '\0';
			spcFile->tag.length = strtoul(s, NULL, 10);
		}
	}

	// TODO: Extended ID666 support

	// Load SPC after parsing tag
	if (!pRoot->SetupNewRawFile(spcFile)) {
		delete spcFile;
		return KEEP_IT;
	}

	return DELETE_IT;
}
