#pragma once
#include "stdafx.h"

class ConversionOptions // static class
{
public:
	ConversionOptions() { numSequenceLoops = 2; }

	static void SetNumSequenceLoops(int numLoops) { ConversionOptions::numSequenceLoops = numLoops; }
	static int GetNumSequenceLoops() { return 1;/*ConversionOptions::numSequenceLoops;*/ }

private:
	static int numSequenceLoops;
};