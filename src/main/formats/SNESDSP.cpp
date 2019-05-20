/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
// Many thanks to bsnes and snes9x.

#include "SNESDSP.h"
#include "Root.h"

// *************
// SNES Envelope
// *************

// Emulate GAIN envelope while (increase: env < env_to, or decrease: env > env_to)
// return elapsed time in sample count, and final env value if requested.
uint32_t EmulateSDSPGAIN(uint8_t gain, int16_t env_from, int16_t env_to, int16_t *env_after_ptr,
                         double *sf2_envelope_time_ptr) {
    // check for illegal parameters
    if (env_from < 0 || env_from > 0x7ff) {
        return 0;
    }

    if (env_to < 0 || env_to > 0x7ff) {
        return 0;
    }

    uint8_t mode = gain >> 5;
    uint8_t rate = gain & 0x1f;
    uint32_t tick = 0;

    uint32_t tick_exp = 0;
    int16_t env_exp_final = env_to;

    int16_t env = env_from;
    if (mode < 4) {  // direct
        env = gain * 0x10;
        rate = 31;
        tick = 0;
    } else if (mode == 4) {  // 4: linear decrease
        while (env > env_to) {
            env -= 0x20;
            if (env < 0)
                env = 0;
            tick++;
        }
    } else if (mode == 5) {  // 5: exponential decrease
        while (env > env_to) {
            int16_t env_prev = env;

            env--;
            env -= env >> 8;
            tick++;

            if (env <= 255 && env_prev > 255) {
                env_exp_final = env;
            }

            if (env > 255) {
                tick_exp++;
            }
        }
    } else {                                                // 6,7: linear increase
        int16_t env_prev = (env >= 0x20) ? env - 0x20 : 0;  // guess previous value

        while (env < env_to) {
            env += 0x20;
            if (mode > 6 && (unsigned)env_prev >= 0x600) {
                env += 0x8 - 0x20;  // 7: two-slope linear increase
            }
            env_prev = env;
            if (env > 0x7ff) {
                env = 0x7ff;
            }
            tick++;
        }
    }

    uint32_t total_samples = tick * SDSP_COUNTER_RATES[rate];
    if (env_after_ptr != NULL) {
        *env_after_ptr = env;
    }

    // calculate envelope time for soundfont use
    if (sf2_envelope_time_ptr != NULL) {
        double sf2_time;
        if (mode < 4) {  // direct
            sf2_time = 0.0;
        } else if (mode == 4) {  // 4: linear decrease
            uint32_t total_samples_full = (0x800 / 0x20) * SDSP_COUNTER_RATES[rate];
            sf2_time = LinAmpDecayTimeToLinDBDecayTime(total_samples_full / 32000.0, 0x800);
        } else if (mode == 5) {  // 5: exponential decrease
            // Exponential decrease mode is almost exponential.
            // It will become to linear decrease when ENVX <= 255.
            // Soundfont is always exponential, so this is a lossy conversion.

            if (tick == 0) {
                sf2_time = 0;
            } else {
                if (env_from > 255) {
                    // exponential part
                    double decibelAtStart = ConvertPercentAmplitudeToAttenDB(env_from / 2047.0);
                    double decibelAtExpFinal =
                        ConvertPercentAmplitudeToAttenDB(env_exp_final / 2047.0);
                    double timeAtExpFinal = (tick_exp * SDSP_COUNTER_RATES[rate]) / 32000.0;
                    sf2_time = timeAtExpFinal * (-100.0 / (decibelAtExpFinal - decibelAtStart));
                } else {
                    // linear part (very small volume)
                    // Measure the time to end volume and fit it to -100.0 dB scale.

                    if (env == 0 && tick == 1) {
                        sf2_time = SDSP_COUNTER_RATES[rate] / 32000.0;
                    } else {
                        int16_t env_final = env;
                        uint32_t tick_total = tick;

                        // avoid -INF decibel
                        if (env_final == 0) {
                            env_final++;
                            tick_total--;
                        }

                        double decibelAtStart = ConvertPercentAmplitudeToAttenDB(env_from / 2047.0);
                        double decibelAtFinal =
                            ConvertPercentAmplitudeToAttenDB(env_final / 2047.0);
                        double timeAtExpFinal = (tick_total * SDSP_COUNTER_RATES[rate]) / 32000.0;
                        sf2_time = timeAtExpFinal * (-100.0 / (decibelAtFinal - decibelAtStart));
                    }

                    // Alternate method:
                    // differentiate the logarithmic-decibel ENVX curve,
                    // then consider the slope of the line as envelope speed.
                    // double env_start = std::maxenv_from, 1);
                    // double decibelDiff = ConvertPercentAmplitudeToAttenDB(env_start / 2047.0) -
                    // ConvertPercentAmplitudeToAttenDB((env_start + 1) / 2047.0); double timePerTick
                    // = SDSP_COUNTER_RATES[rate] / 32000.0; sf2_time = timePerTick * (-100.0 /
                    // decibelDiff);
                }
            }
        } else {  // 6,7: linear increase
            // 7: two-slope linear increase is unable to convert the SF2 time
            uint32_t total_samples_full = (0x800 / 0x20) * SDSP_COUNTER_RATES[rate];
            sf2_time = total_samples_full / 32000.0;  // linear attack time from 0 to full
        }

        *sf2_envelope_time_ptr = sf2_time;
    }

    return total_samples;
}

// See Anomie's S-DSP document for technical details
// http://www.romhacking.net/documents/191/
void ConvertSNESADSR(uint8_t adsr1, uint8_t adsr2, uint8_t gain, uint16_t env_from,
                     double *ptr_attack_time, double *ptr_decay_time, double *ptr_sustain_level,
                     double *ptr_sustain_time, double *ptr_release_time) {
    bool adsr_enabled = (adsr1 & 0x80) != 0;

    double attack_time;
    double decay_time;
    double sustain_level;
    double sustain_time;
    double release_time;

    bool have_attack_time = false;
    bool have_decay_time = false;
    bool have_sustain_level = false;
    bool have_sustain_time = false;
    bool have_release_time = false;

    int16_t env;
    int16_t env_after;
    uint32_t samples;

    if (adsr_enabled) {
        // ADSR mode
        uint8_t ar = adsr1 & 0x0f;
        uint8_t dr = (adsr1 & 0x70) >> 4;
        uint8_t sl = (adsr2 & 0xe0) >> 5;
        uint8_t sr = adsr2 & 0x1f;

        // attack
        if (ar < 15) {
            attack_time = SDSP_COUNTER_RATES[ar * 2 + 1] * 64 / 32000.0;
        } else {
            attack_time = 2 / 32000.0;
        }
        env = 0x7FF;

        // decay
        int16_t env_sustain_start = env;
        if (sl == 7) {
            // no decay
            decay_time = 0;
        } else {
            uint8_t dr_rate = 0x10 | (dr << 1);
            EmulateSDSPGAIN(0xa0 | dr_rate, env, (sl << 8) | 0xff, &env_after,
                            &decay_time);  // exponential decrease
            env_sustain_start = env_after;
            env = env_after;
        }

        // sustain
        sustain_level = (sl + 1) / 8.0;
        if (sr == 0) {
            sustain_time = -1;  // infinite
        } else {
            EmulateSDSPGAIN(0xa0 | sr, env, 0, &env_after, &sustain_time);  // exponential decrease
        }

        // release
        // decrease envelope by 8 for every sample
        samples = (env_sustain_start + 7) / 8;
        release_time = LinAmpDecayTimeToLinDBDecayTime(samples / 32000.0, 0x7ff);

        have_attack_time = true;
        have_decay_time = true;
        have_sustain_level = true;
        have_sustain_time = true;
        have_release_time = true;
    } else {
        uint8_t mode = gain >> 5;

        if (mode < 4) {  // direct
            attack_time = 0;
            decay_time = -1;
            sustain_level = (gain & 0x7f) / 128.0;
            sustain_time = -1;

            // release
            // decrease envelope by 8 for every sample
            samples = (env_from + 7) / 8;
            release_time = LinAmpDecayTimeToLinDBDecayTime(samples / 32000.0, 0x7ff);

            have_attack_time = true;
            have_decay_time = true;
            have_sustain_level = true;
            have_sustain_time = true;
            have_release_time = true;
        } else {
            env = env_from;
            int16_t env_to = (mode >= 6) ? 0x7ff : 0;
            double sf2_env_time;
            EmulateSDSPGAIN(gain, env, env_to, &env_after, &sf2_env_time);

            if (mode >= 6) {
                attack_time = sf2_env_time;
                decay_time = -1;
                sustain_level = 1.0;
                sustain_time = -1;

                // release
                // decrease envelope by 8 for every sample
                samples = (env_to + 7) / 8;
                release_time = LinAmpDecayTimeToLinDBDecayTime(samples / 32000.0, 0x7ff);
            } else {
                attack_time = 0.0;
                decay_time = sf2_env_time;
                sustain_level = 0;
                sustain_time = 0;

                // release
                // decrease envelope by 8 for every sample
                samples = (env_from + 7) / 8;
                release_time = LinAmpDecayTimeToLinDBDecayTime(samples / 32000.0, 0x7ff);
            }

            have_attack_time = true;
            have_decay_time = true;
            have_sustain_level = true;
            have_sustain_time = true;
            have_release_time = true;
        }
    }

    if (ptr_attack_time != NULL && have_attack_time) {
        *ptr_attack_time = attack_time;
    }

    if (ptr_decay_time != NULL && have_decay_time) {
        *ptr_decay_time = decay_time;
    }

    if (ptr_sustain_level != NULL && have_sustain_level) {
        *ptr_sustain_level = sustain_level;
    }

    if (ptr_sustain_time != NULL && have_sustain_time) {
        *ptr_sustain_time = sustain_time;
    }

    if (ptr_release_time != NULL && have_release_time) {
        *ptr_release_time = release_time;
    }
}

// ************
// SNESSampColl
// ************

SNESSampColl::SNESSampColl(const std::string &format, RawFile *rawfile, uint32_t offset,
                           uint32_t maxNumSamps)
    : VGMSampColl(format, rawfile, offset, 0), spcDirAddr(offset) {
    SetDefaultTargets(maxNumSamps);
}

SNESSampColl::SNESSampColl(const std::string &format, VGMInstrSet *instrset, uint32_t offset,
                           uint32_t maxNumSamps)
    : VGMSampColl(format, instrset->rawfile, instrset, offset, 0), spcDirAddr(offset) {
    SetDefaultTargets(maxNumSamps);
}

SNESSampColl::SNESSampColl(const std::string &format, RawFile *rawfile, uint32_t offset,
                           const std::vector<uint8_t> &targetSRCNs, std::wstring name)
    : VGMSampColl(format, rawfile, offset, 0, name), spcDirAddr(offset), targetSRCNs(targetSRCNs) {}

SNESSampColl::SNESSampColl(const std::string &format, VGMInstrSet *instrset, uint32_t offset,
                           const std::vector<uint8_t> &targetSRCNs, std::wstring name)
    : VGMSampColl(format, instrset->rawfile, instrset, offset, 0, name),
      spcDirAddr(offset),
      targetSRCNs(targetSRCNs) {}

SNESSampColl::~SNESSampColl() {}

void SNESSampColl::SetDefaultTargets(uint32_t maxNumSamps) {
    // limit sample count to 256
    if (maxNumSamps > 256) {
        maxNumSamps = 256;
    }

    // target all samples
    for (uint32_t i = 0; i < maxNumSamps; i++) {
        targetSRCNs.push_back(i);
    }
}

bool SNESSampColl::GetSampleInfo() {
    spcDirHeader = AddHeader(spcDirAddr, 0, L"Sample DIR");
    for (std::vector<uint8_t>::iterator itr = this->targetSRCNs.begin();
         itr != this->targetSRCNs.end(); ++itr) {
        uint8_t srcn = (*itr);
        std::wostringstream name;

        uint32_t offDirEnt = spcDirAddr + (srcn * 4);
        if (!SNESSampColl::IsValidSampleDir(GetRawFile(), offDirEnt, true)) {
            continue;
        }

        uint16_t addrSampStart = GetShort(offDirEnt);
        uint16_t addrSampLoop = GetShort(offDirEnt + 2);

        bool loop;
        uint32_t length = SNESSamp::GetSampleLength(GetRawFile(), addrSampStart, loop);

        name << L"SA " << srcn;
        spcDirHeader->AddSimpleItem(offDirEnt, 2, name.str().c_str());

        name.str(L"");
        name << L"LSA " << srcn;
        spcDirHeader->AddSimpleItem(offDirEnt + 2, 2, name.str().c_str());

        name.str(L"");
        name << L"Sample " << srcn;
        SNESSamp *samp = new SNESSamp(this, addrSampStart, length, addrSampStart, length,
                                      addrSampLoop, name.str());
        samples.push_back(samp);
    }
    spcDirHeader->SetGuessedLength();
    return samples.size() != 0;
}

bool SNESSampColl::IsValidSampleDir(RawFile *file, uint32_t spcDirEntAddr, bool validateSample) {
    if (spcDirEntAddr + 4 > 0x10000) {
        return false;
    }

    uint16_t addrSampStart = file->GetShort(spcDirEntAddr);
    uint16_t addrSampLoop = file->GetShort(spcDirEntAddr + 2);
    if (addrSampLoop < addrSampStart || addrSampStart + 9 >= 0x10000) {
        return false;
    }

    if (validateSample) {
        bool loop;
        uint32_t length = SNESSamp::GetSampleLength(file, addrSampStart, loop);
        if (length == 0) {
            return false;
        }

        uint16_t addrSampEnd = addrSampStart + length;
        if (loop && addrSampLoop >= addrSampEnd) {
            return false;
        }
    }

    return true;
}

//  ********
//  SNESSamp
//  ********

SNESSamp::SNESSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset,
                   uint32_t dataLen, uint32_t loopOffset, std::wstring name)
    : VGMSamp(sampColl, offset, length, dataOffset, dataLen, 1, 16, 32000, name),
      brrLoopOffset(loopOffset) {}

SNESSamp::~SNESSamp() {}

uint32_t SNESSamp::GetSampleLength(RawFile *file, uint32_t offset, bool &loop) {
    uint32_t currOffset = offset;
    while (true) {
        if (currOffset + 9 > file->size()) {
            // address out of range, it's apparently corrupt
            return 0;
        }

        uint8_t flag = file->GetByte(currOffset);
        currOffset += 9;

        // end?
        if ((flag & 1) != 0) {
            loop = (flag & 2) != 0;
            break;
        }
    }
    return (currOffset - offset);
}

double SNESSamp::GetCompressionRatio() {
    return ((16.0 / 9.0) * 2);  // aka 3.55...;
}

void SNESSamp::ConvertToStdWave(uint8_t *buf) {
    BRRBlk theBlock;
    int32_t prev1 = 0;
    int32_t prev2 = 0;

    // loopStatus is initiated to -1.  We should default it now to not loop
    SetLoopStatus(0);

    assert(dataLength % 9 == 0);
    for (uint32_t k = 0; k + 9 <= dataLength; k += 9)  // for every adpcm chunk
    {
        if (dwOffset + k + 9 > GetRawFile()->size()) {
            L_WARN("Unexpected EOF ({})", wstring2string(name));
            break;
        }

        theBlock.flag.range = (GetByte(dwOffset + k) & 0xf0) >> 4;
        theBlock.flag.filter = (GetByte(dwOffset + k) & 0x0c) >> 2;
        theBlock.flag.end = (GetByte(dwOffset + k) & 0x01) != 0;
        theBlock.flag.loop = (GetByte(dwOffset + k) & 0x02) != 0;

        GetRawFile()->GetBytes(dwOffset + k + 1, 8, theBlock.brr);
        DecompBRRBlk((int16_t *)(&buf[k * 32 / 9]), &theBlock, &prev1,
                     &prev2);  // each decompressed pcm block is 32 bytes

        if (theBlock.flag.end) {
            if (theBlock.flag.loop) {
                if (brrLoopOffset <= dwOffset + k) {
                    SetLoopOffset(brrLoopOffset - dwOffset);
                    SetLoopLength((k + 9) - (brrLoopOffset - dwOffset));
                    SetLoopStatus(1);
                }
            }
            break;
        }
    }
}

static inline int32_t absolute(int32_t x) {
    return ((x < 0) ? -x : x);
}

static inline int32_t sclip15(int32_t x) {
    return ((x & 16384) ? (x | ~16383) : (x & 16383));
}

static inline int32_t sclamp8(int32_t x) {
    return ((x > 127) ? 127 : (x < -128) ? -128 : x);
}

static inline int32_t sclamp15(int32_t x) {
    return ((x > 16383) ? 16383 : (x < -16384) ? -16384 : x);
}

static inline int32_t sclamp16(int32_t x) {
    return ((x > 32767) ? 32767 : (x < -32768) ? -32768 : x);
}

void SNESSamp::DecompBRRBlk(int16_t *pSmp, BRRBlk *pVBlk, int32_t *prev1, int32_t *prev2) {
    int32_t out, S1, S2;
    int8_t sample1, sample2;
    bool validHeader;
    int i, nybble;

    validHeader = (pVBlk->flag.range < 0xD);

    S1 = *prev1;
    S2 = *prev2;

    for (i = 0; i < 8; i++) {
        sample1 = pVBlk->brr[i];
        sample2 = sample1 << 4;
        sample1 >>= 4;
        sample2 >>= 4;

        for (nybble = 0; nybble < 2; nybble++) {
            out = nybble ? (int32_t)sample2 : (int32_t)sample1;
            out = validHeader ? ((out << pVBlk->flag.range) >> 1) : (out & ~0x7FF);

            switch (pVBlk->flag.filter) {
                case 0:  // Direct
                    break;

                case 1:  // 15/16
                    out += S1 + ((-S1) >> 4);
                    break;

                case 2:  // 61/32 - 15/16
                    out += (S1 << 1) + ((-((S1 << 1) + S1)) >> 5) - S2 + (S2 >> 4);
                    break;

                case 3:  // 115/64 - 13/16
                    out += (S1 << 1) + ((-(S1 + (S1 << 2) + (S1 << 3))) >> 6) - S2 +
                           (((S2 << 1) + S2) >> 4);
                    break;
            }

            out = sclip15(sclamp16(out));

            S2 = S1;
            S1 = out;

            pSmp[i * 2 + nybble] = out << 1;
        }
    }

    *prev1 = S1;
    *prev2 = S2;
}