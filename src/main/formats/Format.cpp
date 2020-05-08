/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <vector>
#include <map>
#include "Scanner.h"
#include "Matcher.h"

FormatMap &Format::registry() {
    static FormatMap registry;
    return registry;
}

Format::Format(const std::string &formatName) : scanner(nullptr), matcher(nullptr) {
    registry().insert(make_pair(formatName, this));
}

Format::~Format() {
    delete scanner;
    delete matcher;
}

Format *Format::GetFormatFromName(const std::string &name) {
    auto findIt = registry().find(name);
    if (findIt == registry().end())
        return nullptr;
    return (*findIt).second;
}

bool Format::OnNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
    if (!matcher) {
        return false;
    }
    return matcher->OnNewFile(file);
}

VGMColl *Format::NewCollection() {
    return new VGMColl();
}

bool Format::OnCloseFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
    if (!matcher) {
        return false;
    }
    return matcher->OnCloseFile(file);
}

bool Format::Init() {
    scanner = NewScanner();
    matcher = NewMatcher();
    return true;
}
