/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #pragma once


#include <cassert>
#include <cwchar>
#include <cmath>
#include <algorithm>
#include <climits>
#include <stdio.h>
#include <cstdint>

#include <fstream>
#include <vector>
#include <list>
#include <map>
#include <string>
#include <cstring>
#include <sstream>
#include <unordered_set>
#include <iterator>
#include <iostream>
#include <iomanip>
#include <ctype.h>
#include "portable.h"
#define countof(arr) sizeof(arr) / sizeof(arr[0])



class ConversionOptions // static class
{
 public:
  ConversionOptions() { }
  virtual ~ConversionOptions() { }

  static void SetNumSequenceLoops(int numLoops) { ConversionOptions::numSequenceLoops = numLoops; }
  static int GetNumSequenceLoops() { return ConversionOptions::numSequenceLoops; }

 private:
  static int numSequenceLoops;
};
