/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <variant>

class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;

using VGMFileVariant = std::variant<VGMSeq*, VGMInstrSet*, VGMSampColl*, VGMMiscFile*>;
