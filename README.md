
# VGMTrans - Video Game Music Translator

[![Build Status](https://travis-ci.org/vgmtrans/vgmtrans.svg?branch=refactor)](https://travis-ci.org/vgmtrans/vgmtrans)
[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/djsal74fdlue142s/branch/refactor?svg=true)](https://ci.appveyor.com/project/mikelow/vgmtrans)

VGMTrans converts proprietary music files used in console video games into standard MIDI sequences and soundfonts.
It supports many different formats with varying degrees of accuracy.

The latest build is always available for Linux (AppImage), OSX and Windows [here](https://github.com/vgmtrans/vgmtrans-qt/releases/tag/continuous-refactor).
Compiling instructions are available [in the wiki](https://github.com/vgmtrans/vgmtrans/wiki/Building-the-Qt-version).

This software is released under the zlib/libpng License. See LICENSE.txt for details.

Contributors
------------

- Mike: The original author of the tool, worked on a lot of formats.
- loveemu: Creator of github project, worked on bugfixes/improvements.
- Sound Test: 774: Anonymous Japanese guy in 2ch BBS, worked on the HOSA format, analyzing the TriAcePS1 format and such.
- sykhro: General maintenance work, ported the tool to Qt.

### Special Thanks

- Bregalad: Author of [GBAMusRiper](http://www.romhacking.net/utilities/881/), great reference of MP2k interpretation.
- Nisto: Author of [kdt-tool](https://github.com/Nisto/kdt-tool), thank you for your approval of porting to VGMTrans.

Third party libraries
------------

- [FluidSynth](https://github.com/FluidSynth/fluidsynth)
- [TinyXML](http://www.grinninglizard.com/tinyxml/)
- [Qt](https://www.qt.io/download-open-source)
- [PhantomStyle](https://github.com/randrew/phantomstyle)
- [zlib](https://github.com/madler/zlib)
- [fmtlib](https://github.com/fmtlib/fmt)
