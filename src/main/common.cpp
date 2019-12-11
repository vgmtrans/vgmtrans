/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "common.h"
#include <zlib.h>
#include <cstring>
#include <array>

std::string removeExtFromPath(const std::string &s) {
    size_t i = s.rfind('.', s.length());
    if (i != std::string::npos) {
        return (s.substr(0, i));
    }

    return s;
}

std::string StringToUpper(std::string myString) {
    const size_t length = myString.length();
    for (size_t i = 0; i != length; ++i) {
        myString[i] = toupper(myString[i]);
    }
    return myString;
}

std::string StringToLower(std::string myString) {
    const size_t length = myString.length();
    for (size_t i = 0; i != length; ++i) {
        myString[i] = tolower(myString[i]);
    }
    return myString;
}

uint32_t StringToHex(const std::string &str) {
    uint32_t value;
    std::stringstream convert(str);
    convert >> std::hex >> value;  // read seq_table as hexadecimal value
    return value;
}

std::string ConvertToSafeFileName(const std::string &str) {
    std::string filename;
    filename.reserve(str.length());

    const char *forbiddenChars = "\\/:,;*?\"<>|";
    size_t pos_begin = 0;
    size_t pos_end;
    while ((pos_end = str.find_first_of(forbiddenChars, pos_begin)) != std::string::npos) {
        filename += str.substr(pos_begin, pos_end - pos_begin);
        if (filename[filename.length() - 1] != L' ') {
            filename += " ";
        }
        pos_begin = pos_end + 1;
    }
    filename += str.substr(pos_begin);

    // right trim
    filename.erase(filename.find_last_not_of(" \n\r\t") + 1);

    return filename;
}

char *GetFileWithBase(const char *f, const char *newfile) {
    static char *ret;
    char *tp1;

#if PSS_STYLE == 1
    tp1 = ((char *)strrchr(f, '/'));
#else
    tp1 = ((char *)std::strrchr(f, '\\'));
#if PSS_STYLE != 3
    {
        char *tp3;

        tp3 = ((char *)std::strrchr(f, '/'));
        if (tp1 < tp3)
            tp1 = tp3;
    }
#endif
#endif
    if (!tp1) {
        ret = (char *)malloc(strlen(newfile) + 1);
        strcpy(ret, newfile);
    } else {
        ret = (char *)malloc((tp1 - f + 2 + strlen(newfile)) * sizeof(char));  // 1(NULL), 1(/).
        memcpy(ret, f, (tp1 - f) * sizeof(char));
        ret[tp1 - f] = L'/';
        ret[tp1 - f + 1] = 0;
        strcpy(ret, newfile);
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
