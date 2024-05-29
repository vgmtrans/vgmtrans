/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Helpers.h"
#include "Colors.h"
#include <QBitmap>

const QIcon &iconForFile(VGMFileVariant file) {
  static Visitor icon{
    [](VGMSeq *) -> const QIcon & {
      static QIcon i_gen{":/images/sequence.svg"};
      return i_gen;
    },
    [](VGMInstrSet *) -> const QIcon & {
      static QIcon i_gen{":/images/instrument-set.svg"};
      return i_gen;
    },
    [](VGMSampColl *) -> const QIcon & {
      static QIcon i_gen{":/images/sample-collection.svg"};
      return i_gen;
    },
    [](VGMMiscFile *) -> const QIcon & {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    },
  };
  return std::visit(icon, file);
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
      return EventColors::CLR_BG_DARK;
    case VGMItem::CLR_UNRECOGNIZED:
      return EventColors::CLR_RED;
    case VGMItem::CLR_HEADER:
      return EventColors::CLR_GRAY;
    case VGMItem::CLR_MISC:
      return EventColors::CLR_DARK_GRAY;
    case VGMItem::CLR_MARKER:
      return EventColors::CLR_DARK_GRAY;
    case VGMItem::CLR_TIMESIG:
      return EventColors::CLR_ORANGE;
    case VGMItem::CLR_TEMPO:
      return EventColors::CLR_GREEN;
    case VGMItem::CLR_PROGCHANGE:
      return EventColors::CLR_PERIWINKLE;
    case VGMItem::CLR_TRANSPOSE:
      return EventColors::CLR_DARK_GREEN;
    case VGMItem::CLR_PRIORITY:
      return EventColors::CLR_DARK_GREEN;
    case VGMItem::CLR_VOLUME:
      return EventColors::CLR_MAGENTA;
    case VGMItem::CLR_EXPRESSION:
      return EventColors::CLR_MAGENTA;
    case VGMItem::CLR_PAN:
      return EventColors::CLR_ORANGE;
    case VGMItem::CLR_NOTEON:
    case VGMItem::CLR_DURNOTE:
      return EventColors::CLR_BLUE;
    case VGMItem::CLR_NOTEOFF:
      return EventColors::CLR_LIGHT_BLUE;
    case VGMItem::CLR_TIE:
      return EventColors::CLR_LIGHT_BLUE;
    case VGMItem::CLR_REST:
      return EventColors::CLR_LIGHT_BLUE;
    case VGMItem::CLR_PITCHBEND:
      return EventColors::CLR_GREEN;
    case VGMItem::CLR_PITCHBENDRANGE:
      return EventColors::CLR_DARK_GREEN;
    case VGMItem::CLR_MODULATION:
      return EventColors::CLR_LIGHT_GREEN;
    case VGMItem::CLR_PORTAMENTO:
      return EventColors::CLR_LIGHT_GREEN;
    case VGMItem::CLR_PORTAMENTOTIME:
      return EventColors::CLR_LIGHT_GREEN;
    case VGMItem::CLR_CHANGESTATE:
      return EventColors::CLR_GRAY;
    case VGMItem::CLR_ADSR:
      return EventColors::CLR_GRAY;
    case VGMItem::CLR_LFO:
      return EventColors::CLR_GRAY;
    case VGMItem::CLR_REVERB:
      return EventColors::CLR_GRAY;
    case VGMItem::CLR_SUSTAIN:
      return EventColors::CLR_YELLOW;
    case VGMItem::CLR_LOOP:
      return EventColors::CLR_LIGHT_RED;
    case VGMItem::CLR_LOOPFOREVER:
      return EventColors::CLR_LIGHT_RED;
    case VGMItem::CLR_TRACKEND:
      return EventColors::CLR_RED;
  }
  return EventColors::CLR_RED;
}

QColor textColorForEventColor(VGMItem::EventColor eventColor) {
    if (eventColor == VGMItem::CLR_UNKNOWN) {
      return EventColors::CLR_GRAY;
    }
    return EventColors::CLR_BG_DARK;
}

QString getFullDescriptionForTooltip(VGMItem* item) {
  QString name = QString::fromStdString(item->name());
  QString description = QString::fromStdString(item->description());

  return QString{"<nobr><h3>%1</h3>%2</nobr>"}.arg(name, description);
}
