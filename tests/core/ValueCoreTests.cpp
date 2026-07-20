/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <exception>
#include <iostream>

void runValueRegistryTests();
void runValueCompilerCursorTests();
void runValueSequenceModelTests();
void runValueSequenceVmTests();
void runValueSessionTests();
void runValueSourceMapTests();
void runValueMidiTests();
void runValueSynthExportTests();

void capcomSnesLayoutSelectsSongListAndFixedHeaders();
void capcomSnesLayoutFallsBackToV2SongList();
void capcomSnesLayoutReadsOldAndRejectsMalformedDspInit();
void capcomSnesModuleDiscoversSequenceInstrumentsAndSamples();
void capcomSnesModuleWarnsWhenDetectedSynthIsEmpty();
void capcomSnesCompiledAndPerformanceSnapshotsAreStable();
void capcomSnesLfoValuesAreResolvedDuringDecode();
void capcomSnesCompiledCommandsDoNotNeedEngineProfile();
void capcomSnesModuleScansSpcThroughVirtualAramSource();
void capcomSnesInstrumentTableSkipsBlankSlotsLikeLegacy();
void capcomSnesNoteStateCommandsAreTypedAndInterpreted();
void capcomSnesSourceDialectDecodesAndRendersDriverCommands();
void capcomSnesInitialDurationRateIsFullLength();
void capcomSnesPanPerformanceCarriesGainCompensation();
void capcomSnesDialectEmitsSourceOnlyDriverSemantics();
void capcomSnesDialectEmitsPortamentoFromPreviousSourceKey();
void capcomSnesDialectExecutesRepeatUntilCommand();
void capcomSnesDialectAppliesRepeatBreakAttributesOnlyWhenBranchIsTaken();
void capcomSnesDialectDecodesRepeatBreakSideTargets();
void capcomSnesV1DialectPreservesUnknownOneByteEvents();
void konamiSnesLayoutDiscoversDirectHeaderAndSynthTables();
void konamiSnesLayoutInfersSpcDirFromInstrumentTables();
void konamiSnesModuleDiscoversSequenceInstrumentsAndSamples();
void konamiSnesSynthParsersStopAtInvalidBankedInstrument();
void konamiSnesProgramChangeReemitsCurrentFineTune();
void konamiSnesLegacyObservedVibratoRateUsesGlobalTempoCeiling();
void konamiSnesPercussionUsesPackedGsDrumBank();
void konamiSnesCompilerCursorDecodesVersionedFlowAndTruncation();
void konamiSnesCompilerCursorUsesVersionedOperandLengths();
void konamiSnesEveryVersionRendersSourceFreeCommands();
void konamiSnesSequenceSimulationPreservesDriverVibratoDepth();
void konamiSnesCompiledPlaybackHandlesCallsLoopsTiesAndSlides();
void konamiSnesCompiledAutomationTicksFades();
void konamiSnesPlayOnceCoordinatesGlobalLoopCompletion();
void akaoSnesLayoutDiscoversFf4StyleAram();
void akaoSnesModuleDiscoversSequenceInstrumentsAndSamples();
void akaoSnesV4TieExtendsShortenedPreviousNote();
void akaoDialectDecodesLegacyRelativeJumpTargets();
void akaoDialectDecodesConditionalBranchSideTargets();
void akaoSequenceAnalysisUsesSourceAnnotations();
void akaoTablePointersUseNonControlSourceLinks();
void akaoDialectDecodesRepeatFlowWithoutManualLayerLeaks();
void akaoRepeatSourceLinksUseSpecificRolesOnly();
void akaoVersion10OverlayCommandsUseLegacyLengthsAndProgramChange();
void akaoLoopBranchUsesCurrentRepeatPass();
void akaoTieAfterRestDoesNotExtendPreviousNote();
void akaoTempoFadeEmitsDriverTickRamp();
void akaoRequiredArticulationsComeFromInstrumentRows();
void akaoMelodicRegionsDropAdvancingOverlaps();
void akaoSampleSelectionKeepsPreferredAndRequiredCollections();
void akaoScanMaterializesInstrumentSetWithoutProvisionalAsset();
void ndsLayoutResolvesNamesFilesAndDependencies();
void ndsLayoutBoundsMalformedTablesAndPointers();
void ndsSequenceFatRangesHandleNormalEmptyAndRecoveredFiles();
void ndsModuleOnlyBuildsDependenciesOfReferencedBanks();
void ndsSequenceDialectDecodesAndRendersNoteWaitCommands();
void ndsSequenceDialectComposesPitchBendRangeActions();
void ndsSequenceDialectExecutesCallAndReturn();
void ndsSequenceDialectDiscoversSecondaryTrackAddresses();
void ndsSequenceTrackAddressDiscoveryKeepsMalformedBootstrapCommands();
void ndsSequenceDialectAnnotatesIgnoredOperandBytes();
void ndsSequenceDialectAnnotatesPartialIgnoredOperandBytes();
void ndsSequenceDialectKeepsEmptyPlaceholderTrack();
void ndsSequenceDialectMarksUnterminatedVarLenAsTruncated();
void ndsSequenceDialectDoesNotLinkInvalidControlTargets();
void ndsMalformedRecoveryKeepsExecutableJumps();
void ndsSynthParserKeepsInfiniteReleaseOutOfPreciseSeconds();
void ndsSynthParserDerivesAdpcmLengthsSafely();
void ndsWaveArchiveReportsTruncatedSampleHeaders();

int main() {
  try {
    runValueRegistryTests();
    runValueCompilerCursorTests();
    runValueSequenceModelTests();
    runValueSequenceVmTests();
    runValueSessionTests();
    runValueSourceMapTests();
    runValueMidiTests();
    runValueSynthExportTests();

    capcomSnesLayoutSelectsSongListAndFixedHeaders();
    capcomSnesLayoutFallsBackToV2SongList();
    capcomSnesLayoutReadsOldAndRejectsMalformedDspInit();
    capcomSnesModuleDiscoversSequenceInstrumentsAndSamples();
    capcomSnesModuleWarnsWhenDetectedSynthIsEmpty();
    capcomSnesCompiledAndPerformanceSnapshotsAreStable();
    capcomSnesLfoValuesAreResolvedDuringDecode();
    capcomSnesCompiledCommandsDoNotNeedEngineProfile();
    capcomSnesModuleScansSpcThroughVirtualAramSource();
    capcomSnesInstrumentTableSkipsBlankSlotsLikeLegacy();
    capcomSnesNoteStateCommandsAreTypedAndInterpreted();
    capcomSnesSourceDialectDecodesAndRendersDriverCommands();
    capcomSnesInitialDurationRateIsFullLength();
    capcomSnesPanPerformanceCarriesGainCompensation();
    capcomSnesDialectEmitsSourceOnlyDriverSemantics();
    capcomSnesDialectEmitsPortamentoFromPreviousSourceKey();
    capcomSnesDialectExecutesRepeatUntilCommand();
    capcomSnesDialectAppliesRepeatBreakAttributesOnlyWhenBranchIsTaken();
    capcomSnesDialectDecodesRepeatBreakSideTargets();
    capcomSnesV1DialectPreservesUnknownOneByteEvents();
    konamiSnesLayoutDiscoversDirectHeaderAndSynthTables();
    konamiSnesLayoutInfersSpcDirFromInstrumentTables();
    konamiSnesModuleDiscoversSequenceInstrumentsAndSamples();
    konamiSnesSynthParsersStopAtInvalidBankedInstrument();
    konamiSnesProgramChangeReemitsCurrentFineTune();
    konamiSnesLegacyObservedVibratoRateUsesGlobalTempoCeiling();
    konamiSnesPercussionUsesPackedGsDrumBank();
    konamiSnesCompilerCursorDecodesVersionedFlowAndTruncation();
    konamiSnesCompilerCursorUsesVersionedOperandLengths();
    konamiSnesEveryVersionRendersSourceFreeCommands();
    konamiSnesSequenceSimulationPreservesDriverVibratoDepth();
    konamiSnesCompiledPlaybackHandlesCallsLoopsTiesAndSlides();
    konamiSnesCompiledAutomationTicksFades();
    konamiSnesPlayOnceCoordinatesGlobalLoopCompletion();
    akaoSnesLayoutDiscoversFf4StyleAram();
    akaoSnesModuleDiscoversSequenceInstrumentsAndSamples();
    akaoSnesV4TieExtendsShortenedPreviousNote();
    akaoDialectDecodesLegacyRelativeJumpTargets();
    akaoDialectDecodesConditionalBranchSideTargets();
    akaoSequenceAnalysisUsesSourceAnnotations();
    akaoTablePointersUseNonControlSourceLinks();
    akaoDialectDecodesRepeatFlowWithoutManualLayerLeaks();
    akaoRepeatSourceLinksUseSpecificRolesOnly();
    akaoVersion10OverlayCommandsUseLegacyLengthsAndProgramChange();
    akaoLoopBranchUsesCurrentRepeatPass();
    akaoTieAfterRestDoesNotExtendPreviousNote();
    akaoTempoFadeEmitsDriverTickRamp();
    akaoRequiredArticulationsComeFromInstrumentRows();
    akaoMelodicRegionsDropAdvancingOverlaps();
    akaoSampleSelectionKeepsPreferredAndRequiredCollections();
    akaoScanMaterializesInstrumentSetWithoutProvisionalAsset();
    ndsLayoutResolvesNamesFilesAndDependencies();
    ndsLayoutBoundsMalformedTablesAndPointers();
    ndsSequenceFatRangesHandleNormalEmptyAndRecoveredFiles();
    ndsModuleOnlyBuildsDependenciesOfReferencedBanks();
    ndsSequenceDialectDecodesAndRendersNoteWaitCommands();
    ndsSequenceDialectComposesPitchBendRangeActions();
    ndsSequenceDialectExecutesCallAndReturn();
    ndsSequenceDialectDiscoversSecondaryTrackAddresses();
    ndsSequenceTrackAddressDiscoveryKeepsMalformedBootstrapCommands();
    ndsSequenceDialectAnnotatesIgnoredOperandBytes();
    ndsSequenceDialectAnnotatesPartialIgnoredOperandBytes();
    ndsSequenceDialectKeepsEmptyPlaceholderTrack();
    ndsSequenceDialectMarksUnterminatedVarLenAsTruncated();
    ndsSequenceDialectDoesNotLinkInvalidControlTargets();
    ndsMalformedRecoveryKeepsExecutableJumps();
    ndsSynthParserKeepsInfiniteReleaseOutOfPreciseSeconds();
    ndsSynthParserDerivesAdpcmLengthsSafely();
    ndsWaveArchiveReportsTruncatedSampleHeaders();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
