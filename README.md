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
- ... more details can be found on the [Wiki](https://github.com/knowthelist/knowthelist/wiki)

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
A prebuilt package for windows is available in the release section on this page. The only prerequisite is a installed GStreamer runtime. But if you what to build Knowthelist on Windows for your self, you can do this like this:

Build dynamic version to debug project:
- Install [gstreamer-x86 runtime & devel](http://gstreamer.freedesktop.org/data/pkg/windows)
- Install [Qt5 MinGW incl. QtCreator](http://qt-project.org/downloads)
Due to different exception handling versions (SJLJ, DWARF) of used MinGW comiler for GStreamer and Qt5, it is neccessary to use an own version of taglib. To get this, do this:  
- Get [CMake](http://www.cmake.org/cmake/resources/software.html) and install
- Get [taglib](http://taglib.github.io) and unzip
- Open CMake GUI, select taglib folder, press Configure and build taglib
- Add the taglib bin path (e.g. C:\Program Files (x86)\taglib-1.9.1\bin) to PATH variable into the QtCreator project build enviroment settings
- Add the GStreamer bin path (e.g. C:\gstreamer\1.0\x86\bin) to PATH variable into the QtCreator project build enviroment settings 
- Rename libtag.dll and libstdc++-6.dll in GStreamer bin path to _libtag.dll and _libstdc++-6.dll
- Build and run knowthelist project within QtCreator (Ctrl-R)

Build static version for release:
- Install [gstreamer-x86 runtime & devel](http://gstreamer.freedesktop.org/data/pkg/windows)
- Build a [Qt static environment](http://qt-project.org/wiki/How-to-build-a-static-Qt-for-Windows-MinGW) 
- Build knowthelist via QtCreator (qmake, build release)
- Copy all dll files of the gstreamer's bin folder (e.g. C:\gstreamer\1.0\x86\bin) into the target folder together with knowthelist.exe
- Copy all dll files of the gstreamer's plugin  folder (e.g. C:\gstreamer\1.0\x86\lib\gstreamer-1.0) into an new folder named 'plugin' in parallel to knowthelist.exe.
- Run knowthelist.exe

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
