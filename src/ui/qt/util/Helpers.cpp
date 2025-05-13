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

    case VGMItem::ICON_NOTE_OFF: {
      static QIcon i_gen{":/images/noteoff.svg"};
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

    case VGMItem::ICON_STARTREP: {
      static QIcon i_gen{":/images/repeat-begin.svg"};
      return i_gen;
    }

    case VGMItem::ICON_TRACK: {
      static QIcon i_gen{":/images/track.svg"};
      return i_gen;
    }

    case VGMItem::ICON_ENDREP: {
      static QIcon i_gen{":/images/repeat-end.svg"};
      return i_gen;
    }

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

    case VGMItem::ICON_LOOP: {
      static QIcon i_gen{":/images/loop.svg"};
      return i_gen;
    }

    case VGMItem::ICON_LOOP_FOREVER: {
      static QIcon i_gen{":/images/loop-forever.svg"};
      return i_gen;
    }

    case VGMItem::ICON_VOLUME: {
      static QIcon i_gen{":/images/volume.svg"};
      return i_gen;
    }

    case VGMItem::ICON_PAN: {
      static QIcon i_gen{":/images/pan.svg"};
      return i_gen;
    }

    case VGMItem::ICON_ADSR: {
      static QIcon i_gen{":/images/adsr.svg"};
      return i_gen;
    }

    case VGMItem::ICON_PITCHBEND: {
      static QIcon i_gen{":/images/pitchbend.svg"};
      return i_gen;
    }

    case VGMItem::ICON_JUMP: {
      static QIcon i_gen{":/images/jump.svg"};
      return i_gen;
    }

    default:
      break;
  }

  static auto i_gen = QIcon(":/images/file.svg");
  return i_gen;
}

QColor colorForItemType(VGMItem::Type type) {
  switch (type) {
    case VGMItem::Type::Unknown:
      return EventColors::CLR_BG_DARK;
    case VGMItem::Type::Unrecognized:
      return EventColors::CLR_RED;
    case VGMItem::Type::Header:
      return EventColors::CLR_GRAY;
    case VGMItem::Type::Misc:
      return EventColors::CLR_DARK_GRAY;
    case VGMItem::Type::Marker:
      return EventColors::CLR_DARK_GRAY;
    case VGMItem::Type::TimeSignature:
      return EventColors::CLR_ORANGE;
    case VGMItem::Type::Tempo:
      return EventColors::CLR_GREEN;
    case VGMItem::Type::ProgramChange:
      return EventColors::CLR_PERIWINKLE;
    case VGMItem::Type::BankSelect:
      return EventColors::CLR_PERIWINKLE;
    case VGMItem::Type::Transpose:
      return EventColors::CLR_DARK_GREEN;
    case VGMItem::Type::Priority:
      return EventColors::CLR_DARK_GREEN;
    case VGMItem::Type::Volume:
      return EventColors::CLR_MAGENTA;
    case VGMItem::Type::Expression:
      return EventColors::CLR_MAGENTA;
    case VGMItem::Type::Pan:
      return EventColors::CLR_ORANGE;
    case VGMItem::Type::NoteOn:
    case VGMItem::Type::DurationNote:
      return EventColors::CLR_BLUE;
    case VGMItem::Type::NoteOff:
      return EventColors::CLR_LIGHT_BLUE;
    case VGMItem::Type::Tie:
      return EventColors::CLR_LIGHT_BLUE;
    case VGMItem::Type::Rest:
      return EventColors::CLR_LIGHT_BLUE;
    case VGMItem::Type::PitchBend:
      return EventColors::CLR_GREEN;
    case VGMItem::Type::PitchBendRange:
      return EventColors::CLR_DARK_GREEN;
    case VGMItem::Type::Modulation:
      return EventColors::CLR_LIGHT_GREEN;
    case VGMItem::Type::Portamento:
      return EventColors::CLR_LIGHT_GREEN;
    case VGMItem::Type::PortamentoTime:
      return EventColors::CLR_LIGHT_GREEN;
    case VGMItem::Type::ChangeState:
      return EventColors::CLR_GRAY;
    case VGMItem::Type::Adsr:
      return EventColors::CLR_GRAY;
    case VGMItem::Type::Lfo:
      return EventColors::CLR_GRAY;
    case VGMItem::Type::Reverb:
      return EventColors::CLR_GRAY;
    case VGMItem::Type::Sustain:
      return EventColors::CLR_YELLOW;
    case VGMItem::Type::Loop:
      return EventColors::CLR_LIGHT_RED;
    case VGMItem::Type::LoopForever:
      return EventColors::CLR_LIGHT_RED;
    case VGMItem::Type::TrackEnd:
      return EventColors::CLR_RED;
  }
  return EventColors::CLR_RED;
}

QColor textColorForItemType(VGMItem::Type type) {
    if (type == VGMItem::Type::Unknown) {
      return EventColors::CLR_GRAY;
    }
    return EventColors::CLR_BG_DARK;
}

QString getFullDescriptionForTooltip(VGMItem* item) {
  QString name = QString::fromStdString(item->name());
  QString description = QString::fromStdString(item->description());

  return QString{"<nobr><h3>%1</h3>%2</nobr>"}.arg(name, description);
}
