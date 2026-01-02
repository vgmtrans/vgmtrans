/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include <vector>
#include <map>
#include <list>
#include <string>
#include <filesystem>

inline std::filesystem::path pathFromUtf8(const std::string& path) {
  return { std::u8string(reinterpret_cast<const char8_t*>(path.c_str())) };
}

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
inline void pushTypeOnVect(std::vector<uint8_t> &theVector, T unit) {
  theVector.insert(theVector.end(), reinterpret_cast<uint8_t *>(&unit),
                   reinterpret_cast<uint8_t *>(&unit) + sizeof(T));
}

template <class T>
inline void pushTypeOnVectBE(std::vector<uint8_t> &theVector, T unit) {
  for (uint32_t i = 0; i < sizeof(T); i++) {
    theVector.push_back(*(reinterpret_cast<uint8_t *>(&unit) - i + sizeof(T) - 1));
  }
}

inline void pushBackStringOnVector(std::vector<uint8_t> &theVector, std::string &str) {
  theVector.insert(theVector.end(), str.data(), str.data() + str.length());
}
