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
	uint32_t loopType;				//can't think of it off my head, look it up in DLS specs.  I set it to 0 always
	uint8_t loopStartMeasure;
	uint8_t loopLengthMeasure;
	uint32_t loopStart;
	uint32_t loopLength;
};
