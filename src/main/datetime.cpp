/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "datetime.h"

const char *DateTime::monthnames[] = {NULL,  "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
const int DateTime::daysmonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
const int DateTime::daysmonthleap[] = {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
