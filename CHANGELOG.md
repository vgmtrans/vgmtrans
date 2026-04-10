# Changelog


## [Unreleased]

### Added

- Added an in-app dialog to help with reporting bugs.
- Added Flatpak builds for Linux. From now on, stable releases will be automatically available on Flathub.
- Added Drum Kit support to the SuzukiSnes driver (Seiken Densetsu 3, Super Mario RPG, etc)

### Changed

- The analysis (HexView) panel was fully rewritten and now uses GPU rendering. Sequence items in the hexview are highlighted as playback progresses. Alt-click on items to seek playback to them.
- The dock layout and window chrome have been redesigned. Dock layout now persists between app runs and can be reset via a new menu action.
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
- Fixed loading of older SPC files that do not have ID666 tags.
