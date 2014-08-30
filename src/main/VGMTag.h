#pragma once

class VGMTag
{
public:
	VGMTag(void);
	VGMTag(const std::wstring & _title, const std::wstring & _artist = L"", const std::wstring & _album = L"");
	virtual ~VGMTag(void);

	bool HasTitle(void);
	bool HasArtist(void);
	bool HasAlbum(void);
	bool HasComment(void);
	bool HasTrackNumber(void);
	bool HasLength(void);

public:
	std::wstring title;
	std::wstring artist;
	std::wstring album;
	std::wstring comment;
	std::map<std::wstring, std::vector<uint8_t>> binaries;

	/** Track number */
	int track_number;

	/** Length in seconds */
	double length;
};
