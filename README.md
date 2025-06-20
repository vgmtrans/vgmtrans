# VGMTrans - Video Game Music Translator &middot; ![Build status](https://github.com/vgmtrans/vgmtrans/actions/workflows/build.yml/badge.svg?branch=master)

VGMTrans is a cross-platform desktop application that converts sequenced video game music into standard formats.

* Converts various formats into MIDI, SoundFont2, and DLS
* Built-in playback to preview tracks
* Interactive hex viewer for inspecting music data

## Download
We have builds for macOS, Windows & Linux.

* [Official releases](https://github.com/vgmtrans/vgmtrans/releases)
* [Bleeding edge versions](https://nightly.link/vgmtrans/vgmtrans/workflows/build/master)

## Supported Formats

The following formats are supported with varying degrees of accuracy:

#### Arcade

* Capcom's sequence and sampled instrument formats used in CPS1/CPS2/CPS3 arcade games

#### Super Nintendo Entertainment System (SNES)

* Akihiko Mori's sequence and instrument format (.spc)
* ASCII Corporation, Shuichi Ukai's sequence and instrument format (.spc)
* Capcom's sequence and instrument format (.spc)
* Chunsoft's sequence and instrument format (.spc)
* Compile's sequence and instrument format (.spc)
* Graphic Research's sequence and instrument format (.spc)
* Heartbeat's sequence and instrument format used in *Dragon Quest VI* and *III* (.spc)
* Hudson's sequence and instrument format (.spc)
* Konami's sequence and instrument format (.spc)
* Namco's sequence and instrument format (.spc)
* Nintendo's sequence and instrument format known as N-SPC (.spc)
* Pandora Box's sequence and instrument format (.spc)
* Prism Kikaku's sequence and instrument format (.spc)
* Rare's sequence and instrument format (.spc)
* Squaresoft's sequence and instrument format (AKAO/SUZUKI/Itikiti) (.spc)

#### Sega Saturn

* Sega's sequence format

#### PlayStation

* Heartbeat's sequence format used in *Dragon Quest* games (.seqq)
* Konami's sequence format known as KDT1
* Sony's sequence and instrument formats (.seq, .vab)
* Squaresoft's format known as AKAO – sequences and instruments
* Squaresoft's format used in games such as *Final Fantasy Tactics* (smds/dwds)
* Tamsoft's sequence and instrument formats (.tsq, .tvb)

#### PlayStation 2

* Sony's sequence and instrument formats (.bq, .hd, .bd)
* Squaresoft's sequence and instrument formats (.bgm, .wd)

#### Game Boy Advance

* Nintendo's sequence and instrument format known as MP2k

#### Nintendo DS

* Nintendo's sequence and instrument formats (SDAT)

## Usage

### Loading Files
Load files by dragging them into the application window or by using the `File` => `Open` menu action. The program will scan a
file for contained music files, which when found will appear under the "Detected Music Files" panel. 

VGMTrans is able to 
unpack portable sound format files (PSF) and derivatives (PSF2, SSF, etc). SNES formats generally must be loaded from SPC or RSN files. 
Arcade formats are loaded via mame rom set zip files, which are catalogued in `mame_roms.xml`.

### Detected Music Files
Right-clicking a file listed in the Detected Music Files panel will bring up a menu of conversion options. Double-clicking 
a detected file will open it in the Hex View. 

### Collections
For some formats, when files representing musical sequences, instruments, and samples are all loaded, VGMTrans may
associate these files together into a Collection. Collections are listed in the Collections panel at the bottom of the
main window. Double-clicking a collection or selecting one and pressing the space bar or clicking the green Play button 
will attempt to play back the sequence using its associated instrument data.

Right-clicking a collection will bring up a menu with conversion options.

### Manual Collection Creation (advanced)
Sometimes VGMTrans cannot properly associate detected files into Collections. In this event, the "Create collection manually" 
button at the bottom of the Collection panel can be used. This buttons open a dialog that allows detected files
to be selected to form a new Collection.


### Hex View
Double-clicking a detected file opens an interactive Hex View. The Hex View provides a visual
breakdown of a file's structure, with each element color-coded and annotated upon selection. 
The accompanying File Structure panel to the right of the Hex View provides a hierarchical list of all the elements 
within the file. Clicking an element, either in the Hex View or the File Structure panel, highlights it and
displays more information about the element in the status bar at the bottom of the main window. When enabled, the "Show Details"
toggle button in the top right corner of the File Structure panel will display details for each element directly
in the File Structure panel.

## Building

Please refer to [the wiki](https://github.com/vgmtrans/vgmtrans/wiki) for compilation instructions.

## Contributors

- Mike: The original author of the tool, worked on a lot of formats.
- loveemu: Creator of github project, worked on bugfixes/improvements.
- sykhro: Worked on the Qt port, CI whisperer, contributed improvements and general housekeeping.

[View more contributors on GitHub](https://github.com/vgmtrans/vgmtrans/graphs/contributors).

#### Before GitHub:

- Sound Test: 774: Anonymous Japanese guy in 2ch BBS, worked on the HOSA format, analyzing the TriAcePS1 format and such.

#### Special Thanks

- Bregalad: Author of [GBAMusRiper](https://www.romhacking.net/utilities/881/), great reference of MP2k interpretation.
- Nisto: Author of [kdt-tool](https://github.com/Nisto/kdt-tool), thank you for your approval of porting to VGMTrans.
- [Gnilda](https://twitter.com/god_gnilda): for his/her dedicated research of SNES AKAO format. <https://gnilda.cloudfree.jp/spc/>
- [@brr890](https://twitter.com/brr890) and [@tssf](https://twitter.com/tssf): Contributed a lot of hints on PS1 AKAO format.

## License
Licensed under the zlib/libpng License. See [`LICENSE`](https://github.com/vgmtrans/vgmtrans/blob/master/LICENSE).
