
#pragma once

class SeqNoteParam
{
public:
	SeqNoteParam(int channel, int time, int length, int key, int volume) :
		channel(channel), time(time), length(length), key(key), volume(volume) {}
	virtual ~SeqNoteParam() {}

public:
	int channel;
	int time;
	int length;
	int key;
	int volume;
};
