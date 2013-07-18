VGMTrans - Video Game Music Translator

This software is released under the zlib/libpng License.  See license.txt.

===========
What is it?
===========
VGMTrans converts a music files used in console video games into standard midi and dls files.  It also plays these files in-program.  The following formats are supported with varying degrees of accuracy:
-Sony's PS2 sequence and instrument formats (.bq, .hd, .bd)
-Squaresoft's PS2 sequence and instrument formats (.bgm, .wd)
-Nintendo's Nintendo DS sequence and instrument formats (SDAT)
-Late versions of Squaresoft's PS1 format known as AKAO - sequences and instruments
-Sony's PS1 sequence and instrument formats (.seq, .vab)
-Capcom's QSound sequence and instrument formats used in CPS1/CPS2 arcade games
-Squaresoft's PS1 format  used in certain PS1 games like Final Fantasy Tactics (smds/dwds)
-Nintendo's Gameboy Advance sequence format

The source code includes preliminary work on additional formats. 

=============
How to use it
=============
To load a file, drag and drop the file into the application window.  The program will scan any file for contained music files.  It knows how to unpack psf, psf2 and certain zipped mame rom sets as specified in the mame_roms.xml file, though this last feature is fairly undeveloped.  For example, drag on an NDS rom file and it will detect SDAT files and their contents.

Once loaded, double-clicking a file listed under "Detected Music Files" will bring up a color-coded hexadecimal display of the file with a break-down of each format element.  Click the hexadecimal to highlight an element and see more information.  Right click a detected file to bring up save options.  To remove files from the "Detected Music Files" or "Scanned Files" list, highlight the files and press the delete key.

The "Collections" window displays file groupings that the software was able to infer.  A sequence file will be paired with one or more instrument sets and/or sample collections. A collection can be played by highlighting it and pressing the play button or spacebar.

===========================
What you need to compile it
===========================
-Visual Studio 2010.  Other versions may also work but are untested.  Since it uses some ATL headers, you will need WDK if you use Express Edition.
-Windows Template Library 8.0 (WTL)
-a version of the DirectX 9 SDK  which supports DirectMusic (DirectMusic was eventually removed by Microsoft.  The August 2007 SDK works, for example)

==============
A special note
==============
This software is prone to crash and its source code is extremely messy.  The same can be said of the author - a sad, hollow shell of a man.

======
Thanks
======
loveemu, for your help on the DS and GBA format.
Sound Test: 774 (anonymous guy in 2ch BBS), for your help on the HOSA format, analyzing the TriAacePS1 format and various other things.

=======
Contact
=======
If you enjoy the software, or have any questions please contact the development team.

https://github.com/vgmtrans/vgmtrans
