# Warning Fix Log

This log records significant warning-policy and warning-fix changes made during the cleanup.

## Build System Policy

- Moved VGMTrans warning flags out of platform toolchain files and into `cmake/VGMTransWarnings.cmake`, applied only to VGMTrans-owned targets. This keeps third-party code under `lib/` from inheriting project warning flags.
- Added `VGMTRANS_WARNINGS_AS_ERRORS` as an opt-in CMake option instead of forcing warnings-as-errors by default. This matches the common maintainer/CI pattern while keeping local dependency/compiler drift from breaking ordinary builds.
- Chose a GCC/Clang/AppleClang policy centered on `-Wall`, `-Wextra`, `-Wcast-align`, `-Wnull-dereference`, and `-Woverloaded-virtual`, with `-Wno-unused-parameter` because this codebase has intentional virtual/default callback parameters.
- Suppressed sign-compare and shadow diagnostics for project targets. VGMTrans has many parser offsets, sentinel values, and local parser-state names where those warnings were mostly noise; keeping them on would obscure higher-signal diagnostics.
- Left `-Wpedantic` out of the project policy because project code uses compiler-supported extensions such as anonymous structs in binary-layout helpers.
- Left `-Wold-style-cast` out of the project policy. It is useful in small modernized projects, but in VGMTrans it produced noise around C APIs and vendored macro expansions without a proportional bug-finding payoff for this cleanup.
- For C sources, omitted `-Wpedantic` because `src/ui/shell/linenoise.c` uses common C variadic macro patterns that are not worth rewriting as part of the C++ warning policy.
- Suppressed Qt's private-module configure warning after confirming the project intentionally links `Qt6::GuiPrivate`.

## Mechanical Warning Cleanup

- Reordered constructor initializer lists to match declaration order in core sample, sequence, RIFF, format-specific instrument, and Qt RHI window classes. These changes remove `-Wreorder-ctor` diagnostics without changing construction semantics.
- Added missing `override` annotations to format macros and several derived parser/scanner classes so `-Woverloaded-virtual` can remain enabled with useful signal.
- Marked the PSX ADSR helper as `inline` because it is defined in a header included by multiple translation units.
- Corrected mismatched `class`/`struct` forward declarations for AKAO articulation data and MAME metadata.
- Removed an unused local reverb flag in AKAO drum-region parsing while preserving the UI annotation for the pan/reverb byte.
