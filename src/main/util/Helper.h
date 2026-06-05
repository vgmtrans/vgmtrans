/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"

#include <string>
#include <vector>

template <class T>
inline void pushTypeOnVect(std::vector<u8> &theVector, T unit) {
  theVector.insert(theVector.end(), reinterpret_cast<u8 *>(&unit),
                   reinterpret_cast<u8 *>(&unit) + sizeof(T));
}

template <class T>
inline void pushTypeOnVectBE(std::vector<u8> &theVector, T unit) {
  for (u32 i = 0; i < sizeof(T); i++) {
    theVector.push_back(*(reinterpret_cast<u8 *>(&unit) - i + sizeof(T) - 1));
  }
}

inline void pushBackStringOnVector(std::vector<u8> &theVector, std::string &str) {
  theVector.insert(theVector.end(), str.data(), str.data() + str.length());
}
