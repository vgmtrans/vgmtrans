/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "workarea/SourceInspectorPresentation.h"

#include "util/Colors.h"
#include "util/TintableSvgIconEngine.h"

#include <QBuffer>
#include <QPixmap>

#include <array>
#include <cmath>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace SourceInspectorPresentation {

namespace {

using namespace vgmtrans::core;

QString midiNoteText(qint64 value) {
  static constexpr std::array noteNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  if (value < 0 || value > 127) {
    return QString::number(value);
  }
  const auto note = static_cast<size_t>(value % 12);
  const qint64 octave = (value / 12) - 1;
  return QStringLiteral("%1%2 (%3)").arg(QLatin1String(noteNames[note])).arg(octave).arg(value);
}

QString valueText(const SourceField& field) {
  return std::visit(
      [&field](const auto& value) -> QString {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return {};
        } else if constexpr (std::is_same_v<T, bool>) {
          return value ? QStringLiteral("true") : QStringLiteral("false");
        } else if constexpr (std::is_same_v<T, std::string>) {
          return QString::fromStdString(value);
        } else if constexpr (std::is_floating_point_v<T>) {
          const QString number = QString::number(value, 'g', 8);
          switch (field.display) {
            case SourceValueDisplay::Boolean:
              return value != 0.0 ? QStringLiteral("true") : QStringLiteral("false");
            case SourceValueDisplay::Percent:
              return QStringLiteral("%1%").arg(QString::number(value * 100.0, 'g', 8));
            case SourceValueDisplay::Cents:
              return QStringLiteral("%1 cents").arg(number);
            case SourceValueDisplay::Decibels:
              return QStringLiteral("%1 dB").arg(number);
            case SourceValueDisplay::MidiNote:
              return std::isfinite(value) ? midiNoteText(static_cast<qint64>(std::llround(value))) : number;
            default:
              return number;
          }
        } else {
          switch (field.display) {
            case SourceValueDisplay::Hex:
              return QStringLiteral("0x%1").arg(static_cast<qulonglong>(value), 0, 16);
            case SourceValueDisplay::Address:
              return QStringLiteral("0x%1").arg(static_cast<qulonglong>(value), 0, 16, QLatin1Char('0'));
            case SourceValueDisplay::Boolean:
              return value != 0 ? QStringLiteral("true") : QStringLiteral("false");
            case SourceValueDisplay::Percent:
              return QStringLiteral("%1%").arg(static_cast<qlonglong>(value));
            case SourceValueDisplay::Cents:
              return QStringLiteral("%1 cents").arg(static_cast<qlonglong>(value));
            case SourceValueDisplay::Decibels:
              return QStringLiteral("%1 dB").arg(static_cast<qlonglong>(value));
            case SourceValueDisplay::MidiNote:
              return midiNoteText(static_cast<qint64>(value));
            case SourceValueDisplay::Ascii: {
              const auto character = static_cast<qulonglong>(value);
              if (character >= 0x20 && character <= 0x7e) {
                return QStringLiteral("'%1' (%2)").arg(QChar(static_cast<char16_t>(character))).arg(character);
              }
              return QString::number(character);
            }
            default:
              if constexpr (std::is_signed_v<T>) {
                return QString::number(static_cast<qlonglong>(value));
              } else {
                return QString::number(static_cast<qulonglong>(value));
              }
          }
        }
      },
      field.value);
}

QString fieldName(std::string_view name) {
  static constexpr std::array substitutions{
      std::pair{std::string_view{"adsr"}, std::string_view{"ADSR"}},
      std::pair{std::string_view{"adsr1"}, std::string_view{"ADSR1"}},
      std::pair{std::string_view{"adsr2"}, std::string_view{"ADSR2"}},
      std::pair{std::string_view{"apu"}, std::string_view{"APU"}},
      std::pair{std::string_view{"bpm"}, std::string_view{"BPM"}},
      std::pair{std::string_view{"brr"}, std::string_view{"BRR"}},
      std::pair{std::string_view{"db"}, std::string_view{"dB"}},
      std::pair{std::string_view{"dls"}, std::string_view{"DLS"}},
      std::pair{std::string_view{"fat"}, std::string_view{"FAT"}},
      std::pair{std::string_view{"hz"}, std::string_view{"Hz"}},
      std::pair{std::string_view{"id"}, std::string_view{"ID"}},
      std::pair{std::string_view{"info"}, std::string_view{"INFO"}},
      std::pair{std::string_view{"lfo"}, std::string_view{"LFO"}},
      std::pair{std::string_view{"lsb"}, std::string_view{"LSB"}},
      std::pair{std::string_view{"midi"}, std::string_view{"MIDI"}},
      std::pair{std::string_view{"msb"}, std::string_view{"MSB"}},
      std::pair{std::string_view{"pcm"}, std::string_view{"PCM"}},
      std::pair{std::string_view{"rom"}, std::string_view{"ROM"}},
      std::pair{std::string_view{"sf2"}, std::string_view{"SF2"}},
      std::pair{std::string_view{"snes"}, std::string_view{"SNES"}},
      std::pair{std::string_view{"srcn"}, std::string_view{"SRCN"}},
      std::pair{std::string_view{"symb"}, std::string_view{"SYMB"}},
  };

  QString result;
  result.reserve(static_cast<qsizetype>(name.size()));
  while (!name.empty()) {
    const size_t separator = name.find('_');
    const std::string_view word = name.substr(0, separator);
    if (!word.empty()) {
      if (!result.isEmpty()) {
        result.append(QLatin1Char(' '));
      }
      bool substituted = false;
      for (const auto& [source, display] : substitutions) {
        if (word == source) {
          result.append(QString::fromLatin1(display.data(), static_cast<qsizetype>(display.size())));
          substituted = true;
          break;
        }
      }
      if (!substituted) {
        result.append(QChar::fromLatin1(word.front()).toUpper());
        result.append(QString::fromLatin1(word.data() + 1, static_cast<qsizetype>(word.size() - 1)));
      }
    }
    if (separator == std::string_view::npos) {
      break;
    }
    name.remove_prefix(separator + 1);
  }
  return result;
}

QString iconPath(const SourceAnnotation& annotation) {
  if (annotation.sequenceSemantic) {
    switch (*annotation.sequenceSemantic) {
      case SequenceSemantic::Note:
        return QStringLiteral(":/icons/note.svg");
      case SequenceSemantic::Program:
      case SequenceSemantic::Instrument:
        return QStringLiteral(":/icons/progchange.svg");
      case SequenceSemantic::Level:
        return QStringLiteral(":/icons/volume.svg");
      case SequenceSemantic::Pan:
        return QStringLiteral(":/icons/pan.svg");
      case SequenceSemantic::Pitch:
      case SequenceSemantic::Portamento:
        return QStringLiteral(":/icons/pitchbend.svg");
      default:
        break;
    }
  }
  switch (annotation.role) {
    case SourceRole::Sequence:
    case SourceRole::SequenceTrack:
      return QStringLiteral(":/icons/sequence.svg");
    case SourceRole::InstrumentSet:
      return QStringLiteral(":/icons/instrument-set.svg");
    case SourceRole::Instrument:
    case SourceRole::Region:
      return QStringLiteral(":/icons/instr.svg");
    case SourceRole::SampleCollection:
    case SourceRole::Sample:
      return QStringLiteral(":/icons/sample-collection.svg");
    default:
      return QStringLiteral(":/icons/binary.svg");
  }
}

QString escaped(QString text) {
  return text.toHtmlEscaped();
}

}  // namespace

QColor color(const SourceAnnotation& annotation) {
  if (annotation.sequenceSemantic) {
    switch (*annotation.sequenceSemantic) {
      case SequenceSemantic::Note:
        return EventColors::CLR_BLUE;
      case SequenceSemantic::Rest:
      case SequenceSemantic::Wait:
        return EventColors::CLR_LIGHT_BLUE;
      case SequenceSemantic::Program:
      case SequenceSemantic::Instrument:
        return EventColors::CLR_PERIWINKLE;
      case SequenceSemantic::Level:
        return EventColors::CLR_MAGENTA;
      case SequenceSemantic::Pan:
        return EventColors::CLR_ORANGE;
      case SequenceSemantic::Pitch:
      case SequenceSemantic::Tempo:
        return EventColors::CLR_GREEN;
      case SequenceSemantic::Modulation:
      case SequenceSemantic::Portamento:
        return EventColors::CLR_LIGHT_GREEN;
      case SequenceSemantic::Jump:
      case SequenceSemantic::Call:
      case SequenceSemantic::Return:
      case SequenceSemantic::Loop:
      case SequenceSemantic::Repeat:
      case SequenceSemantic::RepeatBreak:
        return EventColors::CLR_LIGHT_RED;
      case SequenceSemantic::End:
      case SequenceSemantic::Unsupported:
        return EventColors::CLR_RED;
      default:
        break;
    }
  }

  switch (annotation.role) {
    case SourceRole::Pointer:
      return EventColors::CLR_LIGHT_RED;
    case SourceRole::Opcode:
      return EventColors::CLR_PERIWINKLE;
    case SourceRole::Operand:
    case SourceRole::Field:
      return EventColors::CLR_LIGHTER_GREEN;
    case SourceRole::Padding:
      return EventColors::CLR_DARK_GRAY;
    case SourceRole::Payload:
    case SourceRole::DataBlock:
    case SourceRole::Sample:
      return EventColors::CLR_GRAY;
    case SourceRole::Unknown:
      return EventColors::CLR_BG_DARK;
    default:
      return EventColors::CLR_GRAY;
  }
}

QColor textColor(const SourceAnnotation& annotation) {
  return annotation.role == SourceRole::Unknown ? EventColors::CLR_GRAY : EventColors::CLR_BG_DARK;
}

QIcon icon(const SourceAnnotation& annotation) {
  return QIcon(new TintableSvgIconEngine(iconPath(annotation), color(annotation)));
}

CapsuleText description(const SourceAnnotation& annotation) {
  CapsuleText text{.prefix = QString::fromStdString(annotation.description)};
  if (!annotation.fields.empty()) {
    text.capsules.reserve(static_cast<qsizetype>(annotation.fields.size()));
    for (const auto& field : annotation.fields) {
      if (annotation.role == SourceRole::Command && field.name == "opcode") {
        continue;
      }
      text.capsules.push_back(
          QStringLiteral("%1: %2").arg(fieldName(field.name), valueText(field)));
    }
  }
  return text;
}

QString tooltipHtml(const SourceAnnotation& annotation) {
  const QIcon itemIcon = icon(annotation);
  const QPixmap pixmap = itemIcon.pixmap(QSize(16, 16));
  QString iconData;
  if (!pixmap.isNull()) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    pixmap.toImage().save(&buffer, "PNG");
    iconData = QStringLiteral("data:image/png;base64,%1").arg(QString::fromLatin1(bytes.toBase64()));
  }

  const QString name = escaped(QString::fromStdString(annotation.label));
  const QString detail = escaped(description(annotation).plainText());
  QString body = QStringLiteral("<nobr><h3>%1</h3>%2</nobr>").arg(name, detail);
  if (!iconData.isEmpty()) {
    body = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\"><tr>"
                          "<td style=\"padding-right:6px; vertical-align:top;\">"
                          "<img src=\"%1\" width=\"16\" height=\"16\"></td>"
                          "<td>%2</td></tr></table>")
               .arg(iconData, body);
  }
  return QStringLiteral("<table cellspacing=\"0\" cellpadding=\"6\"><tr><td>%1</td></tr></table>").arg(body);
}

}  // namespace SourceInspectorPresentation
