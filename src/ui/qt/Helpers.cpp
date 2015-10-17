#include "Helpers.h"

QIcon iconForFileType(FileType filetype) {
    switch (filetype) {
        case FILETYPE_SEQ:
            return QIcon(":/images/music_transcripts-32.png");
        case FILETYPE_INSTRSET:
            return QIcon(":/images/piano-32.png");
        case FILETYPE_SAMPCOLL:
            return QIcon(":/images/audio_wave-32.png");
    }
    return QIcon(":/images/audio_file-32.png");
}

QColor colorForEventColor(uint8_t eventColor) {

    switch (eventColor) {
        case CLR_UNKNOWN:           return Qt::black;
        case CLR_UNRECOGNIZED:      return Qt::red;
        case CLR_HEADER:            return Qt::lightGray;
        case CLR_MISC:              return Qt::gray;
        case CLR_MARKER:            return Qt::gray;
        case CLR_TIMESIG:           return Qt::green;
        case CLR_TEMPO:             return Qt::darkGreen;
        case CLR_PROGCHANGE:        return Qt::magenta;
        case CLR_TRANSPOSE:         return Qt::darkMagenta;
        case CLR_PRIORITY:          return Qt::darkMagenta;
        case CLR_VOLUME:            return Qt::darkRed;
        case CLR_EXPRESSION:        return Qt::darkYellow;
        case CLR_PAN:               return Qt::yellow;
        case CLR_NOTEON:            return Qt::blue;
        case CLR_NOTEOFF:           return Qt::darkBlue;
        case CLR_DURNOTE:           return Qt::blue;
        case CLR_TIE:               return QColor(255, 128, 128, 255);
        case CLR_REST:              return QColor(255, 196, 196, 255);
        case CLR_PITCHBEND:         return Qt::magenta;
        case CLR_PITCHBENDRANGE:    return Qt::magenta;
        case CLR_MODULATION:        return Qt::yellow;
        case CLR_PORTAMENTO:        return Qt::magenta;
        case CLR_PORTAMENTOTIME:    return Qt::magenta;
        case CLR_CHANGESTATE:       return Qt::gray;
        case CLR_ADSR:              return Qt::gray;
        case CLR_LFO:               return Qt::gray;
        case CLR_REVERB:            return Qt::gray;
        case CLR_SUSTAIN:           return Qt::yellow;
        case CLR_LOOP:              return Qt::yellow;
        case CLR_LOOPFOREVER:       return Qt::yellow;
        case CLR_TRACKEND:          return Qt::red;
    }
    return Qt::red;
}