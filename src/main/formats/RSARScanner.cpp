#include "pch.h"
#include "common.h"
#include "RawFile.h"

#include "RSARFormat.h"
#include "RSARScanner.h"

#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "VGMSeq.h"
#include "SeqTrack.h"

#include <locale>
#include <codecvt>
#include <string>
#include <stack>

DECLARE_FORMAT(RSAR);

/* Utility */

static std::wstring string_to_wstring(std::string str) {
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	std::wstring wide = converter.from_bytes(str);
	return wide;
}

struct FileRange {
	uint32_t offset;
	uint32_t size;
};

static bool MatchMagic(RawFile *file, uint32_t offs, const char *magic) {
	char buf[16];
	assert(sizeof(buf) >= strlen(magic));
	file->GetBytes(offs, strlen(magic), buf);
	return memcmp(buf, magic, strlen(magic)) == 0;
}

static DataSeg * LoadRange(RawFile *file, FileRange range) {
	uint8_t *data = new uint8_t[range.size];
	file->GetBytes(range.offset, range.size, data);
	DataSeg *seg = new DataSeg();
	/* DataSeg.load takes ownership of its input buffer. */
	seg->load(data, 0, range.size);
	return seg;
}

static FileRange CheckBlock(RawFile *file, uint32_t offs, const char *magic) {
	if (!MatchMagic(file, offs, magic))
		return{ 0, 0 };

	uint32_t size = file->GetWordBE(offs + 0x04);
	return { offs + 0x08, size - 0x08 };
}

static DataSeg * LoadBlock(RawFile *file, uint32_t offs, const char *magic) {
	if (!MatchMagic(file, offs, magic))
		return nullptr;
	uint32_t blockSize = file->GetWordBE(offs + 0x04);
	FileRange range = { offs + 0x08, blockSize - 0x08 };
	return LoadRange(file, range);
}

/* RSEQ */

/* Seems to be very similar, if not the exact same, as the MML in SSEQ and JaiSeq.
 * Perhaps a cleanup is in order? */

/* From https://github.com/libertyernie/brawltools/blob/master/BrawlLib/SSBB/Types/Audio/RSEQ.cs#L124 */
enum Mml
{
	MML_WAIT = 0x80,
	MML_PRG = 0x81,

	MML_OPEN_TRACK = 0x88,
	MML_JUMP = 0x89,
	MML_CALL = 0x8a,

	MML_RANDOM = 0xa0,
	MML_VARIABLE = 0xa1,
	MML_IF = 0xa2,
	MML_TIME = 0xa3,
	MML_TIME_RANDOM = 0xa4,
	MML_TIME_VARIABLE = 0xa5,

	MML_TIMEBASE = 0xb0,
	MML_ENV_HOLD = 0xb1,
	MML_MONOPHONIC = 0xb2,
	MML_VELOCITY_RANGE = 0xb3,
	MML_PAN = 0xc0,
	MML_VOLUME = 0xc1,
	MML_MAIN_VOLUME = 0xc2,
	MML_TRANSPOSE = 0xc3,
	MML_PITCH_BEND = 0xc4,
	MML_BEND_RANGE = 0xc5,
	MML_PRIO = 0xc6,
	MML_NOTE_WAIT = 0xc7,
	MML_TIE = 0xc8,
	MML_PORTA = 0xc9,
	MML_MOD_DEPTH = 0xca,
	MML_MOD_SPEED = 0xcb,
	MML_MOD_TYPE = 0xcc,
	MML_MOD_RANGE = 0xcd,
	MML_PORTA_SW = 0xce,
	MML_PORTA_TIME = 0xcf,
	MML_ATTACK = 0xd0,
	MML_DECAY = 0xd1,
	MML_SUSTAIN = 0xd2,
	MML_RELEASE = 0xd3,
	MML_LOOP_START = 0xd4,
	MML_VOLUME2 = 0xd5,
	MML_PRINTVAR = 0xd6,
	MML_SURROUND_PAN = 0xd7,
	MML_LPF_CUTOFF = 0xd8,
	MML_FXSEND_A = 0xd9,
	MML_FXSEND_B = 0xda,
	MML_MAINSEND = 0xdb,
	MML_INIT_PAN = 0xdc,
	MML_MUTE = 0xdd,
	MML_FXSEND_C = 0xde,
	MML_DAMPER = 0xdf,

	MML_MOD_DELAY = 0xe0,
	MML_TEMPO = 0xe1,
	MML_SWEEP_PITCH = 0xe3,

	MML_EX_COMMAND = 0xf0,

	MML_LOOP_END = 0xfc,
	MML_RET = 0xfd,
	MML_ALLOC_TRACK = 0xfe,
	MML_FIN = 0xff,
};

class RSARTrack : public SeqTrack {
public:
	RSARTrack(VGMSeq *parentFile, uint32_t offset = 0, uint32_t length = 0) :
		SeqTrack(parentFile, offset, length) {}

private:
	bool noteWait;
	struct StackFrame {
		uint32_t retOffset;
		uint32_t loopCount;
	};
	std::stack<StackFrame> callStack;

	uint32_t Read24BE(uint32_t &offset) {
		uint32_t val = (GetWordBE(offset - 1) & 0x00FFFFFF);
		offset += 3;
		return val;
	}

	virtual bool ReadEvent() override {
		uint32_t beginOffset = curOffset;
		uint8_t status_byte = GetByte(curOffset++);

		if (status_byte < 0x80) {
			/* Note on. */
			uint8_t vel = GetByte(curOffset++);
			dur = ReadVarLen(curOffset);
			AddNoteByDur(beginOffset, curOffset - beginOffset, status_byte, vel, dur);
			if (noteWait) {
				AddTime(dur);
			}
		}
		else {
			switch (status_byte) {
			case MML_WAIT:
				dur = ReadVarLen(curOffset);
				AddRest(beginOffset, curOffset - beginOffset, dur);
				break;
			case MML_CALL: {
				uint32_t destOffset = parentSeq->dwOffset + Read24BE(curOffset);
				AddGenericEvent(beginOffset, 4, L"Call", L"", CLR_LOOP);
				callStack.push({ curOffset, 0 });
				curOffset = destOffset;
				break;
			}
			case MML_JUMP: {
				uint32_t destOffset = parentSeq->dwOffset + Read24BE(curOffset);
				if (IsOffsetUsed(destOffset)) {
					/* This isn't going to go anywhere... just quit early. */
					return AddLoopForever(beginOffset, 4);
				}
				else {
					AddGenericEvent(beginOffset, 4, L"Jump", L"", CLR_LOOPFOREVER);
				}
				curOffset = destOffset;
				break;
			}
			case MML_PRG: {
				uint8_t newProg = (uint8_t)ReadVarLen(curOffset);
				AddProgramChange(beginOffset, curOffset - beginOffset, newProg);
				break;
			}
			case MML_OPEN_TRACK:
				curOffset += 4;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Open Track");
				break;
			case MML_PAN: {
				uint8_t pan = GetByte(curOffset++);
				AddPan(beginOffset, curOffset - beginOffset, pan);
				break;
			}
			case MML_VOLUME:
				vol = GetByte(curOffset++);
				AddVol(beginOffset, curOffset - beginOffset, vol);
				break;
			case MML_MAIN_VOLUME: {
				uint8_t mvol = GetByte(curOffset++);
				AddUnknown(beginOffset, curOffset - beginOffset, L"Master Volume");
				break;
			}
			case MML_TRANSPOSE: {
				int8_t transpose = (signed)GetByte(curOffset++);
				AddTranspose(beginOffset, curOffset - beginOffset, transpose);
				break;
			}
			case MML_PITCH_BEND: {
				int16_t bend = (signed)GetByte(curOffset++) * 64;
				AddPitchBend(beginOffset, curOffset - beginOffset, bend);
				break;
			}
			case MML_BEND_RANGE: {
				uint8_t semitones = GetByte(curOffset++);
				AddPitchBendRange(beginOffset, curOffset - beginOffset, semitones);
				break;
			}
			case MML_PRIO:
				curOffset++;
				AddGenericEvent(beginOffset, curOffset - beginOffset, L"Priority", L"", CLR_CHANGESTATE);
				break;
			case MML_NOTE_WAIT: {
				noteWait = !!GetByte(curOffset++);
				AddUnknown(beginOffset, curOffset - beginOffset, L"Notewait Mode");
				break;
			}
			case MML_TIE:
				curOffset++;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Tie");
				break;
			case MML_PORTA:
				curOffset++;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Portamento Control");
				break;
			case MML_MOD_DEPTH: {
				uint8_t amount = GetByte(curOffset++);
				AddModulation(beginOffset, curOffset - beginOffset, amount, L"Modulation Depth");
				break;
			}
			case MML_MOD_SPEED:
				curOffset++;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Modulation Speed");
				break;
			case MML_MOD_TYPE:
				curOffset++;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Modulation Type");
				break;
			case MML_MOD_RANGE:
				curOffset++;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Modulation Range");
				break;
			case MML_PORTA_SW: {
				bool bPortOn = (GetByte(curOffset++) != 0);
				AddPortamento(beginOffset, curOffset - beginOffset, bPortOn);
				break;
			}
			case MML_PORTA_TIME: {
				uint8_t portTime = GetByte(curOffset++);
				AddPortamentoTime(beginOffset, curOffset - beginOffset, portTime);
				break;
			}
			case MML_ATTACK:
				curOffset++;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Attack Rate");
				break;
			case MML_DECAY:
				curOffset++;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Decay Rate");
				break;
			case MML_SUSTAIN:
				curOffset++;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Sustain Level");
				break;
			case MML_RELEASE:
				curOffset++;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Release Rate");
				break;
			case MML_LOOP_START: {
				uint8_t loopCount = GetByte(curOffset++);
				callStack.push({ curOffset, loopCount });
				AddUnknown(beginOffset, curOffset - beginOffset, L"Loop Start");
				break;
			}
			case MML_VOLUME2: {
				uint8_t expression = GetByte(curOffset++);
				AddExpression(beginOffset, curOffset - beginOffset, expression);
				break;
			}
			case MML_PRINTVAR:
				curOffset++;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Print Variable");
				break;
			case MML_MOD_DELAY:
				curOffset += 2;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Modulation Delay");
				break;
			case MML_TEMPO: {
				uint16_t bpm = GetShortBE(curOffset);
				curOffset += 2;
				AddTempoBPM(beginOffset, curOffset - beginOffset, bpm);
				break;
			}
			case MML_SWEEP_PITCH:
				curOffset += 2;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Sweep Pitch");
				break;
			case MML_FXSEND_A:
			case MML_FXSEND_B:
			case MML_FXSEND_C:
				curOffset++;
				AddUnknown(beginOffset, curOffset - beginOffset, L"FXSEND");
				break;
			case MML_LOOP_END:
				/* XXX: Not sure why this happens sometimes... */
				if (callStack.size() == 0) break;
				/* Jump only if we should loop more... */
				if (callStack.top().loopCount-- == 0)
					break;
				/* Fall through. */
			case MML_RET:
				AddGenericEvent(beginOffset, curOffset - beginOffset, L"Return", L"", CLR_LOOP);
				/* XXX: Not sure why this happens sometimes... */
				if (callStack.size() == 0) break;
				curOffset = callStack.top().retOffset;
				callStack.pop();
				break;

			/* Handled for the master track in RSARSeq. */
			case MML_ALLOC_TRACK:
				curOffset += 2;
				AddUnknown(beginOffset, curOffset - beginOffset, L"Allocate Track");
				break;
			case MML_FIN:
				AddEndOfTrack(beginOffset, curOffset - beginOffset);
				return false;

			default:
				AddUnknown(beginOffset, curOffset - beginOffset);
				return false;
			}
		}
		return true;
	}
};

class RSARSeq : public VGMSeq {
public:
	RSARSeq(RawFile *file, uint32_t offset, uint32_t pMasterTrackOffset, uint32_t length, std::wstring name, uint32_t pAllocTrack) :
		VGMSeq(RSARFormat::name, file, offset, length, name), masterTrackOffset(pMasterTrackOffset), allocTrack(pAllocTrack) {}

private:
	uint32_t allocTrack;
	uint32_t masterTrackOffset;

	virtual bool GetHeaderInfo() override {
		SetPPQN(0x30);
		return true;
	}

	virtual bool GetTrackPointers() override {
		uint32_t offset = dwOffset + masterTrackOffset;
		aTracks.push_back(new RSARTrack(this, offset, 0));

		/* Scan through the master track for the OPEN_TRACK commands. */
		while (GetByte(offset) == MML_OPEN_TRACK) {
			AddHeader(offset, 5, L"Opening Track");
			uint8_t trackNo = GetByte(offset + 1);
			uint32_t trackOffs = GetWordBE(offset + 1) & 0x00FFFFFF;
			aTracks.push_back(new RSARTrack(this, dwOffset + trackOffs, 0));
			offset += 0x05;
		}

		return true;
	}
};

namespace RSEQ {
	static VGMSeq * Parse(RawFile *file, std::wstring name, uint32_t rseqOffset, uint32_t dataOffset, uint32_t allocTrack) {
		if (!MatchMagic(file, rseqOffset, "RSEQ\xFE\xFF\x01"))
			return nullptr;

		uint32_t version = file->GetByte(rseqOffset + 0x07);
		uint32_t dataBlockBase = rseqOffset + file->GetWordBE(rseqOffset + 0x10);

		FileRange dataBlock = CheckBlock(file, dataBlockBase, "DATA");
		uint32_t seqBase = dataBlockBase + file->GetWordBE(dataBlock.offset + 0x00);
		uint32_t seqOffset = dataOffset;

		return new RSARSeq(file, seqBase, seqOffset, 0, name, allocTrack);
	}
}

/* RBNK */

static const double INTR_FREQUENCY = 1.0 / 192.0;

class RBNKInstr : public VGMInstr {
public:
	RBNKInstr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theInstrNum, uint32_t theBank = 0)
		: VGMInstr(instrSet, offset, length, theBank, theInstrNum) {}

private:
	enum RegionSet { DIRECT = 1, RANGE = 2, INDEX = 3, NONE = 4 };

	struct Region {
		uint32_t low;
		uint32_t subRefOffs;
	};

	virtual bool LoadInstr() override {
		std::vector<Region> keyRegions = EnumerateRegionTable(dwOffset);

		for (uint32_t i = 0; i < keyRegions.size(); i++) {
			Region keyRegion = keyRegions[i];
			uint8_t keyLow = keyRegion.low;
			uint8_t keyHigh = (i + 1 < keyRegions.size()) ? keyRegions[i+1].low : 0x7F;

			std::vector<Region> velRegions = EnumerateRegionTable(keyRegion.subRefOffs);
			for (uint32_t j = 0; j < velRegions.size(); j++) {
				Region velRegion = velRegions[j];
				uint8_t velLow = velRegion.low;
				uint8_t velHigh = (j + 1 < velRegions.size()) ? velRegions[j + 1].low : 0x7F;

				uint32_t offs = parInstrSet->dwOffset + GetWordBE(velRegion.subRefOffs + 0x04);
				VGMRgn *rgn = AddRgn(offs, 0, 0, keyLow, keyHigh, velLow, velHigh);
				LoadRgn(rgn);
			}
		}

		return true;
	}

	std::vector<Region> EnumerateRegionTable(uint32_t refOffset) {
		RegionSet regionSet = (RegionSet)GetByte(refOffset + 0x01);

		/* First, enumerate based on key. */
		switch (regionSet) {
		case RegionSet::DIRECT:
			return { { 0, refOffset } };
		case RegionSet::RANGE: {
			uint32_t offs = parInstrSet->dwOffset + GetWordBE(refOffset + 0x04);
			uint8_t tableSize = GetByte(offs);
			uint32_t tableEnd = (offs + 1 + tableSize + 3) & ~3;

			std::vector<Region> table;
			for (uint8_t i = 0; i < tableSize; i++) {
				uint32_t low = GetByte(offs + 1 + i);
				uint32_t subRefOffs = tableEnd + (i * 8);
				Region region = { low, subRefOffs };
				table.push_back(region);
			}
			return table;
		}
		case RegionSet::INDEX:
			/* TODO: Implement INDEX region sets. */
			return {};
		case RegionSet::NONE:
			return {};
		default:
			assert(false && "Unsupported region");
			return {};
		}
	}

	void LoadRgn(VGMRgn *rgn) {
		uint32_t rgnBase = rgn->dwOffset;
		uint32_t waveIndex = GetWordBE(rgnBase + 0x00);
		rgn->SetSampNum(waveIndex);

		uint8_t a = GetByte(rgnBase + 0x04);
		uint8_t d = GetByte(rgnBase + 0x05);
		uint8_t s = GetByte(rgnBase + 0x06);
		uint8_t r = GetByte(rgnBase + 0x07);

		/* XXX: This doesn't seem to work. Figure this out later. */
		/* SetupEnvelope(rgn, a, d, s, r); */

		/* hold */
		uint8_t waveDataLocationType = GetByte(rgnBase + 0x09);
		assert(waveDataLocationType == 0x00 && "We only support INDEX wave data for now.");

		/* noteOffType */
		/* alternateAssign */
		rgn->SetUnityKey(GetByte(rgnBase + 0x0C));

		/* XXX: How do I transcribe volume? */
		/* rgn->SetVolume(GetByte(rgnBase + 0x0D)); */

		rgn->SetPan(GetByte(rgnBase + 0x0E));
		/* padding */
		/* f32 tune */
		/* lfo table */
		/* graph env table */
		/* randomizer table */
	}

	static uint16_t GetFallingRate(uint8_t DecayTime) {
		uint32_t realDecay;
		if (DecayTime == 0x7F)
			realDecay = 0xFFFF;
		else if (DecayTime == 0x7E)
			realDecay = 0x3C00;
		else if (DecayTime < 0x32) {
			realDecay = DecayTime * 2;
			++realDecay;
			realDecay &= 0xFFFF;
		}
		else {
			realDecay = 0x1E00;
			DecayTime = 0x7E - DecayTime;
			realDecay /= DecayTime;     //there is a whole subroutine that seems to resolve simply to this.  I have tested all cases
			realDecay &= 0xFFFF;
		}
		return (uint16_t)realDecay;
	}

	/* Stolen from NDSInstrSet.cpp since the format is similar. */
	static void SetupEnvelope(VGMRgn *rgn, uint8_t AttackTime, uint8_t ReleaseTime, uint8_t SustainLev, uint8_t DecayTime) {
		short realDecay;
		short realRelease;
		uint8_t realAttack;
		long realSustainLev;
		const uint8_t AttackTimeTable[] = { 0x00, 0x01, 0x05, 0x0E, 0x1A, 0x26,
			0x33, 0x3F, 0x49, 0x54, 0x5C, 0x64, 0x6D, 0x74, 0x7B, 0x7F, 0x84,
			0x89, 0x8F };

		const uint16_t sustainLevTable[] =
		{ 0xFD2D, 0xFD2E, 0xFD2F, 0xFD75, 0xFDA7, 0xFDCE, 0xFDEE, 0xFE09, 0xFE20, 0xFE34, 0xFE46, 0xFE57, 0xFE66, 0xFE74,
			0xFE81, 0xFE8D, 0xFE98, 0xFEA3, 0xFEAD, 0xFEB6, 0xFEBF, 0xFEC7, 0xFECF, 0xFED7, 0xFEDF, 0xFEE6, 0xFEEC, 0xFEF3,
			0xFEF9, 0xFEFF, 0xFF05, 0xFF0B, 0xFF11, 0xFF16, 0xFF1B, 0xFF20, 0xFF25, 0xFF2A, 0xFF2E, 0xFF33, 0xFF37, 0xFF3C,
			0xFF40, 0xFF44, 0xFF48, 0xFF4C, 0xFF50, 0xFF53, 0xFF57, 0xFF5B, 0xFF5E, 0xFF62, 0xFF65, 0xFF68, 0xFF6B, 0xFF6F,
			0xFF72, 0xFF75, 0xFF78, 0xFF7B, 0xFF7E, 0xFF81, 0xFF83, 0xFF86, 0xFF89, 0xFF8C, 0xFF8E, 0xFF91, 0xFF93, 0xFF96,
			0xFF99, 0xFF9B, 0xFF9D, 0xFFA0, 0xFFA2, 0xFFA5, 0xFFA7, 0xFFA9, 0xFFAB, 0xFFAE, 0xFFB0, 0xFFB2, 0xFFB4, 0xFFB6,
			0xFFB8, 0xFFBA, 0xFFBC, 0xFFBE, 0xFFC0, 0xFFC2, 0xFFC4, 0xFFC6, 0xFFC8, 0xFFCA, 0xFFCC, 0xFFCE, 0xFFCF, 0xFFD1,
			0xFFD3, 0xFFD5, 0xFFD6, 0xFFD8, 0xFFDA, 0xFFDC, 0xFFDD, 0xFFDF, 0xFFE1, 0xFFE2, 0xFFE4, 0xFFE5, 0xFFE7, 0xFFE9,
			0xFFEA, 0xFFEC, 0xFFED, 0xFFEF, 0xFFF0, 0xFFF2, 0xFFF3, 0xFFF5, 0xFFF6, 0xFFF8, 0xFFF9, 0xFFFA, 0xFFFC, 0xFFFD,
			0xFFFF, 0x0000 };

		if (AttackTime >= 0x6D)
			realAttack = AttackTimeTable[0x7F - AttackTime];
		else
			realAttack = 0xFF - AttackTime;

		realDecay = GetFallingRate(DecayTime);
		realRelease = GetFallingRate(ReleaseTime);

		int count = 0;
		for (long i = 0x16980; i != 0; i = (i * realAttack) >> 8)
			count++;
		rgn->attack_time = count * INTR_FREQUENCY;

		if (SustainLev == 0x7F)
			realSustainLev = 0;
		else
			realSustainLev = (0x10000 - sustainLevTable[SustainLev]) << 7;
		if (DecayTime == 0x7F)
			rgn->decay_time = 0.001;
		else {
			count = 0x16980 / realDecay;//realSustainLev / realDecay;
			rgn->decay_time = count * INTR_FREQUENCY;
		}

		if (realSustainLev == 0)
			rgn->sustain_level = 1.0;
		else
			//rgn->sustain_level = 20 * log10 ((92544.0-realSustainLev) / 92544.0);
			rgn->sustain_level = (double)(0x16980 - realSustainLev) / (double)0x16980;

		//we express release rate as time from maximum volume, not sustain level
		count = 0x16980 / realRelease;
		rgn->release_time = count * INTR_FREQUENCY;
	}
};

class RBNKSamp : public VGMSamp {
public:
	RBNKSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset, uint32_t dataLength) :
		VGMSamp(sampColl, offset, length, dataOffset, dataLength) {
		Load();
	}

private:
	enum Format { PCM8 = 0, PCM16 = 1, ADPCM = 2 } format;

	struct AdpcmParam {
		uint16_t coef[16];
		uint16_t gain;
		uint16_t expectedScale;
		uint16_t yn1;
		uint16_t yn2;
	};

	struct ChannelParam {
		AdpcmParam adpcmParam;
		uint32_t dataOffset;
	};

	static const int MAX_CHANNELS = 2;
	ChannelParam channelParams[MAX_CHANNELS];

	uint32_t DspAddressToSamples(uint32_t dspAddr) {
		switch (format) {
		case Format::ADPCM:
			return (dspAddr / 16) * 14 + (dspAddr % 16) - 2;
		case Format::PCM16:
		case Format::PCM8:
		default:
			return dspAddr;
		}
	}

	void ReadAdpcmParam(uint32_t adpcmParamBase, AdpcmParam *param) {
		for (int i = 0; i < 16; i++)
			param->coef[i] = GetShortBE(adpcmParamBase + i * 0x02);
		param->gain = GetShortBE(adpcmParamBase + 0x20);
		param->expectedScale = GetShortBE(adpcmParamBase + 0x22);
		param->yn1 = GetShortBE(adpcmParamBase + 0x24);
		param->yn2 = GetShortBE(adpcmParamBase + 0x26);

		/* Make sure that these are sane. */
		assert(param->gain == 0x00);
		assert(param->yn2 == 0x00);
		assert(param->yn1 == 0x00);
	}

	void Load() {
		format = (Format) GetByte(dwOffset + 0x00);

		switch (format) {
		case Format::PCM8:
			SetWaveType(WT_PCM8);
			SetBPS(8);
			break;
		case Format::PCM16:
		case Format::ADPCM:
			SetWaveType(WT_PCM16);
			SetBPS(16);
		}

		bool loop = !!GetByte(dwOffset + 0x01);

		SetNumChannels(GetByte(dwOffset + 0x02));
		assert((channels <= MAX_CHANNELS) && "We only support up to two channels.");

		uint8_t sampleRateH = GetByte(dwOffset + 0x03);
		uint16_t sampleRateL = GetShortBE(dwOffset + 0x04);
		SetRate((sampleRateH << 16) | sampleRateL);

		uint8_t dataLocationType = GetByte(dwOffset + 0x06);
		assert((dataLocationType == 0x00) && "We only support OFFSET-based wave data locations.");
		/* padding */

		uint32_t loopStart = GetWordBE(dwOffset + 0x08);
		uint32_t loopEnd = GetWordBE(dwOffset + 0x0C);

		SetLoopStatus(loop);
		if (loop) {
			SetLoopStartMeasure(LM_SAMPLES);
			SetLoopLengthMeasure(LM_SAMPLES);
			/* XXX: Not sure about these exactly but it makes Wii Shop sound correct.
			 * Probably specific to PCM16 though... */
			SetLoopOffset(DspAddressToSamples(loopStart) - 2);
			SetLoopLength(DspAddressToSamples(loopEnd - loopStart) + 1);
		}

		uint32_t channelInfoTableBase = dwOffset + GetWordBE(dwOffset + 0x10);
		for (int i = 0; i < channels; i++) {
			ChannelParam *channelParam = &channelParams[i];

			uint32_t channelInfoBase = dwOffset + GetWordBE(channelInfoTableBase + (i * 4));
			channelParam->dataOffset = GetWordBE(channelInfoBase + 0x00);

			uint32_t adpcmOffset = GetWordBE(channelInfoBase + 0x04);
			/* volumeFrontL, volumeFrontR, volumeRearL, volumeRearR */

			if (adpcmOffset != 0) {
				uint32_t adpcmBase = dwOffset + adpcmOffset;
				ReadAdpcmParam(adpcmBase, &channelParam->adpcmParam);
			}
		}

		uint32_t dataLocation = GetWordBE(dwOffset + 0x14);
		dataOff += dataLocation;

		/* Quite sure this maps to end bytes, even without looping. */
		dataLength = loopEnd;
		ulUncompressedSize = DspAddressToSamples(loopEnd) * (bps / 8) * channels;

		name = (format == PCM8 ? L"PCM8" : format == PCM16 ? L"PCM16" : L"ADPCM");
	}

	static int16_t Clamp16(int16_t sample) {
		if (sample >  0x7FFF) return  0x7FFF;
		if (sample < -0x8000) return -0x8000;
		return sample;
	}
	
	void ConvertAdpcmChannel(ChannelParam *param, uint8_t *buf, int channelSpacing) {
		int16_t hist1 = param->adpcmParam.yn1, hist2 = param->adpcmParam.yn2;

		uint32_t samplesDone = 0;
		uint32_t numSamples = DspAddressToSamples(dataLength);

		/* Go over and convert each frame of 14 samples. */
		uint32_t srcOffs = dataOff + param->dataOffset;
		uint32_t dstOffs = 0;
		while (samplesDone < numSamples) {
			/* Each frame is 8 bytes -- a one-byte header, and then seven bytes
			 * of samples. Each sample is 4 bits, so we have 14 samples per frame. */
			uint8_t frameHeader = GetByte(srcOffs++);

			/* Sanity check. */
			if (samplesDone == 0)
				assert(frameHeader == param->adpcmParam.expectedScale);

			int32_t scale = 1 << (frameHeader & 0x0F);
			uint8_t coefIdx = (frameHeader >> 4);
			int16_t coef1 = (int16_t) param->adpcmParam.coef[coefIdx * 2], coef2 = (int16_t) param->adpcmParam.coef[coefIdx * 2 + 1];

			for (int i = 0; i < 14; i++) {
				/* Pull out the correct byte and nibble. */
				uint8_t byte = GetByte(srcOffs + (i >> 1));
				uint8_t unsignedNibble = (i & 1) ? (byte & 0x0F) : (byte >> 4);

				/* Sign extend. */
				int8_t signedNibble = ((int8_t) (unsignedNibble << 4)) >> 4;

				/* Decode. */
				int16_t sample = Clamp16((((signedNibble * scale) << 11) + 1024 + (coef1*hist1 + coef2*hist2)) >> 11);
				((int16_t *)buf)[dstOffs] = sample;
				dstOffs += channelSpacing;

				hist2 = hist1;
				hist1 = sample;

				samplesDone++;
				if (samplesDone >= numSamples)
					break;
			}

			srcOffs += 7;
		}
	}

	void ConvertPCM8Channel(ChannelParam *param, uint8_t *buf, int channelSpacing) {
		uint32_t srcOffs = dataOff + param->dataOffset;
		uint32_t numSamples = DspAddressToSamples(dataLength);
		GetBytes(srcOffs, numSamples, buf);
	}

	void ConvertPCM16Channel(ChannelParam *param, uint8_t *buf, int channelSpacing) {
		/* Do this the long way because of endianness issues... */

		uint32_t srcOffs = dataOff + param->dataOffset;
		uint32_t dstOffs = 0;

		uint32_t samplesDone = 0;
		uint32_t numSamples = DspAddressToSamples(dataLength);

		while (samplesDone < numSamples) {
			uint16_t sample = GetWordBE(srcOffs);
			srcOffs += 2;

			((int16_t *)buf)[dstOffs] = sample;
			dstOffs += channelSpacing;

			samplesDone++;
		}
	}

	void ConvertChannel(ChannelParam *param, uint8_t *buf, int channelSpacing) {
		if (format == ADPCM)
			ConvertAdpcmChannel(param, buf, channelSpacing);
		else if (format == PCM8)
			ConvertPCM8Channel(param, buf, channelSpacing);
		else if (format == PCM16)
			ConvertPCM16Channel(param, buf, channelSpacing);
	}

	virtual void ConvertToStdWave(uint8_t *buf) override {
		int bytesPerSample = bps / 8;
		for (int i = 0; i < channels; i++)
			ConvertChannel(&channelParams[i], &buf[i*bytesPerSample], channels);
	}
};

class RSARInstrSet : public VGMInstrSet {
public:
	RSARInstrSet(RawFile *file,
		uint32_t offset,
		uint32_t length,
		std::wstring name = L"RSAR Instrument Bank",
		VGMSampColl *sampColl = NULL) : VGMInstrSet(RSARFormat::name, file, offset, length, name, sampColl) {}
private:
	virtual bool GetInstrPointers() override {
		uint32_t instTableCount = GetWordBE(dwOffset + 0x00);
		uint32_t instTableIdx = dwOffset + 0x04;
		for (uint32_t i = 0; i < instTableCount; i++) {
			/* Some RBNK files have NULL offsets here. Don't crash in that case. */
			uint32_t instKind = GetByte(instTableIdx + 0x01);
			if (instKind != 0x00)
				aInstrs.push_back(new RBNKInstr(this, instTableIdx, 0, i));
			instTableIdx += 0x08;
		}
		return true;
	}
};

class RSARSampCollWAVE : public VGMSampColl {
public:
	RSARSampCollWAVE(RawFile *file, uint32_t offset, uint32_t length,
		uint32_t pWaveDataOffset, uint32_t pWaveDataLength,
		std::wstring name = L"RBNK WAVE Sample Collection")
		: VGMSampColl(RSARFormat::name, file, offset, length, name),
		  waveDataOffset(pWaveDataOffset), waveDataLength(pWaveDataLength) {}
private:
	uint32_t waveDataOffset;
	uint32_t waveDataLength;

	virtual bool GetSampleInfo() override {
		uint32_t waveTableCount = GetWordBE(dwOffset + 0x00);
		uint32_t waveTableIdx = dwOffset + 0x08;
		for (uint32_t i = 0; i < waveTableCount; i++) {
			uint32_t waveOffs = dwOffset + GetWordBE(waveTableIdx);
			samples.push_back(new RBNKSamp(this, waveOffs, 0, waveDataOffset, 0));
			waveTableIdx += 0x08;
		}
		return true;
	}
};

class RSARSampCollRWAR : public VGMSampColl {
public:
	RSARSampCollRWAR(RawFile *file, uint32_t offset, uint32_t length,
		std::wstring name = L"RWAR Sample Collection")
		: VGMSampColl(RSARFormat::name, file, offset, length, name) {}
private:
	VGMSamp * ParseRWAVFile(FileRange rwav) {
		if (!MatchMagic(rawfile, rwav.offset, "RWAV\xFE\xFF\x01"))
			return nullptr;

		uint32_t infoBlockOffs = rwav.offset + GetWordBE(rwav.offset + 0x10);
		uint32_t dataBlockOffs = rwav.offset + GetWordBE(rwav.offset + 0x18);
		FileRange infoBlock = CheckBlock(rawfile, infoBlockOffs, "INFO");
		FileRange dataBlock = CheckBlock(rawfile, dataBlockOffs, "DATA");

		/* Bizarrely enough, dataLocation inside the WAVE info points to the start
		 * of the DATA block header so add an extra 0x08. */
		uint32_t dataOffset = infoBlock.offset + 0x08;
		uint32_t dataSize = dataBlock.size + 0x08;

		return new RBNKSamp(this, infoBlock.offset, infoBlock.size, dataOffset, dataSize);
	}

	virtual bool GetHeaderInfo() override {
		AddHeader(dwOffset, 8, L"RWAR Signature");
		if (!MatchMagic(rawfile, dwOffset, "RWAR\xFE\xFF\x01"))
			return false;

		uint32_t tablBlockOffs = dwOffset + GetWordBE(dwOffset + 0x10);
		uint32_t dataBlockOffs = dwOffset + GetWordBE(dwOffset + 0x18);
		FileRange tablBlock = CheckBlock(rawfile, tablBlockOffs, "TABL");

		uint32_t waveTableCount = GetWordBE(tablBlock.offset + 0x00);
		uint32_t waveTableIdx = tablBlock.offset + 0x08;
		for (uint32_t i = 0; i < waveTableCount; i++) {
			uint32_t rwavHeaderOffs = dataBlockOffs + GetWordBE(waveTableIdx);
			uint32_t rwavHeaderSize = GetWordBE(waveTableIdx + 0x04);
			FileRange rwav = { rwavHeaderOffs, rwavHeaderSize };
			samples.push_back(ParseRWAVFile(rwav));
			waveTableIdx += 0x0C;
		}

		return true;
	}

	virtual bool GetSampleInfo() override {
		return true;
	}
};

struct RBNK {
	VGMInstrSet *instrSet;
	VGMSampColl *sampColl;

	static RBNK Parse(RawFile *file, std::wstring name, uint32_t rbnkOffset, uint32_t waveDataOffset) {
		if (!MatchMagic(file, rbnkOffset, "RBNK\xFE\xFF\x01"))
			return { nullptr, nullptr };

		uint32_t version = file->GetByte(rbnkOffset + 0x07);
		uint32_t dataBlockOffs = rbnkOffset + file->GetWordBE(rbnkOffset + 0x10);
		uint32_t waveBlockOffs = rbnkOffset + file->GetWordBE(rbnkOffset + 0x18);

		bool success;
		FileRange dataBlock = CheckBlock(file, dataBlockOffs, "DATA");
		VGMInstrSet *instrSet = new RSARInstrSet(file, dataBlock.offset, dataBlock.size, name);
		success = instrSet->LoadVGMFile();
		assert(success);

		FileRange waveBlock = CheckBlock(file, waveBlockOffs, "WAVE");
		VGMSampColl *sampColl;
		if (waveBlock.size != 0)
			sampColl = new RSARSampCollWAVE(file, waveBlock.offset, waveBlock.size, waveDataOffset, 0);
		else
			sampColl = new RSARSampCollRWAR(file, waveDataOffset, 0);
		success = sampColl->LoadVGMFile();
		assert(success);

		RBNK bank = { instrSet, sampColl };
		return bank;
	}
};

/* RSAR */

struct Sound {
	std::string name;
	uint32_t fileID;

	enum Type { SEQ = 1, STRM = 2, WAVE = 3 } type;
	struct {
		uint32_t dataOffset;
		uint32_t bankID;
		uint32_t allocTrack;
	} seq;
};

struct Bank {
	std::string name;
	uint32_t fileID;
};

struct GroupItem {
	uint32_t fileID;
	FileRange data;
	FileRange waveData;
};

struct Group {
	std::string name;
	FileRange data;
	FileRange waveData;
	std::vector<GroupItem> items;
};

struct File {
	uint32_t groupID;
	uint32_t index;
};

struct Context {
	RawFile *file;

	/* RSAR structure internals... */
	std::vector<std::string> stringTable;
	std::vector<Sound> soundTable;
	std::vector<Bank> bankTable;
	std::vector<File> fileTable;
	std::vector<Group> groupTable;

	/* Parsed stuff. */
	std::vector<RBNK> rbnks;
};

static std::vector<std::string> ParseSymbBlock(RawFile *file, uint32_t blockBase) {
	uint32_t strTableBase = blockBase + file->GetWordBE(blockBase);
	uint32_t strTableCount = file->GetWordBE(strTableBase + 0x00);

	std::vector<std::string> table(strTableCount);
	uint32_t strTableIdx = strTableBase + 0x04;
	for (uint32_t i = 0; i < strTableCount; i++) {
		uint32_t strOffs = file->GetWordBE(strTableIdx);
		strTableIdx += 0x04;

		uint8_t *strP = &file->buf.data[blockBase + strOffs];
		std::string str((char *) strP);
		table[i] = str;
	}

	return table;
}

static std::vector<Sound> ReadSoundTable(Context *context, DataSeg *block) {
	uint32_t soundTableOffs = block->GetWordBE(0x04);
	uint32_t soundTableCount = block->GetWordBE(soundTableOffs);
	std::vector<Sound> soundTable;

	uint32_t soundTableIdx = soundTableOffs + 0x08;
	for (uint32_t i = 0; i < soundTableCount; i++) {
		uint32_t soundOffs = block->GetWordBE(soundTableIdx);
		soundTableIdx += 0x08;

		Sound sound;
		uint32_t stringID = block->GetWordBE(soundOffs + 0x00);
		if (stringID != 0xFFFFFFFF)
			sound.name = context->stringTable[stringID];
		sound.fileID = block->GetWordBE(soundOffs + 0x04);
		/* 0x08 player */
		/* 0x0C param3DRef */
		/* 0x14 volume */
		/* 0x15 playerPriority */
		sound.type = (Sound::Type) block->GetByte(soundOffs + 0x16);
		if (sound.type != Sound::Type::SEQ)
			continue;

		/* 0x17 remoteFilter */
		uint32_t seqInfoOffs = block->GetWordBE(soundOffs + 0x1C);

		sound.seq.dataOffset = block->GetWordBE(seqInfoOffs + 0x00);
		sound.seq.bankID = block->GetWordBE(seqInfoOffs + 0x04);
		sound.seq.allocTrack = block->GetWordBE(seqInfoOffs + 0x08);
		soundTable.push_back(sound);
	}

	return soundTable;
}
	
static std::vector<Bank> ReadBankTable(Context *context, DataSeg *block) {
	uint32_t bankTableOffs = block->GetWordBE(0x0C);
	uint32_t bankTableCount = block->GetWordBE(bankTableOffs);
	std::vector<Bank> bankTable;

	uint32_t bankTableIdx = bankTableOffs + 0x08;
	for (uint32_t i = 0; i < bankTableCount; i++) {
		uint32_t bankOffs = block->GetWordBE(bankTableIdx);
		bankTableIdx += 0x08;

		Bank bank;
		uint32_t stringID = block->GetWordBE(bankOffs + 0x00);
		if (stringID != 0xFFFFFFFF)
			bank.name = context->stringTable[stringID];
		bank.fileID = block->GetWordBE(bankOffs + 0x04);
		bankTable.push_back(bank);
	}

	return bankTable;
}

static std::vector<File> ReadFileTable(Context *context, DataSeg *block) {
	uint32_t fileTableOffs = block->GetWordBE(0x1C);
	uint32_t fileTableCount = block->GetWordBE(fileTableOffs);
	std::vector<File> fileTable;

	uint32_t fileTableIdx = fileTableOffs + 0x08;
	for (uint32_t i = 0; i < fileTableCount; i++) {
		uint32_t fileOffs = block->GetWordBE(fileTableIdx);
		fileTableIdx += 0x08;

		File file = {};
		uint32_t filePosTableOffs = block->GetWordBE(fileOffs + 0x18);
		uint32_t filePosTableCount = block->GetWordBE(filePosTableOffs);

		/* Push a dummy file if we don't have anything and hope nothing references it... */
		if (filePosTableCount == 0) {
			fileTable.push_back(file);
			continue;
		}

		uint32_t filePosOffs = block->GetWordBE(filePosTableOffs + 0x08);
		file.groupID = block->GetWordBE(filePosOffs + 0x00);
		file.index = block->GetWordBE(filePosOffs + 0x04);
		fileTable.push_back(file);
	}

	return fileTable;
}

static std::vector<Group> ReadGroupTable(Context *context, DataSeg *block) {
	uint32_t groupTableOffs = block->GetWordBE(0x24);
	uint32_t groupTableCount = block->GetWordBE(groupTableOffs);
	std::vector<Group> groupTable;

	uint32_t groupTableIdx = groupTableOffs + 0x08;
	for (uint32_t i = 0; i < groupTableCount; i++) {
		uint32_t groupOffs = block->GetWordBE(groupTableIdx);
		groupTableIdx += 0x08;

		uint32_t stringID = block->GetWordBE(groupOffs + 0x00);
		Group group;
		if (stringID != 0xFFFFFFFF)
			group.name = context->stringTable[stringID];
		group.data.offset = block->GetWordBE(groupOffs + 0x10);
		group.data.size = block->GetWordBE(groupOffs + 0x14);
		group.waveData.offset = block->GetWordBE(groupOffs + 0x18);
		group.waveData.size = block->GetWordBE(groupOffs + 0x1C);

		uint32_t itemTableOffs = block->GetWordBE(groupOffs + 0x24);
		uint32_t itemTableCount = block->GetWordBE(itemTableOffs);
		uint32_t itemTableIdx = itemTableOffs + 0x08;
		for (uint32_t j = 0; j < itemTableCount; j++) {
			uint32_t itemOffs = block->GetWordBE(itemTableIdx);
			itemTableIdx += 0x08;

			GroupItem item;
			item.fileID = block->GetWordBE(itemOffs + 0x00);
			item.data.offset = block->GetWordBE(itemOffs + 0x04);
			item.data.size = block->GetWordBE(itemOffs + 0x08);
			item.waveData.offset = block->GetWordBE(itemOffs + 0x0C);
			item.waveData.size = block->GetWordBE(itemOffs + 0x10);
			group.items.push_back(item);
		}

		groupTable.push_back(group);
	}

	return groupTable;
}

static RBNK LoadBank(Context *context, Bank *bank) {
	File fileInfo = context->fileTable[bank->fileID];
	Group groupInfo = context->groupTable[fileInfo.groupID];
	GroupItem itemInfo = groupInfo.items[fileInfo.index];

	assert(itemInfo.fileID == bank->fileID);
	uint32_t rbnkOffset = itemInfo.data.offset + groupInfo.data.offset;
	uint32_t waveOffset = itemInfo.waveData.offset + groupInfo.waveData.offset;

	return RBNK::Parse(context->file, string_to_wstring(bank->name), rbnkOffset, waveOffset);
}

static void LoadSeq(Context *context, Sound *sound) {
	assert(sound->type == Sound::SEQ);

	File fileInfo = context->fileTable[sound->fileID];
	Group groupInfo = context->groupTable[fileInfo.groupID];
	GroupItem itemInfo = groupInfo.items[fileInfo.index];

	bool success;

	uint32_t rseqOffset = itemInfo.data.offset + groupInfo.data.offset;
	VGMSeq *seq = RSEQ::Parse(context->file, string_to_wstring(sound->name), rseqOffset, sound->seq.dataOffset, sound->seq.allocTrack);
	success = seq->LoadVGMFile();
	assert(success);
	RBNK rbnk = context->rbnks[sound->seq.bankID];

	VGMColl *coll = new VGMColl(string_to_wstring(sound->name));
	if (rbnk.instrSet)
		coll->AddInstrSet(rbnk.instrSet);
	if (rbnk.sampColl)
		coll->AddSampColl(rbnk.sampColl);
	coll->UseSeq(seq);
	success = coll->Load();
	assert(success);
}

void RSARScanner::Scan(RawFile *file, void *info) {
	if (!MatchMagic(file, 0, "RSAR\xFE\xFF\x01"))
		return;

	uint32_t version = file->GetByte(0x07);
	uint32_t symbBlockOffs = file->GetWordBE(0x10);
	uint32_t infoBlockOffs = file->GetWordBE(0x18);
	uint32_t fileBlockOffs = file->GetWordBE(0x20);

	FileRange symbBlockBase = CheckBlock(file, symbBlockOffs, "SYMB");
	std::vector<std::string> strTable = ParseSymbBlock(file, symbBlockBase.offset);

	Context context;
	context.file = file;
	context.stringTable = strTable;

	DataSeg *infoBlock = LoadBlock(file, infoBlockOffs, "INFO");

	/*
	uint32_t playerTableOffs = file->GetWordBE(infoBlockBase + 0x14);
	*/

	context.soundTable = ReadSoundTable(&context, infoBlock);
	context.bankTable = ReadBankTable(&context, infoBlock);
	context.fileTable = ReadFileTable(&context, infoBlock);
	context.groupTable = ReadGroupTable(&context, infoBlock);

	/* Load all BNKS. */
	for (size_t i = 0; i < context.bankTable.size(); i++)
		context.rbnks.push_back(LoadBank(&context, &context.bankTable[i]));

	for (size_t i = 0; i < context.soundTable.size(); i++)
		LoadSeq(&context, &context.soundTable[i]);
}
