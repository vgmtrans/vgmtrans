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

const QIcon &iconForItemType(VGMItem::Type type) {
  switch (type) {
    case VGMItem::Type::Adsr: {
      static QIcon i_gen{":/images/adsr.svg"};
      return i_gen;
    }

    case VGMItem::Type::BankSelect: {
      static QIcon i_gen{":/images/progchange.svg"};
      return i_gen;
    }

    case VGMItem::Type::ChangeState: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::Type::Control: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::Type::DurationChange: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::Type::DurationNote: {
      static QIcon i_gen{":/images/note.svg"};
      return i_gen;
    }

    case VGMItem::Type::Expression: {
      static QIcon i_gen{":/images/volume.svg"};
      return i_gen;
    }

    case VGMItem::Type::ExpressionSlide: {
      static QIcon i_gen{":/images/volume.svg"};
      return i_gen;
    }

    case VGMItem::Type::FineTune: {
      static QIcon i_gen{":/images/pitchbend.svg"};
      return i_gen;
    }

    case VGMItem::Type::Header: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::Type::Instrument: {
      static QIcon i_gen{":/images/instr.svg"};
      return i_gen;
    }

    case VGMItem::Type::Jump: {
      static QIcon i_gen{":/images/jump.svg"};
      return i_gen;
    }

    case VGMItem::Type::JumpConditional: {
      static QIcon i_gen{":/images/jump.svg"};
      return i_gen;
    }

    case VGMItem::Type::Loop: {
      static QIcon i_gen{":/images/loop.svg"};
      return i_gen;
    }

    case VGMItem::Type::LoopBreak: {
      static QIcon i_gen{":/images/loop.svg"};
      return i_gen;
    }

    case VGMItem::Type::LoopForever: {
      static QIcon i_gen{":/images/loop-forever.svg"};
      return i_gen;
    }

    case VGMItem::Type::Lfo: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::Type::Marker: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::Type::MasterVolume: {
      static QIcon i_gen{":/images/volume.svg"};
      return i_gen;
    }

    case VGMItem::Type::MasterVolumeSlide: {
      static QIcon i_gen{":/images/volume.svg"};
      return i_gen;
    }

    case VGMItem::Type::Misc: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::Type::Modulation: {
      static QIcon i_gen{":/images/pitchbend.svg"};
      return i_gen;
    }

    case VGMItem::Type::Mute: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::Type::Noise: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::Type::Nop: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::Type::NoteOff: {
      static QIcon i_gen{":/images/noteoff.svg"};
      return i_gen;
    }

    case VGMItem::Type::NoteOn: {
      static QIcon i_gen{":/images/note.svg"};
      return i_gen;
    }

    case VGMItem::Type::Octave: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::Type::Pan: {
      static QIcon i_gen{":/images/pan.svg"};
      return i_gen;
    }

    case VGMItem::Type::PanLfo: {
      static QIcon i_gen{":/images/lfo.svg"};
      return i_gen;
    }

    case VGMItem::Type::PanSlide: {
      static QIcon i_gen{":/images/pan.svg"};
      return i_gen;
    }

    case VGMItem::Type::PanEnvelope: {
      static QIcon i_gen{":/images/pan.svg"};
      return i_gen;
    }

    case VGMItem::Type::PitchBend: {
      static QIcon i_gen{":/images/pitchbend.svg"};
      return i_gen;
    }

    case VGMItem::Type::PitchBendSlide: {
      static QIcon i_gen{":/images/pitchbend.svg"};
      return i_gen;
    }

    case VGMItem::Type::PitchBendRange: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::Type::PitchEnvelope: {
      static QIcon i_gen{":/images/adsr.svg"};
      return i_gen;
    }

    case VGMItem::Type::Portamento: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::Type::PortamentoTime: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::Type::Priority: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::Type::ProgramChange: {
      static QIcon i_gen{":/images/progchange.svg"};
      return i_gen;
    }

    case VGMItem::Type::RepeatStart: {
      static QIcon i_gen{":/images/repeat-begin.svg"};
      return i_gen;
    }

    case VGMItem::Type::RepeatEnd: {
      static QIcon i_gen{":/images/repeat-end.svg"};
      return i_gen;
    }

    case VGMItem::Type::Rest: {
      static QIcon i_gen{":/images/rest.svg"};
      return i_gen;
    }

    case VGMItem::Type::Reverb: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::Type::Sample: {
      static QIcon i_gen{":/images/sample.svg"};
      return i_gen;
    }

    case VGMItem::Type::Sustain: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::Type::Tempo: {
      static QIcon i_gen{":/images/tempo.svg"};
      return i_gen;
    }

    case VGMItem::Type::Tie: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::Type::TimeSignature: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::Type::Track: {
      static QIcon i_gen{":/images/track.svg"};
      return i_gen;
    }

    case VGMItem::Type::TrackEnd: {
      static QIcon i_gen{":/images/trackend.svg"};
      return i_gen;
    }

    case VGMItem::Type::Transpose: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::Type::Tremelo: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::Type::Unrecognized: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::Type::Unknown: {
      static QIcon i_gen{":/images/binary.svg"};
      return i_gen;
    }

    case VGMItem::Type::UseDrumKit: {
      static QIcon i_gen{":/images/progchange.svg"};
      return i_gen;
    }

    case VGMItem::Type::Vibrato: {
      static QIcon i_gen{":/images/control.svg"};
      return i_gen;
    }

    case VGMItem::Type::Volume: {
      static QIcon i_gen{":/images/volume.svg"};
      return i_gen;
    }

    case VGMItem::Type::VolumeSlide: {
      static QIcon i_gen{":/images/volume.svg"};
      return i_gen;
    }

    case VGMItem::Type::VolumeEnvelope: {
      static QIcon i_gen{":/images/volume.svg"};
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
    case VGMItem::Type::Adsr:              return EventColors::CLR_GRAY;
    case VGMItem::Type::BankSelect:        return EventColors::CLR_PERIWINKLE;
    case VGMItem::Type::ChangeState:       return EventColors::CLR_GRAY;
    case VGMItem::Type::Control:           return EventColors::CLR_GRAY;
    case VGMItem::Type::DurationChange:    return EventColors::CLR_GRAY;
    case VGMItem::Type::DurationNote:      return EventColors::CLR_BLUE;
    case VGMItem::Type::Expression:        return EventColors::CLR_MAGENTA;
    case VGMItem::Type::ExpressionSlide:   return EventColors::CLR_MAGENTA;
    case VGMItem::Type::FineTune:          return EventColors::CLR_GREEN;
    case VGMItem::Type::Header:            return EventColors::CLR_GRAY;
    case VGMItem::Type::Instrument:        return EventColors::CLR_GRAY;
    case VGMItem::Type::Jump:              return EventColors::CLR_LIGHT_RED;
    case VGMItem::Type::JumpConditional:   return EventColors::CLR_LIGHT_RED;
    case VGMItem::Type::Lfo:               return EventColors::CLR_GRAY;
    case VGMItem::Type::Loop:              return EventColors::CLR_LIGHT_RED;
    case VGMItem::Type::LoopBreak:         return EventColors::CLR_LIGHT_RED;
    case VGMItem::Type::LoopForever:       return EventColors::CLR_LIGHT_RED;
    case VGMItem::Type::Marker:            return EventColors::CLR_DARK_GRAY;
    case VGMItem::Type::MasterVolume:      return EventColors::CLR_MAGENTA;
    case VGMItem::Type::MasterVolumeSlide: return EventColors::CLR_MAGENTA;
    case VGMItem::Type::Misc:              return EventColors::CLR_DARK_GRAY;
    case VGMItem::Type::Modulation:        return EventColors::CLR_LIGHT_GREEN;
    case VGMItem::Type::Mute:              return EventColors::CLR_MAGENTA;
    case VGMItem::Type::Noise:             return EventColors::CLR_GRAY;
    case VGMItem::Type::Nop:               return EventColors::CLR_GRAY;
    case VGMItem::Type::NoteOff:           return EventColors::CLR_LIGHT_BLUE;
    case VGMItem::Type::NoteOn:            return EventColors::CLR_BLUE;
    case VGMItem::Type::Octave:            return EventColors::CLR_GRAY;
    case VGMItem::Type::Pan:               return EventColors::CLR_ORANGE;
    case VGMItem::Type::PanEnvelope:       return EventColors::CLR_ORANGE;
    case VGMItem::Type::PanLfo:            return EventColors::CLR_ORANGE;
    case VGMItem::Type::PanSlide:          return EventColors::CLR_ORANGE;
    case VGMItem::Type::PitchBend:         return EventColors::CLR_GREEN;
    case VGMItem::Type::PitchBendRange:    return EventColors::CLR_DARK_GREEN;
    case VGMItem::Type::PitchBendSlide:    return EventColors::CLR_GREEN;
    case VGMItem::Type::PitchEnvelope:     return EventColors::CLR_GREEN;
    case VGMItem::Type::Portamento:        return EventColors::CLR_LIGHT_GREEN;
    case VGMItem::Type::PortamentoTime:    return EventColors::CLR_LIGHT_GREEN;
    case VGMItem::Type::Priority:          return EventColors::CLR_DARK_GREEN;
    case VGMItem::Type::ProgramChange:     return EventColors::CLR_PERIWINKLE;
    case VGMItem::Type::RepeatEnd:         return EventColors::CLR_LIGHT_RED;
    case VGMItem::Type::RepeatStart:       return EventColors::CLR_LIGHT_RED;
    case VGMItem::Type::Rest:              return EventColors::CLR_LIGHT_BLUE;
    case VGMItem::Type::Reverb:            return EventColors::CLR_GRAY;
    case VGMItem::Type::Sample:            return EventColors::CLR_GRAY;
    case VGMItem::Type::Sustain:           return EventColors::CLR_YELLOW;
    case VGMItem::Type::Tempo:             return EventColors::CLR_GREEN;
    case VGMItem::Type::Tie:               return EventColors::CLR_LIGHT_BLUE;
    case VGMItem::Type::TimeSignature:     return EventColors::CLR_ORANGE;
    case VGMItem::Type::Track:             return EventColors::CLR_GRAY;
    case VGMItem::Type::TrackEnd:          return EventColors::CLR_RED;
    case VGMItem::Type::Transpose:         return EventColors::CLR_DARK_GREEN;
    case VGMItem::Type::Tremelo:           return EventColors::CLR_MAGENTA;
    case VGMItem::Type::Unknown:           return EventColors::CLR_MAGENTA;
    case VGMItem::Type::Unrecognized:      return EventColors::CLR_RED;
    case VGMItem::Type::UseDrumKit:        return EventColors::CLR_PERIWINKLE;
    case VGMItem::Type::Vibrato:           return EventColors::CLR_GREEN;
    case VGMItem::Type::Volume:            return EventColors::CLR_MAGENTA;
    case VGMItem::Type::VolumeEnvelope:    return EventColors::CLR_MAGENTA;
    case VGMItem::Type::VolumeSlide:       return EventColors::CLR_MAGENTA;
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
