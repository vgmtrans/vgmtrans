/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Helpers.h"
#include <QPixmap>
#include <QBitmap>

const QIcon &iconForFileType(FileType filetype) {
  switch (filetype) {
    case FILETYPE_SEQ: {
      static QIcon i_gen{":/images/sequence.svg"};
      return i_gen;
    }

    case FILETYPE_INSTRSET: {
      static QIcon i_gen{":/images/instrument-set.svg"};
      return i_gen;
    }

    case FILETYPE_SAMPCOLL: {
      static QIcon i_gen{":/images/sample-collection.svg"};
      return i_gen;
    }

    default:
      break;
  }

  static QIcon i_gen{":/images/file.png"};
  return i_gen;
}

const QIcon &iconForItemType(VGMItem::Icon type) {
  switch (type) {
    case VGMItem::ICON_NOTE: {
      static QIcon i_gen{":/images/note.svg"};
      return i_gen;
    }

    case VGMItem::ICON_SEQ: {
      static QIcon i_gen{":/images/sequence.svg"};
      return i_gen;
    }

    case VGMItem::ICON_SAMPCOLL: {
      static QIcon i_gen{":/images/wave.svg"};
      return i_gen;
    }

    case VGMItem::ICON_INSTRSET: {
      static QIcon i_gen{":/images/instrument-set.svg"};
      return i_gen;
    }

    case VGMItem::ICON_INSTR: {
      static QIcon i_gen{":/images/instr.svg"};
      return i_gen;
    }

    case VGMItem::ICON_STARTREP:
    case VGMItem::ICON_TRACK: {
      static QIcon i_gen{":/images/track.svg"};
      return i_gen;
    }

    case VGMItem::ICON_ENDREP:
    case VGMItem::ICON_TRACKEND: {
      static QIcon i_gen{":/images/trackend.svg"};
      return i_gen;
    }

    case VGMItem::ICON_PROGCHANGE: {
      static QIcon i_gen{":/images/progchange.svg"};
      return i_gen;
    }

    case VGMItem::ICON_CONTROL: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::ICON_BINARY: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::ICON_REST: {
      static QIcon i_gen{":/images/rest.svg"};
      return i_gen;
    }

    case VGMItem::ICON_TEMPO: {
      static QIcon i_gen{":/images/tempo.svg"};
      return i_gen;
    }

    case VGMItem::ICON_SAMP: {
      static QIcon i_gen{":/images/sample.svg"};
      return i_gen;
    }

    default:
      break;
  }

  static auto i_gen = QIcon(":/images/file.svg");
  return i_gen;
}

QColor colorForEventColor(VGMItem::EventColor eventColor) {
  switch (eventColor) {
    case VGMItem::CLR_UNKNOWN:
      return Qt::black;
    case VGMItem::CLR_UNRECOGNIZED:
      return Qt::red;
    case VGMItem::CLR_HEADER:
      return Qt::lightGray;
    case VGMItem::CLR_MISC:
      return Qt::gray;
    case VGMItem::CLR_MARKER:
      return Qt::gray;
    case VGMItem::CLR_TIMESIG:
      return Qt::green;
    case VGMItem::CLR_TEMPO:
      return Qt::darkGreen;
    case VGMItem::CLR_PROGCHANGE:
      return QColor(244, 152, 16, 255);
    case VGMItem::CLR_TRANSPOSE:
      return Qt::darkMagenta;
    case VGMItem::CLR_PRIORITY:
      return Qt::darkMagenta;
    case VGMItem::CLR_VOLUME:
      return QColor(174, 138, 190, 255);
    case VGMItem::CLR_EXPRESSION:
      return QColor(83, 43, 46, 255);
    case VGMItem::CLR_PAN:
      return QColor(50, 89, 61, 255);
    case VGMItem::CLR_NOTEON:
      return QColor(40, 123, 222, 255);
    case VGMItem::CLR_NOTEOFF:
      return QColor(65, 159, 211, 255);
    case VGMItem::CLR_DURNOTE:
      return QColor(73, 164, 219, 255);
    case VGMItem::CLR_TIE:
      return QColor(117, 109, 220, 255);
    case VGMItem::CLR_REST:
      return QColor(142, 101, 207, 255);
    case VGMItem::CLR_PITCHBEND:
      return Qt::magenta;
    case VGMItem::CLR_PITCHBENDRANGE:
      return Qt::magenta;
    case VGMItem::CLR_MODULATION:
      return QColor(176, 97, 0, 255);
    case VGMItem::CLR_PORTAMENTO:
      return Qt::magenta;
    case VGMItem::CLR_PORTAMENTOTIME:
      return Qt::magenta;
    case VGMItem::CLR_CHANGESTATE:
      return Qt::gray;
    case VGMItem::CLR_ADSR:
      return Qt::gray;
    case VGMItem::CLR_LFO:
      return Qt::gray;
    case VGMItem::CLR_REVERB:
      return Qt::gray;
    case VGMItem::CLR_SUSTAIN:
      return Qt::yellow;
    case VGMItem::CLR_LOOP:
      return QColor(188, 63, 60, 255);
    case VGMItem::CLR_LOOPFOREVER:
      return QColor(188, 63, 60, 255);
    case VGMItem::CLR_TRACKEND:
      return QColor(158, 41, 39, 255);
  }
  return Qt::red;
}

QColor textColorForEventColor(VGMItem::EventColor eventColor) {
  return Qt::white;
}
