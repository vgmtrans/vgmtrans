/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueFormatCorpus.h"
#include "value/formats/NinSnes/NinSnes.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>

void ninSnesMetalCombatRecognizesDriverWithoutInstrumentOverwrite();
void ninSnesIntelligentPercussionUsesRevisionSpecificTables();
void ninSnesIntelligentVoiceLoadingPreservesTuningAndMasksIndex();
void ninSnesIntelligentOverridesApplyOnInstrumentLoadAndDeduplicate();
void ninSnesIntelligentEchoAdsrAndGainKeepIndependentState();
void ninSnesIntelligentNoiseRowsDoNotTerminateSoundBanks();
void ninSnesIntelligentSparsePaddingDoesNotHideSongBank();
void ninSnesIntelligentSectionPreservesVoiceAndLegato();
void ninSnesProfilesDescribeEverySupportedDriverFamily();
void ninSnesKonamiClockControlsTempo();
void ninSnesScannerFindsRequestedSongAcrossSparseTable();
void ninSnesKoeiUsesSixBgmTracksAndPendingRequest();
void ninSnesProfilesShareSquaredLevelCurve();
void ninSnesProfilesShareTempoRelativeVibratoClock();
void ninSnesProfilesEmitSubtractiveTremolo();
void ninSnesStandardEchoUsesMaskLevelAndDisable();
void ninSnesKonamiLoopAppliesAndClearsReplayDeltas();
void ninSnesKonamiAdsrGainEmitsNeutralEnvelopeState();
void ninSnesNoteVelocityPreservesLegacyCurve();
void ninSnesIntelligentVoiceTablesUseTypedPlaybackState();
void ninSnesProgramResolutionIsCapturedByRuntime();
void ninSnesFe3ConditionalJumpUsesCapturedDriverState();
void ninSnesControllerFadesRemainInTheSourceDomain();
void ninSnesPrepassClearsMasterVolumeAutomationBinding();
void ninSnesPlaylistCarriesTiesAcrossSectionParserResets();
void ninSnesKonamiZeroDurationRateContinuesHeldVoice();
void ninSnesF9UsesSharedPitchTransitions();
void ninSnesPercussionStartsPerNoteVibratoFade();
void ninSnesFixedPercussionBaseIgnoresFaOperand();
void ninSnesKonamiPercussionUsesDriverMapAndNeutralTuning();
void ninSnesEarlierPercussionUsesSeparateSixByteTable();
void ninSnesGainModeInstrumentsUseDspEnvelope();
void ninSnesIdentityMappedSilentSlotsAreSparse();
void ninSnesSunsoftRecognizesBgmLayouts();
void ninSnesSunsoftCommandsPreserveEchoAndEnvelopeState();
void ninSnesSunsoftFeAndGateFollowRevision();
void ninSnesSunsoftNoiseInstrumentsPreserveLaterSamples();

int main(int argc, char** argv) {
  try {
    if (argc == 2 || argc == 3) {
      return vgmtrans::tests::scanValueFormatArchive(
          argv[1], vgmtrans::tests::ValueFormatCorpus{
                       .format = "NinSnes",
                       .exports =
                           vgmtrans::core::ExportRequest{
                               .kinds = {vgmtrans::core::ExportKind::Midi, vgmtrans::core::ExportKind::SoundFont2},
                               .sequence = {.loopPolicy = vgmtrans::core::LoopPolicy::PlayOnce, .sequenceLoops = 0},
                               .dynamicEnvelopes = vgmtrans::core::DynamicEnvelopePolicy::InstrumentVariants,
                           },
                       .outputDirectory = argc == 3 ? std::optional<std::filesystem::path>{argv[2]} : std::nullopt,
                       .requireSoundBank = true,
                   });
    }
    ninSnesMetalCombatRecognizesDriverWithoutInstrumentOverwrite();
    ninSnesIntelligentPercussionUsesRevisionSpecificTables();
    ninSnesIntelligentVoiceLoadingPreservesTuningAndMasksIndex();
    ninSnesIntelligentOverridesApplyOnInstrumentLoadAndDeduplicate();
    ninSnesIntelligentEchoAdsrAndGainKeepIndependentState();
    ninSnesIntelligentNoiseRowsDoNotTerminateSoundBanks();
    ninSnesIntelligentSparsePaddingDoesNotHideSongBank();
    ninSnesIntelligentSectionPreservesVoiceAndLegato();
    ninSnesProfilesDescribeEverySupportedDriverFamily();
    ninSnesKonamiClockControlsTempo();
    ninSnesScannerFindsRequestedSongAcrossSparseTable();
    ninSnesKoeiUsesSixBgmTracksAndPendingRequest();
    ninSnesProfilesShareSquaredLevelCurve();
    ninSnesProfilesShareTempoRelativeVibratoClock();
    ninSnesProfilesEmitSubtractiveTremolo();
    ninSnesStandardEchoUsesMaskLevelAndDisable();
    ninSnesKonamiLoopAppliesAndClearsReplayDeltas();
    ninSnesKonamiAdsrGainEmitsNeutralEnvelopeState();
    ninSnesNoteVelocityPreservesLegacyCurve();
    ninSnesIntelligentVoiceTablesUseTypedPlaybackState();
    ninSnesProgramResolutionIsCapturedByRuntime();
    ninSnesFe3ConditionalJumpUsesCapturedDriverState();
    ninSnesControllerFadesRemainInTheSourceDomain();
    ninSnesPrepassClearsMasterVolumeAutomationBinding();
    ninSnesPlaylistCarriesTiesAcrossSectionParserResets();
    ninSnesKonamiZeroDurationRateContinuesHeldVoice();
    ninSnesF9UsesSharedPitchTransitions();
    ninSnesPercussionStartsPerNoteVibratoFade();
    ninSnesFixedPercussionBaseIgnoresFaOperand();
    ninSnesKonamiPercussionUsesDriverMapAndNeutralTuning();
    ninSnesEarlierPercussionUsesSeparateSixByteTable();
    ninSnesGainModeInstrumentsUseDspEnvelope();
    ninSnesIdentityMappedSilentSlotsAreSparse();
    ninSnesSunsoftRecognizesBgmLayouts();
    ninSnesSunsoftCommandsPreserveEchoAndEnvelopeState();
    ninSnesSunsoftFeAndGateFollowRevision();
    ninSnesSunsoftNoiseInstrumentsPreserveLaterSamples();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
