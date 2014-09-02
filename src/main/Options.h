#pragma once
#include "pch.h"

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
