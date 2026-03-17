# Changelog


## [Unreleased]

### Added

- Added an in-app dialog to help with reporting bugs.
- Added Flatpak builds for Linux. From now on, stable releases will be automatically available on Flathub.

### Changed

- The analysis (HexView) panel was fully rewritten and now uses GPU rendering. Sequence items in the hexview are highlighted as playback progresses. Alt-click on items to seek playback to them.
- Improved accuracy of Nintendo DS (SDAT) instrument ADSR.
- Improved Wayland support (drag and drop, sandbox).
- The space-bar hotkey to toggle playback no longer requires clicking on a collection first.

### Fixed

- Fixed a crash when removing a collection that was being played.
- Fixed build issues on Linux.
- Fixed an issue where scanning certain Nintendo DS (SDAT) files would crash the program or show corrupt sequence names.
- Fixed an issue where Nintendo DS (SDAT) PSG instruments were exported too quiet (-4.6dB attenuated).
- Fixed an issues where samples from Nintendo DS (SDAT) data were being read with the wrong sample rate,
  resulting in instruments playing at the wrong pitch or seemingly be absent.
- Fixed various memory leaks, most notably one which occurred on collection playback.