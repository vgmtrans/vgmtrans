/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "common.h"
#include <zlib.h>
#include <cstring>
#include <array>

std::wstring StringToUpper(std::wstring myString) {
    const size_t length = myString.length();
    for (size_t i = 0; i != length; ++i) {
        myString[i] = toupper(myString[i]);
    }
    return myString;
}

std::wstring StringToLower(std::wstring myString) {
    const size_t length = myString.length();
    for (size_t i = 0; i != length; ++i) {
        myString[i] = tolower(myString[i]);
    }
    return myString;
}

std::string wstring2string(std::wstring &wstr) {
    char *mbs = new char[wstr.length() * MB_CUR_MAX + 1];
    wcstombs(mbs, wstr.c_str(), wstr.length() * MB_CUR_MAX + 1);
    std::string str(mbs);
    delete[] mbs;
    return str;
}

std::wstring string2wstring(std::string &str) {
    wchar_t *wcs = new wchar_t[str.length() + 1];
    mbstowcs(wcs, str.c_str(), str.length() + 1);
    std::wstring wstr(wcs);
    delete[] wcs;
    return wstr;
}

uint32_t StringToHex(const std::string &str) {
    uint32_t value;
    std::stringstream convert(str);
    convert >> std::hex >> value;  // read seq_table as hexadecimal value
    return value;
}

std::wstring ConvertToSafeFileName(const std::wstring &str) {
    std::wstring filename;
    filename.reserve(str.length());

    const wchar_t *forbiddenChars = L"\\/:,;*?\"<>|";
    size_t pos_begin = 0;
    size_t pos_end;
    while ((pos_end = str.find_first_of(forbiddenChars, pos_begin)) != std::wstring::npos) {
        filename += str.substr(pos_begin, pos_end - pos_begin);
        if (filename[filename.length() - 1] != L' ') {
            filename += L" ";
        }
        pos_begin = pos_end + 1;
    }
    filename += str.substr(pos_begin);

    // right trim
    filename.erase(filename.find_last_not_of(L" \n\r\t") + 1);

    return filename;
}

wchar_t *GetFileWithBase(const wchar_t *f, const wchar_t *newfile) {
    static wchar_t *ret;
    wchar_t *tp1;

#if PSS_STYLE == 1
    tp1 = ((wchar_t *)wcsrchr(f, '/'));
#else
    tp1 = ((wchar_t *)std::wcsrchr(f, '\\'));
#if PSS_STYLE != 3
    {
        wchar_t *tp3;

        tp3 = ((wchar_t *)std::wcsrchr(f, '/'));
        if (tp1 < tp3)
            tp1 = tp3;
    }
#endif
#endif
    if (!tp1) {
        ret = (wchar_t *)malloc(wcslen(newfile) + 1);
        wcscpy(ret, newfile);
    } else {
        ret =
            (wchar_t *)malloc((tp1 - f + 2 + wcslen(newfile)) * sizeof(wchar_t));  // 1(NULL), 1(/).
        memcpy(ret, f, (tp1 - f) * sizeof(wchar_t));
        ret[tp1 - f] = L'/';
        ret[tp1 - f + 1] = 0;
        wcscat(ret, newfile);
    }
    return (ret);
}

std::vector<char> zdecompress(std::vector<char> &src) {
    std::vector<char> result;

    /* allocate inflate state */
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    int ret = inflateInit(&strm);
    if (ret != Z_OK) {
        throw std::runtime_error("Failed to init decompression");
    }

    strm.avail_in = src.size();
    strm.next_in = reinterpret_cast<Bytef *>(src.data());

    unsigned actual_size = 0;
    constexpr int CHUNK = 16384;
    std::array<char, CHUNK> out;
    do {
        strm.avail_out = CHUNK;
        strm.next_out = reinterpret_cast<Bytef *>(out.data());
        ret = inflate(&strm, Z_NO_FLUSH);

        assert(ret != Z_STREAM_ERROR);

        switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;
                [[fallthrough]];
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                throw std::runtime_error("Decompression failed");
        }

        actual_size = CHUNK - strm.avail_out;
        result.insert(result.end(), out.begin(), out.begin() + actual_size);
    } while (strm.avail_out == 0);

    assert(ret == Z_STREAM_END);

    (void)inflateEnd(&strm);
    return result;
}
