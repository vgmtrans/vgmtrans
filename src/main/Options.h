/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

class ConversionOptions  // static class
{
   public:
    ConversionOptions() {}
    virtual ~ConversionOptions() {}

    static void SetNumSequenceLoops(int numLoops) {
        ConversionOptions::numSequenceLoops = numLoops;
    }
    static int GetNumSequenceLoops() { return ConversionOptions::numSequenceLoops; }

   private:
    static int numSequenceLoops;
};
