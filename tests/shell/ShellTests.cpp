/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "commands.h"
#include "ExportOptions.h"
#include "value/export/CollectionStitch.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace {

using namespace vgmtrans::core;
using namespace vgmtrans::shell;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct TemporaryDirectory {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("vgmtrans-shell-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

  TemporaryDirectory() { std::filesystem::create_directories(path); }
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

std::vector<u8> readFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  expect(file.good(), "missing output file: " + path.string());
  return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

// The test format uses the real Session admission, inspection, VM and exporters.
// Its tiny source contains a note, a loop, and four PCM samples; no game data.
ScanResult scanProbe(const ScanInput& input, bool emptyBank) {
  if (input.reader.size() != 10 || input.reader.u8At(0) != 0x7f) {
    return {};
  }
  ScanResultBuilder result(input, "Shell Probe");
  auto sequence = result.sequence("Probe sequence", input.reader.range(0, 1));
  const auto root = result.sourceMap()
                        .annotation(SourceRole::Sequence, "Sequence", input.reader.range(0, 1))
                        .owner(ObjectRefs::sequence(sequence.id()));
  const auto note = result.sourceMap()
                        .command("Note", input.reader.range(0, 1), SequenceSemantic::Note)
                        .parent(root.id())
                        .kind("probe.note")
                        .field("key", input.reader.range(0, 1), 60, SourceValueDisplay::Hex);
  const auto loop = result.sourceMap()
                        .command("Loop", input.reader.range(1, 1), SequenceSemantic::Loop)
                        .parent(root.id())
                        .kind("probe.loop");
  SequenceProgram program{
      .runtime = {.execute =
                      [](const SourceCommand& command, std::any&, std::any&, PerformanceEmitter& out, VmApi&) {
                        if (command.address.value == 0) {
                          out.instrument(3, 7);
                          out.tuning(35);
                          const auto voice = out.note(60, 1.0, 12);
                          out.pitchSlide(voice, 60, 64, 4).preferPortamento();
                          return Effects::wait(12);
                        }
                        return Effects{};
                      }},
      .tracks = {{.sourceTrackNumber = 3,
                  .commands =
                      {
                          {.opcode = 0x7f,
                           .address = Address{0},
                           .range = input.reader.range(0, 1),
                           .annotation = note.id(),
                           .semantic = SequenceSemantic::Note,
                           .flow = CommandFlow::fallthroughTo(Address{1})},
                          {.opcode = 0xfe,
                           .address = Address{1},
                           .range = input.reader.range(1, 1),
                           .annotation = loop.id(),
                           .semantic = SequenceSemantic::Loop,
                           .flow = CommandFlow::jumpTo(Address{0}, Address{2}, JumpSemantics::DeclaredLoop)},
                      }}},
  };
  sequence.program(std::move(program));
  auto bank = result.soundBank("Probe bank", input.reader.range(2, 8));
  const auto sample = bank.localSamples().add(0, Sample{
                                                     .name = "Wave",
                                                     .codec = AudioCodec::PcmS16,
                                                     .encodedData = input.reader.range(2, 8),
                                                     .sampleRate = 8000,
                                                 });
  if (!emptyBank) {
    bank.instruments()
        .append(Instrument{.explicitAddress = InstrumentAddress{.bank = 3, .program = 7}, .name = "Instrument"})
        .region(sample.ref(), Region{.range = input.reader.range(2, 8)});
  }
  result.sourceCollection("Same title").sequence(sequence).soundBank(bank);
  result.warning("Probe warning", input.reader.range(0, 1));
  return result.finish();
}

struct Fixture {
  Session session;
  std::ostringstream output;
  std::ostringstream errors;
  size_t scans = 0;

  explicit Fixture(bool emptyBank = false) {
    session.registerExtractor(SourceExtractor{
        .name = "Probe container",
        .extract = [](const ExtractionInput& input) -> ExtractionResult {
          if (input.source.derived() || input.reader.empty() || input.reader.u8At(0) != 0x7f) {
            return {};
          }
          const auto bytes = input.reader.slice(0, input.reader.size());
          return {.sources = {{.file = {.name = input.source.name + ".child",
                                        .origin = input.reader.range(0, input.reader.size())},
                               .bytes = {bytes.begin(), bytes.end()}}}};
        },
    });
    session.registerFormat(FormatModule{.name = "Shell Probe", .scan = [this, emptyBank](const ScanInput& input) {
                                          ++scans;
                                          return scanProbe(input, emptyBank);
                                        }});
  }

  CommandResult run(std::string_view command) {
    output.str({});
    errors.str({});
    return executeLine(session, command, output, errors);
  }

  void ok(std::string_view command) {
    expect(run(command) == CommandResult::Success, std::string(command) + ": " + errors.str());
  }

  void fails(std::string_view command) {
    expect(run(command) == CommandResult::Error, "command should fail: " + std::string(command));
    expect(!errors.str().empty(), "failed commands should explain the failure on stderr");
  }

  void seed() {
    const auto id = session.addSource(SourceFile{.name = "probe"}, {0x7f, 0xfe, 0, 0x40, 0, 0xc0, 0, 0x40, 0, 0xc0});
    session.scanSource(id);
    expect(session.snapshot().assets().size() >= 2, "probe should admit a sequence and bank");
  }
};

void persistentSessionAndStableIds() {
  Fixture fixture;
  fixture.seed();
  const auto first = fixture.session.snapshot();
  const auto sequenceId = metadata(first.assets()[0]).id;
  const auto bankId = metadata(first.assets()[1]).id;
  fixture.ok("sources");
  expect(fixture.output.str().find("parent 0") != std::string::npos, "derived sources should expose their parent");
  fixture.ok("assets 0");
  expect(fixture.output.str().find("source track 3") != std::string::npos, "sequence inspection should list tracks");
  fixture.ok("tree 0 1");
  expect(fixture.output.str().find("Loop") != std::string::npos,
         "inspection should include bytes beyond asset metadata");
  expect(fixture.output.str().find("key: 0x3c") != std::string::npos, "inspection should honor field display hints");
  fixture.ok("tree 0 0");
  expect(fixture.output.str().find("Note") == std::string::npos, "tree depth should bound traversal");
  fixture.ok("events 0 0 1");
  expect(fixture.output.str().find("opcode=0x7f") != std::string::npos &&
             fixture.output.str().find("1 more commands") != std::string::npos,
         "events should honor the limit");
  fixture.ok("instruments 1 0");
  expect(fixture.output.str().find("sample 1:0") != std::string::npos, "regions should use sample owner IDs");
  fixture.ok("samples 1 0");
  expect(fixture.output.str().find("pcm-s16") != std::string::npos, "samples should expose codec names");
  fixture.ok("read 1 0x0 2");
  expect(fixture.output.str().find("7f fe") != std::string::npos, "read should access derived source bytes");
  fixture.ok("collections");
  fixture.ok("diagnostics");
  expect(fixture.output.str().find("Probe warning") != std::string::npos, "diagnostics should remain inspectable");
  expect(fixture.scans == 1, "read-only commands must reuse the scan");

  fixture.seed();
  const auto second = fixture.session.snapshot();
  const auto survivingId = metadata(second.assets()[2]).id;
  fixture.fails("remove source 0 999");
  expect(fixture.session.snapshot().assets().size() == 4, "validate every removal ID before mutating");
  fixture.ok("remove source 0");
  const auto remaining = fixture.session.snapshot();
  expect(remaining.sources().size() == 2 && remaining.assets().size() == 2 && remaining.collections().size() == 1,
         "source removal should remove its entire family and reconcile collections");
  expect(remaining.asset(sequenceId) == nullptr && remaining.asset(bankId) == nullptr &&
             remaining.asset(survivingId) != nullptr,
         "remaining asset IDs must not shift");
  fixture.ok("assets " + std::to_string(survivingId.value));
  expect(fixture.scans == 2, "loading another file should not rescan earlier files");
  expect(first.assets().size() == 2, "previous snapshots should remain valid");
  fixture.ok("remove asset 2 3");
  expect(fixture.session.snapshot().sources().empty(), "removing all assets should release their sources");
}

void commandValidationAndCollections() {
  Fixture fixture;
  fixture.seed();
  for (const auto* command : {"read 1 -1 1",
                              "read 1 0 18446744073709551616",
                              "read 1 9 2",
                              "assets 0junk",
                              "assets 4294967296",
                              "assets -0",
                              "assets 0x",
                              "assets ''",
                              "assets 4294967295",
                              "sources 99",
                              "tree 0 -1",
                              "events 1 0",
                              "events 0 1",
                              "samples 0",
                              "samples 1 1",
                              "instruments 1 -1",
                              "load",
                              "quit extra",
                              "help unknown",
                              "collections 0 extra",
                              "unknown",
                              "create 'unterminated",
                              "create '' 0 1",
                              "create invalid 0 0 1",
                              "create invalid 0 1 1"}) {
    fixture.fails(command);
  }
  expect(fixture.session.snapshot().collections().size() == 1, "invalid commands must not create collections");
  fixture.ok(" \tassets\t0\r");
  fixture.ok("create \"C:\\Music\\mix\"' tape' 0 1");
  const auto snapshot = fixture.session.snapshot();
  expect(snapshot.collections().size() == 2 && snapshot.collections().back().name == "C:\\Music\\mix tape",
         "quoted fragments should combine and preserve literal backslashes");
  expect(snapshot.collections().back().members.sequence == AssetId{0} &&
             snapshot.collections().back().members.soundBanks == std::vector{AssetId{1}},
         "manual collections should use the core's typed members");
  expect(fixture.run("quit") == CommandResult::Exit, "quit should return an exit result");
  fixture.ok(" \t");
  expect(completeCommand("exp") == std::vector<std::string>{"export", "export-asset"},
         "completion should use the command definitions");
}

void loadingAndErrors(const std::filesystem::path& directory) {
  const auto source = directory / "source with spaces.bin";
  {
    std::ofstream file(source, std::ios::binary);
    const std::array<u8, 10> bytes{0x7f, 0xfe};
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  const auto unknown = directory / "unknown.bin";
  std::ofstream(unknown) << "not music";
  Fixture fixture;
  const std::vector<std::string> args{"load", (directory / "missing.bin").string(), source.string()};
  expect(execute(fixture.session, args, fixture.output, fixture.errors) == CommandResult::Error,
         "partial load failure must be reported");
  expect(fixture.session.snapshot().assets().size() == 2, "other files should still load after a failure");
  expect(fixture.errors.str().find("Probe warning") != std::string::npos, "load should report scanner warnings");
  fixture.fails("load '" + unknown.string() + "'");
  expect(fixture.errors.str().find("no supported music") != std::string::npos, "unrecognized input should be reported");
  fixture.ok("load '" + source.string() + "'");
  expect(fixture.session.snapshot().collections().size() == 2, "load should add to the existing session");
  fixture.ok("dump 1 '" + (directory / "dump.bin").string() + "'");
  expect(readFile(directory / "dump.bin") == readFile(source), "dump should preserve source bytes");
}

void exportOptionParsing() {
  const auto parse = [](std::initializer_list<std::string> args, ExportTarget target = ExportTarget::Collection) {
    return parseExportOptions(std::vector<std::string>(args), target);
  };
  const auto custom =
      parse({"sf2", "--loops=0x2", "--bank-select", "mma", "--pitch-transitions=pitch-bend", "--tuning", "rpn",
             "--terminate-previous-voice", "--use-channel-10", "--modulation=events", "--modulation-scaling",
             "observed", "--dynamic-envelopes", "--used-instruments", "--sample-filter=psx", "midi"});
  expect(custom.kinds == std::vector{ExportKind::SoundFont2, ExportKind::Midi} && custom.sequence.sequenceLoops == 2,
         "formats and spaced/equals options should combine in command order");
  expect(custom.sequence.midi.bankSelectStyle == MidiBankSelectStyle::MsbAndLsb &&
             custom.sequence.midi.pitchTransitions == MidiPitchTransitionRendering::PitchBend &&
             custom.sequence.midi.tuning == MidiTuningRendering::CoarseAndFineTune &&
             custom.sequence.midi.terminatePreviousVoice && !custom.sequence.midi.skipChannel10,
         "MIDI options must select the corresponding core policies");
  expect(custom.modulationConversion == ModulationConversionPolicy::SequenceEventSimulation &&
             custom.modulationScaling == ModulationScalingPolicy::ObservedSequenceRange &&
             custom.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants && custom.exportOnlyUsedInstruments &&
             custom.sampleFiltering == SampleFilteringPolicy::PsxSpuLowPass,
         "sound bank options must select the corresponding core policies");
  const auto reset = parse({"--loops",
                            "2",
                            "--loops=1",
                            "--bank-select=mma",
                            "--bank-select=gs",
                            "--pitch-transitions=portamento",
                            "--pitch-transitions=preserve",
                            "--tuning=rpn",
                            "--tuning=pitch-bend",
                            "--terminate-previous-voice",
                            "--no-terminate-previous-voice",
                            "--use-channel-10",
                            "--skip-channel-10",
                            "--simulate-modulation",
                            "--modulation=synth",
                            "--modulation-scaling=observed",
                            "--modulation-scaling=full",
                            "--no-dynamic-envelopes",
                            "--dynamic-envelopes",
                            "--used-instruments",
                            "--all-instruments",
                            "--sample-filter=snes",
                            "--sample-filter=auto"});
  const auto defaults = parse({});
  expect(defaults.dynamicEnvelopes == DynamicEnvelopePolicy::InstrumentVariants &&
             parse({"--no-dynamic-envelopes"}).dynamicEnvelopes == DynamicEnvelopePolicy::Ignore,
         "dynamic envelope conversion should default to enabled and allow opting out");
  expect(reset.sequence.sequenceLoops == defaults.sequence.sequenceLoops &&
             reset.sequence.midi.bankSelectStyle == defaults.sequence.midi.bankSelectStyle &&
             reset.sequence.midi.pitchTransitions == defaults.sequence.midi.pitchTransitions &&
             reset.sequence.midi.tuning == defaults.sequence.midi.tuning &&
             reset.sequence.midi.terminatePreviousVoice == defaults.sequence.midi.terminatePreviousVoice &&
             reset.sequence.midi.skipChannel10 == defaults.sequence.midi.skipChannel10 &&
             reset.modulationConversion == defaults.modulationConversion &&
             reset.modulationScaling == defaults.modulationScaling &&
             reset.dynamicEnvelopes == defaults.dynamicEnvelopes &&
             reset.exportOnlyUsedInstruments == defaults.exportOnlyUsedInstruments &&
             reset.sampleFiltering == defaults.sampleFiltering && defaults.kinds.empty(),
         "explicit defaults should restore core defaults, with the last setting winning");
  expect(
      parse({"--pitch-transitions=portamento"}, ExportTarget::Sequence).sequence.midi.pitchTransitions ==
              MidiPitchTransitionRendering::Portamento &&
          parse({"--sample-filter=none"}, ExportTarget::SoundBank).sampleFiltering == SampleFilteringPolicy::None &&
          parse({"--sample-filter=snes"}, ExportTarget::Stitch).sampleFiltering ==
              SampleFilteringPolicy::SnesDspLowPass &&
          parse({"--simulate-modulation"}).modulationConversion == ModulationConversionPolicy::SequenceEventSimulation,
      "all rendering choices and the existing modulation alias should remain accessible");

  for (const auto& args : std::initializer_list<std::vector<std::string>>{{"--loops"},
                                                                          {"--loops="},
                                                                          {"--loops=-1"},
                                                                          {"--loops=4294967296"},
                                                                          {"--loops=2junk"},
                                                                          {"--bank-select", "--tuning=rpn"},
                                                                          {"--bank-select=bad"},
                                                                          {"--pitch-transitions=bad"},
                                                                          {"--tuning=bad"},
                                                                          {"--modulation=bad"},
                                                                          {"--modulation-scaling=bad"},
                                                                          {"--sample-filter=bad"},
                                                                          {"--used-instruments=false"},
                                                                          {"--unknown"},
                                                                          {"all", "sf2"}}) {
    bool rejected = false;
    try {
      (void)parseExportOptions(args, ExportTarget::Collection);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    expect(rejected, "invalid options should fail: " + args.front());
  }
}

void exportOptionsReachCore(const std::filesystem::path& directory) {
  Fixture fixture;
  fixture.seed();
  const auto exportDir = directory / "options";
  const auto destination = "'" + exportDir.string() + "'";
  ExportRequest request{.kinds = {ExportKind::Midi, ExportKind::SoundFont2, ExportKind::Dls}};
  request.sequence.sequenceLoops = 0;
  request.sequence.midi.bankSelectStyle = MidiBankSelectStyle::MsbAndLsb;
  request.sequence.midi.pitchTransitions = MidiPitchTransitionRendering::PitchBend;
  request.sequence.midi.tuning = MidiTuningRendering::CoarseAndFineTune;
  request.sequence.midi.skipChannel10 = false;
  request.sequence.midi.terminatePreviousVoice = true;
  request.sampleFiltering = SampleFilteringPolicy::SnesDspLowPass;
  request.exportOnlyUsedInstruments = true;
  const std::string sequenceOptions = " --loops=0 --bank-select mma --pitch-transitions pitch-bend --tuning=rpn"
                                      " --use-channel-10 --terminate-previous-voice";
  const std::string options = sequenceOptions + " --sample-filter snes --used-instruments";
  fixture.ok("export 0 " + destination + " midi sf2 dls" + options);
  const auto expected = fixture.session.exportCollection(CollectionId{0}, request);
  const auto defaults = fixture.session.exportCollection(CollectionId{0}, ExportRequest{.kinds = request.kinds});
  expect(expected.size() == 3 && defaults.size() == 3, "probe should export MIDI, SF2, and DLS");
  for (size_t i = 0; i < expected.size(); ++i) {
    expect(!expected[i].bytes.empty() && readFile(exportDir / expected[i].filename) == expected[i].bytes &&
               expected[i].bytes != defaults[i].bytes,
           "collection options should change the exported artifacts exactly as the core does");
  }
  fixture.ok("export-asset 0 " + destination + " midi" + sequenceOptions);
  const auto midi = fixture.session.exportSequenceMidi(AssetId{0}, request.sequence);
  expect(readFile(exportDir / midi.filename) == midi.bytes, "standalone MIDI should honor sequence options");
  // Isolate pitch rendering: a tuning or loop change must not hide a dropped pitch option.
  fixture.ok("export-asset 0 " + destination + " midi --pitch-transitions=pitch-bend");
  const auto pitchBend = readFile(exportDir / midi.filename);
  fixture.ok("export-asset 0 " + destination + " midi --pitch-transitions=portamento");
  expect(readFile(exportDir / midi.filename) != pitchBend, "pitch transition choices should change MIDI output");
  for (const auto& [name, format] :
       {std::pair{"sf2", SynthExportFormat::SoundFont2}, std::pair{"dls", SynthExportFormat::Dls}}) {
    fixture.ok("export-asset 1 " + destination + " " + name + options);
    const auto bank = fixture.session.exportSoundBank(AssetId{1}, format, request);
    expect(readFile(exportDir / bank.filename) == bank.bytes, "standalone banks should honor conversion options");
  }
  fixture.seed();
  fixture.ok("stitch " + destination + " 1 0" + options);
  const auto stitched = fixture.session.stitchCollections(std::array{CollectionId{1}, CollectionId{0}}, request);
  expect(stitched.complete() && readFile(exportDir / stitched.midi.filename) == stitched.midi.bytes &&
             readFile(exportDir / stitched.soundFont.filename) == stitched.soundFont.bytes,
         "stitching should honor the same conversion options");
  const auto rejectedDir = directory / "unsupported-options";
  const auto rejected = "'" + rejectedDir.string() + "'";
  fixture.fails("export-asset 0 " + rejected + " midi --sample-filter=snes");
  expect(fixture.errors.str().find("requires a collection or sound bank export") != std::string::npos,
         "standalone MIDI should explain unsupported sound bank options");
  fixture.fails("export-asset 1 " + rejected + " wav --sample-filter=none");
  fixture.fails("export-asset 1 " + rejected + " sf2 midi");
  fixture.fails("stitch " + rejected + " 0 --loops 0 sf2");
  expect(!std::filesystem::exists(rejectedDir), "unsupported options must fail before writing artifacts");
}

void exportsUseCoreArtifacts(const std::filesystem::path& directory) {
  Fixture fixture;
  fixture.seed();
  const auto exportDir = directory / "export with spaces";
  const std::string destination = "'" + exportDir.string() + "'";
  const ExportRequest request{.kinds = {ExportKind::Midi, ExportKind::SoundFont2, ExportKind::Dls, ExportKind::Wav}};
  fixture.ok("export 0 " + destination + " all");
  const auto expected = fixture.session.exportCollection(CollectionId{0}, request);
  expect(expected.size() == 4, "probe should export MIDI, SF2, DLS and WAV");
  for (const auto& artifact : expected) {
    expect(!artifact.bytes.empty(), "core export should produce bytes");
    expect(readFile(exportDir / artifact.filename) == artifact.bytes, "shell must write core artifacts unchanged");
  }
  for (const auto& [id, format] :
       std::array<std::pair<int, const char*>, 4>{{{0, "midi"}, {1, "sf2"}, {1, "dls"}, {1, "wav"}}}) {
    fixture.ok("export-asset " + std::to_string(id) + " " + destination + " " + format);
  }
  fixture.ok("export 0 " + destination + " midi --loops 0");
  const auto once = fixture.session.exportCollection(CollectionId{0}, ExportRequest{.sequence = {.sequenceLoops = 0}});
  expect(readFile(exportDir / once[0].filename) == once[0].bytes && once[0].bytes != expected[0].bytes,
         "loop options must reach the core renderer");
  fixture.seed();
  fixture.ok("export all " + destination + " midi");
  expect(readFile(exportDir / "collection-0" / expected[0].filename) == expected[0].bytes &&
             readFile(exportDir / "collection-1" / expected[0].filename) == expected[0].bytes,
         "export all should separate duplicate collection titles");
  fixture.ok("stitch " + destination + " 1 0 --loops 0");
  const std::array ids{CollectionId{1}, CollectionId{0}};
  const auto stitched = fixture.session.stitchCollections(ids, ExportRequest{.sequence = {.sequenceLoops = 0}});
  expect(stitched.complete() && readFile(exportDir / stitched.midi.filename) == stitched.midi.bytes &&
             readFile(exportDir / stitched.soundFont.filename) == stitched.soundFont.bytes,
         "stitch should write the core's ordered MIDI/SF2 pair");
  expect(fixture.scans == 2, "exports must not rescan source data");

  const auto rejectedDir = directory / "rejected";
  const std::string rejected = "'" + rejectedDir.string() + "'";
  for (const auto& option :
       {"--loops", "--loops -1", "--modulation-scaling", "--modulation-scaling bad", "--unknown", "midi midi"}) {
    fixture.fails("export all " + rejected + " " + option);
  }
  expect(!std::filesystem::exists(rejectedDir), "invalid export options must be rejected before writing");
  fixture.fails("export-asset 1 " + rejected + " midi");
  expect(!std::filesystem::exists(rejectedDir), "failed exports must not write empty placeholder files");
  const auto blocked = directory / "file-not-directory";
  std::ofstream(blocked) << "preserve";
  fixture.fails("export 0 '" + blocked.string() + "' all");
  expect(readFile(blocked) == std::vector<u8>({'p', 'r', 'e', 's', 'e', 'r', 'v', 'e'}),
         "an output error must preserve the blocking file");

  Fixture partial(true);
  partial.seed();
  const auto partialDir = directory / "partial";
  partial.fails("export 0 '" + partialDir.string() + "' midi sf2");
  expect(!readFile(partialDir / expected[0].filename).empty(),
         "a partial export should save successful artifacts while returning failure");
  expect(!std::filesystem::exists(partialDir / expected[1].filename),
         "a partial export must not write empty artifacts");
}

}  // namespace

int main() {
  try {
    TemporaryDirectory directory;
    persistentSessionAndStableIds();
    commandValidationAndCollections();
    loadingAndErrors(directory.path);
    exportOptionParsing();
    exportOptionsReachCore(directory.path);
    exportsUseCoreArtifacts(directory.path);
    std::cout << "Shell integration tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
