/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <exception>
#include <iostream>

void runValueRegistryTests();
void runValueSequenceModelTests();
void runValueSequenceVmTests();
void runValueSessionTests();
void runValueSourceMapTests();
void runValueMidiTests();
void runValueSynthExportTests();

void capcomSnesModuleDiscoversSequenceInstrumentsAndSamples();
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
void ndsSequenceDialectDecodesAndRendersNoteWaitCommands();
void ndsSequenceDialectExecutesCallAndReturn();
void ndsSequenceDialectDiscoversSecondaryTrackStarts();
void ndsSequenceTrackStartDiscoveryKeepsMalformedBootstrapCommands();
void ndsSequenceDialectAnnotatesIgnoredOperandBytes();
void ndsSequenceDialectKeepsEmptyPlaceholderTrack();
void ndsSequenceDialectMarksUnterminatedVarLenAsTruncated();
void ndsSequenceDialectLinksInvalidControlTargets();
void ndsMalformedRecoveryKeepsExecutableJumps();
void ndsSynthParserKeepsInfiniteReleaseOutOfPreciseSeconds();
void ndsSynthParserDerivesAdpcmLengthsSafely();
void ndsWaveArchiveReportsTruncatedSampleHeaders();

int main() {
  try {
    runValueRegistryTests();
    runValueSequenceModelTests();
    runValueSequenceVmTests();
    runValueSessionTests();
    runValueSourceMapTests();
    runValueMidiTests();
    runValueSynthExportTests();

    capcomSnesModuleDiscoversSequenceInstrumentsAndSamples();
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
    ndsSequenceDialectDecodesAndRendersNoteWaitCommands();
    ndsSequenceDialectExecutesCallAndReturn();
    ndsSequenceDialectDiscoversSecondaryTrackStarts();
    ndsSequenceTrackStartDiscoveryKeepsMalformedBootstrapCommands();
    ndsSequenceDialectAnnotatesIgnoredOperandBytes();
    ndsSequenceDialectKeepsEmptyPlaceholderTrack();
    ndsSequenceDialectMarksUnterminatedVarLenAsTruncated();
    ndsSequenceDialectLinksInvalidControlTargets();
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
