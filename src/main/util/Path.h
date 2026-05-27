/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

std::filesystem::path makeSafeFileName(std::string_view s);
std::string pathToUtf8String(const std::filesystem::path& path);
