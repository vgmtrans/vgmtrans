[![Build Status](https://travis-ci.org/vgmtrans/vgmtrans.svg?branch=master)](https://travis-ci.org/vgmtrans/vgmtrans)
[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/djsal74fdlue142s/branch/master?svg=true)](https://ci.appveyor.com/project/mikelow/vgmtrans)

VGMTrans - Video Game Music Translator
======================================

VGMTrans converts a music files used in console video games into standard midi and dls/sf2 files.  It also plays these files in-program.  The following formats are supported with varying degrees of accuracy:

- Sony's PS2 sequence and instrument formats (.bq, .hd, .bd)
- Squaresoft's PS2 sequence and instrument formats (.bgm, .wd)
- Nintendo's Nintendo DS sequence and instrument formats (SDAT)
- Late versions of Squaresoft's PS1 format known as AKAO - sequences and instruments
- Sony's PS1 sequence and instrument formats (.seq, .vab)
- Heartbeat's PS1 sequence format used in PS1 Dragon Quest games (.seqq)
- Tamsoft's PS1 sequence and instrument formats (.tsq, .tvb)
- Capcom's QSound sequence and instrument formats used in CPS1/CPS2 arcade games
- Squaresoft's PS1 format used in certain PS1 games like Final Fantasy Tactics (smds/dwds)
- Nintendo's Gameboy Advance sequence format
- Nintendo's SNES sequence and instrument format known as N-SPC (.spc)
- Squaresoft's SNES sequence and instrument format (AKAO/SUZUKI) (.spc)
- Capcom's SNES sequence and instrument format (.spc)
- Konami's SNES sequence and instrument format (.spc)
- Hudson's SNES sequence and instrument format (.spc)
- Rare's SNES sequence and instrument format (.spc)
- Heartbeat's SNES sequence and instrument format used in SNES Dragon Quest VI and III (.spc)
- Akihiko Mori's SNES sequence and instrument format (.spc)
- Pandora Box's SNES sequence and instrument format (.spc)
- Graphic Research's SNES sequence and instrument format (.spc)
- Chunsoft's SNES sequence and instrument format (.spc)
- Compile's SNES sequence and instrument format (.spc)
- Namco's SNES sequence and instrument format (.spc)
- Prism Kikaku's SNES sequence and instrument format (.spc)

The source code includes preliminary work on additional formats. 

This software is released under the zlib/libpng License. See LICENSE.txt.

How to use it
-------------

To load a file, drag and drop the file into the application window.  The program will scan any file for contained music files.  It knows how to unpack psf, psf2 and certain zipped mame rom sets as specified in the mame_roms.xml file, though this last feature is fairly undeveloped.  For example, drag on an NDS rom file and it will detect SDAT files and their contents.

Once loaded, double-clicking a file listed under "Detected Music Files" will bring up a color-coded hexadecimal display of the file with a break-down of each format element.  Click the hexadecimal to highlight an element and see more information.  Right click a detected file to bring up save options.  To remove files from the "Detected Music Files" or "Scanned Files" list, highlight the files and press the delete key.

The "Collections" window displays file groupings that the software was able to infer.  A sequence file will be paired with one or more instrument sets and/or sample collections. A collection can be played by highlighting it and pressing the play button or spacebar.

How to compile it
-----------------

Please read VGMTrans wiki page:
<https://github.com/vgmtrans/vgmtrans/wiki/Build-Instructions>

Contributors
------------

- Mike: The original author of the tool, worked on a lot of formats.
- loveemu: Creator of github project, worked on bugfixes/improvements.
- Sound Test: 774: Anonymous Japanese guy in 2ch BBS, worked on the HOSA format, analyzing the TriAcePS1 format and such.

### Special Thanks

- Bregalad: Author of [GBAMusRiper](http://www.romhacking.net/utilities/881/), great reference of MP2k interpretation.

Contact
-------

If you enjoy the software, or have any questions please contact the development team.

<https://github.com/vgmtrans/vgmtrans>
