/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "common.h"

#include <cstring>
#include <gsl-lite.hpp>

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
