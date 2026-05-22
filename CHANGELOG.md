# Changelog

## [Unreleased]

### Music Formats

**SNES (Super Nintendo / SFC)**

* **N-SPC:** Added drum kit, vibrato, and pitch slide support to the standard driver.
* **N-SPC:** Improved detection of instrument data.
* **KonamiSnes:** Added drum kit, portamento, and slide support for volume/pan/pitch.
* **CapcomSnes:** Added vibrato, tremolo, more accurate portamento support, and fixed pan calculation.
* **AkaoSnes:** Added vibrato support.
* **SuzukiSnes:** Added drum kit support (Seiken Densetsu 3, Super Mario RPG, etc.).
* **Intelligent Systems:** Improved support for late-era titles (Tetris Attack, Fire Emblem 4, Super Famicom Wars).
* **SPC:** Fixed loading of older SPC files that do not have ID666 tags.

**Game Boy Advance (GBA)**

* **Sappy (mp2k):** Added support for missing commands (LFO, pitch bend range, transpose, etc.).
* **Sappy (mp2k):** Added support for GBA PSG instruments and programmable wavetables.
* **Sappy (mp2k):** Fixed sequences being exported 0.45% faster than they should be.
* **Sappy (mp2k):** Fixed instruments potentially having very long release times.

**Nintendo DS (SDAT)**

* **Instruments:** Improved accuracy of instrument ADSR.
* **Instruments:** Fixed an issue where PSG instruments were exported too quiet (-4.6dB attenuated).
* **Samples:** Fixed an issue where samples were being read with the wrong sample rate, resulting in instruments playing at the wrong pitch or seemingly being absent.
* **Stability:** Fixed an issue where scanning certain files would crash the program or show corrupt sequence names.

**Arcade (CPS3)**

* **Audio:** Fixed CPS3 volume and ADSR bugs.

### User Interface & Tools

* **Rendering:** The analysis panel was fully rewritten and now uses GPU rendering.
* **Playback:** Sequence items in the hexview are now highlighted as playback progresses. Alt-click on items to seek playback directly to them.
* **Dock Layout:** The dock layout and window chrome have been redesigned. The layout now persists between app runs and can be reset via a new menu action.
* **Collections:** Added a search field to the Collections panel.
* **Stitched Export:** Added stitched export for collection chunks. You can now drag collections into a stitch queue, reorder them, and export them as a merged MIDI + SF2.
* **Hotkeys:** The space-bar hotkey to toggle playback no longer requires clicking on a collection first.
* **Bug Reporting:** Added an in-app dialog to help with reporting bugs.

### General

* **Crash Fix:** Fixed a crash when removing a collection that was currently being played.
* **Memory Leaks:** Fixed various memory leaks, most notably one which occurred during collection playback.

### Platform Specific Notes

**Linux**

* **Packaging:** Added Flatpak builds. From now on, stable releases will be automatically available on Flathub.
* **Wayland:** Improved Wayland support (drag and drop, sandbox).
* **Builds:** Fixed general build issues on Linux.

<!--
After a release goes out, reset the changelog to this:

# Changelog

## [Version]

### Music Formats
- **[System/Driver]:** [Change detail]

### User Interface & Tools
- **[Panel/Element]:** [Change detail]

### General
- **[Whatever]:** [Change detail]

### Platform Specific Notes
- **[OS/Package]:** [Change detail]

--!>