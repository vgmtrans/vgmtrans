# VGMTrans - Video Game Music Translator
---
[![Build Status](https://travis-ci.org/vgmtrans/vgmtrans.svg?branch=refactor)](https://travis-ci.org/vgmtrans/vgmtrans) [![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/djsal74fdlue142s/branch/refactor?svg=true)](https://ci.appveyor.com/project/mikelow/vgmtrans)

*This branch is experimental and aims to modernize and clean the codebase. Don't use this unless you know what you are doing.*

VGMTrans converts music files used in console videogames into standard MIDI and soundfonts.
It also plays these files in-program.

The following formats are supported with varying degrees of accuracy:
- Sony's PS2 sequence and instrument formats (.bq, .hd, .bd)
- Squaresoft's PS2 sequence and instrument formats (.bgm, .wd)
- Nintendo's Nintendo DS sequence and instrument formats (SDAT)
- Late versions of Squaresoft's PS1 format known as AKAO - sequences and instruments
- Sony's PS1 sequence and instrument formats (.seq, .vab)
- Heartbeat's PS1 sequence format used in PS1 Dragon Quest games (.seqq)
- Tamsoft's PS1 sequence and instrument formats (.tsq, .tvb)
- Capcom's QSound sequence and instrument formats used in CPS1/CPS2 arcade games
- Squaresoft's PS1 format used in certain PS1 games like Final Fantasy Tactics (smds/dwds)
- Konami's PS1 sequence format known as KDT1
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

Contributors
------------

- Mike: The original author of the tool, worked on a lot of formats.
- loveemu: Creator of github project, worked on bugfixes/improvements.
- Sound Test: 774: Anonymous Japanese guy in 2ch BBS, worked on the HOSA format, analyzing the TriAcePS1 format and such.

### Special Thanks

- Bregalad: Author of [GBAMusRiper](http://www.romhacking.net/utilities/881/), great reference of MP2k interpretation.
- Nisto: Author of [kdt-tool](https://github.com/Nisto/kdt-tool), thank you for your approval of porting to VGMTrans.

Contact
-------
If you enjoy the software, or have any questions please contact the development team.
<https://github.com/vgmtrans/vgmtrans>
