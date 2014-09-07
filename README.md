knowthelist
===========

Knowthelist - the awesome party music player

- Easy to use for all party guests
- Quick search for tracks in collection
- Two players with separate playlists
- Mixer with fader, 3 channel EQ and gain
- Auto fader and auto gain
- Trackanalyser search for song start/end and gain setting
- Auto DJ function with multiple filters  for random play 
- Monitor player for pre listen tracks (via 2nd sound card e.g. USB)
- ... more https://github.com/knowthelist/knowthelist/wiki

Runs under Linux, MacOS and Windows

![](https://github.com/knowthelist/knowthelist/blob/gh-pages/images/knowthelist_2.2_mac_s.png)

Needed packages for building:
------------------
- Qt5 or Qt4	core,gui,xml,sql,widgets (Qt5 only) http://qt-project.org
- taglib		http://taglib.github.io 
- gstreamer-1.0	http://gstreamer.freedesktop.org 
- gstreamer-1.0-plugins-* http://docs.gstreamer.com/display/GstSDK/Home 
- alsa-devel		(Linux only)

Build:
----------
- cd ~/src
- git clone https://github.com/knowthelist/knowthelist.git
- cd knowthelist
- qmake (for MacOS: qmake -spec macx-g++)
- make
- ./knowthelist

MacOS X:
----------
Knowthelist works well on MacOS X.

* OSX 10.6.8 is tested and known to work

It can be compiled to a .app bundle, suitable for placing in /Applications.
Compiling is incredibly easy using [Homebrew](http://brew.sh).  Just run this command:
    
    $ cp ./dist/knowthelist.rb /usr/local/Library/Formula
    $ brew install knowthelist

And you're done. 
An icon for "knowthelist" should now be in your main OSX Applications list, ready to launch.

Windows:
----------
Build taglib
- Get [CMake](http://www.cmake.org/cmake/resources/software.html) and install
- Get [taglib](http://taglib.github.io) and unzip
- Open CMake GUI, select taglib folder, press Configure and Generate
- Download MinGW version of QtCreator [here](https://qt-project.org/downloads)
    e.g.   qt-opensource-windows-x86-mingw482_opengl-5.3.1.exe
- Add C:\Qt\Qt5.3.1\Tools\mingw482_32\bin to your PATH variable

... to be continued


Install packages:
-----------------
Prebuilt packages for Linux can be found here: http://opendesktop.org/content/show.php/Knowthelist?content=165335

Versions:
----------
- 2.3 (2014-09):	Qt5 compatibility and usage of GStreamer 1.x
- 2.2 (2014-08):	Support for stored lists
- 2.1 (2014-05):	First public version; removed qt3support
- 2.0 (2011)   :	Qt-only + gstreamer version for multiple OS support
- 1.x (2005)   :    Only for KDE Linux with arts sound framework
