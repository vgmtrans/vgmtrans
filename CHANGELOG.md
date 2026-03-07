# Changelog

All notable changes to this project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Added an in-app dialog to help with reporting bugs.
- Added Flatpak builds for Linux. From now on, stable releases will be automatically available on Flathub.

### Changed

- The analysis (HexView) panel was fully rewritten and now uses GPU rendering. Sequence items in the hexview are highlighted as playback progresses. Alt-click on items to seek playback to them.
- Improved accuracy of Nintendo DS instrument ADSR.
- Improved Wayland support (drag and drop, sandbox).
- The space-bar hotkey to toggle playback no longer requires clicking on a collection first.

### Fixed

- Fixed a crash when removing a collection that was being played.
- Fixed build issues on Linux.
- Fixed an issue where scanning certain Nintendo DS files would crash the program or show corrupt sequence names.
- Fixed an issue where Nintendo DS PSG instruments were exported too quiet (-4.6dB attenuated).
