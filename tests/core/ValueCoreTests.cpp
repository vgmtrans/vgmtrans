/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <exception>
#include <iostream>

void runValueRegistryTests();
void runValueCompilerCursorTests();
void runValueCollectionDiscoveryTests();
void runValueCollectionStitchTests();
void runValueInstrumentVariantTests();
void runValueSequenceModelTests();
void runValueSequenceVmTests();
void runValueSessionTests();
void runValueSourceMapTests();
void runValueSampleTests();
void runValueSynthBuilderTests();
void runValueMidiEncodingTests();
void runValueMidiRendererTests();
void runValueMidiPitchTests();
void runValueMidiModulationTests();
void runValueSequenceExportTests();
void runValueModulationTests();
void runValueSynthExportTests();
void runValueCollectionExportTests();
void runValuePsxTests();

int main() {
  try {
    runValueRegistryTests();
    runValueCompilerCursorTests();
    runValueCollectionDiscoveryTests();
    runValueCollectionStitchTests();
    runValueInstrumentVariantTests();
    runValueSequenceModelTests();
    runValueSequenceVmTests();
    runValueSessionTests();
    runValueSourceMapTests();
    runValueSampleTests();
    runValueSynthBuilderTests();
    runValueMidiEncodingTests();
    runValueMidiRendererTests();
    runValueMidiPitchTests();
    runValueMidiModulationTests();
    runValueSequenceExportTests();
    runValueModulationTests();
    runValueSynthExportTests();
    runValueCollectionExportTests();
    runValuePsxTests();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
