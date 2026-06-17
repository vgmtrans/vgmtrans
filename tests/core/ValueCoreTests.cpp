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
void capcomSnesV1DialectPreservesUnknownOneByteEvents();
void ndsSequenceDialectDecodesAndRendersNoteWaitCommands();
void ndsSequenceDialectExecutesCallAndReturn();
void ndsSequenceDialectDiscoversSecondaryTrackStarts();
void ndsSequenceDialectPreservesIgnoredCommandOperands();
void ndsSequenceDialectKeepsEmptyPlaceholderTrack();
void ndsSequenceDialectMarksUnterminatedVarLenAsTruncated();
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
    capcomSnesV1DialectPreservesUnknownOneByteEvents();
    ndsSequenceDialectDecodesAndRendersNoteWaitCommands();
    ndsSequenceDialectExecutesCallAndReturn();
    ndsSequenceDialectDiscoversSecondaryTrackStarts();
    ndsSequenceDialectPreservesIgnoredCommandOperands();
    ndsSequenceDialectKeepsEmptyPlaceholderTrack();
    ndsSequenceDialectMarksUnterminatedVarLenAsTruncated();
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
