/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ExportOptions.h"

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

namespace vgmtrans::shell {
namespace {

using namespace core;

template <typename T>
T choice(std::string_view value, std::initializer_list<std::pair<std::string_view, T>> choices) {
  std::string accepted;
  for (const auto& [name, setting] : choices) {
    if (value == name) {
      return setting;
    }
    if (!accepted.empty()) {
      accepted += ", ";
    }
    accepted += name;
  }
  throw std::invalid_argument(fmt::format("invalid value '{}'; expected {}", value, accepted));
}

u32 loopCount(std::string_view text) {
  const auto original = text;
  int base = 10;
  if (text.starts_with("0x") || text.starts_with("0X")) {
    text.remove_prefix(2);
    base = 16;
  }
  u32 value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
    throw std::invalid_argument("expected an unsigned loop count, got '" + std::string(original) + "'");
  }
  return value;
}

struct Option {
  std::string_view name;
  std::string_view values;  // Empty for a flag.
  std::string_view description;
  bool sequence;  // Accepted by standalone MIDI's SequenceExportRequest.
  void (*apply)(ExportRequest&, std::string_view);
};

// Keep parsing, supported targets, and help together.
constexpr Option options[] = {
    {"--loops", "N", "Extra sequence repeats (default: 1)", true,
     [](ExportRequest& r, std::string_view v) { r.sequence.sequenceLoops = loopCount(v); }},
    {"--bank-select", "gs|mma", "GS: CC0 only (default); MMA: CC0 + CC32", true,
     [](ExportRequest& r, std::string_view v) {
       r.sequence.midi.bankSelectStyle = choice<MidiBankSelectStyle>(
           v, {{"gs", MidiBankSelectStyle::MsbOnly}, {"mma", MidiBankSelectStyle::MsbAndLsb}});
     }},
    {"--pitch-transitions", "preserve|portamento|pitch-bend", "Pitch transition rendering (default: preserve)", true,
     [](ExportRequest& r, std::string_view v) {
       r.sequence.midi.pitchTransitions =
           choice<MidiPitchTransitionRendering>(v, {{"preserve", MidiPitchTransitionRendering::PreserveFormat},
                                                    {"portamento", MidiPitchTransitionRendering::Portamento},
                                                    {"pitch-bend", MidiPitchTransitionRendering::PitchBend}});
     }},
    {"--tuning", "pitch-bend|rpn", "Tuning via pitch bend (default) or coarse/fine tune RPN", true,
     [](ExportRequest& r, std::string_view v) {
       r.sequence.midi.tuning = choice<MidiTuningRendering>(
           v, {{"pitch-bend", MidiTuningRendering::PitchBend}, {"rpn", MidiTuningRendering::CoarseAndFineTune}});
     }},
    {"--terminate-previous-voice", "", "Terminate the previous voice on a new attack", true,
     [](ExportRequest& r, std::string_view) { r.sequence.midi.terminatePreviousVoice = true; }},
    {"--no-terminate-previous-voice", "", "Allow the previous voice to ring out (default)", true,
     [](ExportRequest& r, std::string_view) { r.sequence.midi.terminatePreviousVoice = false; }},
    {"--skip-channel-10", "", "Skip MIDI channel 10 (default)", true,
     [](ExportRequest& r, std::string_view) { r.sequence.midi.skipChannel10 = true; }},
    {"--use-channel-10", "", "Allow MIDI channel 10", true,
     [](ExportRequest& r, std::string_view) { r.sequence.midi.skipChannel10 = false; }},
    {"--modulation", "synth|events", "Synth modulators (default) or MIDI event simulation", false,
     [](ExportRequest& r, std::string_view v) {
       r.modulationConversion =
           choice<ModulationConversionPolicy>(v, {{"synth", ModulationConversionPolicy::SynthModulators},
                                                  {"events", ModulationConversionPolicy::SequenceEventSimulation}});
     }},
    {"--simulate-modulation", "", "Alias for --modulation events", false,
     [](ExportRequest& r, std::string_view) {
       r.modulationConversion = ModulationConversionPolicy::SequenceEventSimulation;
     }},
    {"--modulation-scaling", "full|observed", "Modulator range (default: full)", false,
     [](ExportRequest& r, std::string_view v) {
       r.modulationScaling =
           choice<ModulationScalingPolicy>(v, {{"full", ModulationScalingPolicy::FullFormatRange},
                                               {"observed", ModulationScalingPolicy::ObservedSequenceRange}});
     }},
    {"--dynamic-envelopes", "", "Convert dynamic envelopes to instrument variants", false,
     [](ExportRequest& r, std::string_view) { r.dynamicEnvelopes = DynamicEnvelopePolicy::InstrumentVariants; }},
    {"--no-dynamic-envelopes", "", "Ignore dynamic envelope changes (default)", false,
     [](ExportRequest& r, std::string_view) { r.dynamicEnvelopes = DynamicEnvelopePolicy::Ignore; }},
    {"--used-instruments", "", "Export only instruments used by rendered notes", false,
     [](ExportRequest& r, std::string_view) { r.exportOnlyUsedInstruments = true; }},
    {"--all-instruments", "", "Export all instrument data (shell default)", false,
     [](ExportRequest& r, std::string_view) { r.exportOnlyUsedInstruments = false; }},
    {"--sample-filter", "auto|none|snes|psx",
     "SF2/DLS filtering: format recommended (auto, default), none, SNES S-DSP, or PSX SPU low-pass", false,
     [](ExportRequest& r, std::string_view v) {
       r.sampleFiltering = choice<SampleFilteringPolicy>(v, {{"auto", SampleFilteringPolicy::FormatPreferred},
                                                             {"none", SampleFilteringPolicy::None},
                                                             {"snes", SampleFilteringPolicy::SnesDspLowPass},
                                                             {"psx", SampleFilteringPolicy::PsxSpuLowPass}});
     }},
};

}  // namespace

core::ExportKind parseExportKind(std::string_view text) {
  return choice<core::ExportKind>(text, {{"midi", core::ExportKind::Midi},
                                         {"sf2", core::ExportKind::SoundFont2},
                                         {"dls", core::ExportKind::Dls},
                                         {"wav", core::ExportKind::Wav}});
}

core::ExportRequest parseExportOptions(std::span<const std::string> args, ExportTarget target) {
  core::ExportRequest request;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    if (!arg.starts_with("--")) {
      if (target != ExportTarget::Collection) {
        throw std::invalid_argument("unexpected argument: " + std::string(arg));
      }
      const auto kinds = arg == "all" ? std::vector{core::ExportKind::Midi, core::ExportKind::SoundFont2,
                                                    core::ExportKind::Dls, core::ExportKind::Wav}
                                      : std::vector{parseExportKind(arg)};
      for (const auto kind : kinds) {
        if (std::ranges::find(request.kinds, kind) != request.kinds.end()) {
          throw std::invalid_argument("repeated export format: " + std::string(arg));
        }
        request.kinds.push_back(kind);
      }
      continue;
    }

    const size_t equals = arg.find('=');
    const auto name = arg.substr(0, equals);
    const auto option = std::ranges::find(options, name, &Option::name);
    if (option == std::end(options)) {
      throw std::invalid_argument("unknown export option: " + std::string(name));
    }
    if (target == ExportTarget::Samples) {
      throw std::invalid_argument("WAV sample export does not accept conversion options");
    }
    if (target == ExportTarget::Sequence && !option->sequence) {
      throw std::invalid_argument(std::string(name) + " requires a collection or sound bank export");
    }
    std::string_view value;
    if (!option->values.empty()) {
      if (equals != std::string_view::npos) {
        value = arg.substr(equals + 1);
      } else {
        if (i + 1 == args.size() || args[i + 1].starts_with("--")) {
          throw std::invalid_argument("missing value after " + std::string(name));
        }
        value = args[++i];
      }
    } else if (equals != std::string_view::npos) {
      throw std::invalid_argument(std::string(name) + " does not take a value");
    }
    try {
      option->apply(request, value);
    } catch (const std::invalid_argument& error) {
      throw std::invalid_argument(std::string(name) + ": " + error.what());
    }
  }
  return request;
}

void printExportOptions(std::ostream& output) {
  for (const bool sequence : {true, false}) {
    output << (sequence ? "Sequence / MIDI options:\n" : "Collection / sound bank options:\n");
    for (const auto& option : options) {
      if (option.sequence == sequence) {
        output << fmt::format("  {}{}{}\n      {}\n", option.name, option.values.empty() ? "" : " ", option.values,
                              option.description);
      }
    }
  }
  output << "Values accept --option value or --option=value. Repeated settings use the last value.\n"
            "Standalone MIDI accepts sequence options; it always simulates modulation in MIDI.\n"
            "Standalone SF2/DLS accept both groups; standalone WAV accepts no conversion options.\n"
            "Sample filtering affects SF2/DLS; WAV always exports original samples.\n"
            "Collection exports and stitching accept both groups; settings apply to the relevant outputs.\n";
}

}  // namespace vgmtrans::shell
