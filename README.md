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

To load a file, drag and drop the file into the application window.  The program will scan any file for contained music files. It knows how to unpack psf, psf2 and certain zipped mame rom sets as specified in the mame_roms.xml file.  For example, drag on an NDS rom file and it will detect SDAT files and their contents.

Once loaded, double-clicking a file listed under "Detected Music Files" will bring up a color-coded hexadecimal display of the file with a break-down of each format element.  Click the hexadecimal to highlight an element and see more information.  Right click a detected file to bring up save options.  To remove files from the "Detected Music Files" or "Scanned Files" list, highlight the files and press the delete key.

The "Collections" window displays file groupings that the software was able to infer.  A sequence file will be paired with one or more instrument sets and/or sample collections. A collection can be played by double-clicking it or by highlighting it and pressing the play button or spacebar.

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
