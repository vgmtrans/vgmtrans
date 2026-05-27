/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"

#include <vector>
#include <map>
#include <list>
#include <string>

template <class T>
void deleteVect(std::vector<T *> &vect) {
  for (auto p : vect) {
    delete p;
  }
  vect.clear();
}

template <class T>
void deleteList(std::list<T *> &list) {
  for (auto p : list) {
    delete p;
  }
  list.clear();
}

template <class T1, class T2>
void deleteMap(std::map<T1, T2 *> &container) {
  for (auto el : container) {
    delete el.second;
  }
  container.clear();
}

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
