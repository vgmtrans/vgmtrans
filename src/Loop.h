#pragma once

enum LoopMeasure
{
	LM_SAMPLES,
	LM_BYTES
};


struct Loop							//Sample Block
{
	Loop()
		: loopStatus(-1), loopType(0), loopStartMeasure(LM_BYTES), loopLengthMeasure(LM_BYTES), loopStart(0), loopLength(0)
	{ }

	int loopStatus;
	ULONG loopType;				//can't think of it off my head, look it up in DLS specs.  I set it to 0 always
	BYTE loopStartMeasure;
	BYTE loopLengthMeasure;
	ULONG loopStart;
	ULONG loopLength;
};
