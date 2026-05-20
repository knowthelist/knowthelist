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

![](https://github.com/knowthelist/knowthelist/blob/gh-pages/images/knowthelist_2.4_mac_s.png)

Needed packages for building:
------------------
- Qt6			core,gui,xml,sql,widgets,concurrent https://www.qt.io
- taglib		http://taglib.github.io
- gstreamer-1.0	https://gstreamer.freedesktop.org
- gstreamer-1.0-plugins-* https://gstreamer.freedesktop.org/documentation/plugins_doc.html
- alsa-devel		(Linux only)

Build:
----------
- cd ~/src
- git clone https://github.com/knowthelist/knowthelist.git
- cd knowthelist
- qmake (or: cmake -B build && cmake --build build)
- make
- ./knowthelist

macOS:
----------
Knowthelist works well on macOS.

It can be compiled to a .app bundle, suitable for placing in /Applications.
Install the required dependencies using [Homebrew](https://brew.sh):

    $ brew install qt gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad taglib
    $ qmake
    $ make

An icon for "knowthelist" should now be in your main macOS Applications list, ready to launch.

Windows:
----------
A prebuilt package for Windows is available in the release section on this page. The only prerequisite is an installed GStreamer runtime. But if you want to build Knowthelist on Windows yourself, you can do this as follows:

Build dynamic version to debug project:
- Install [GStreamer runtime & devel](https://gstreamer.freedesktop.org/data/pkg/windows) (MSVC or MinGW variant matching your Qt build)
- Install [Qt6 incl. QtCreator](https://www.qt.io/download)
- Get [CMake](https://cmake.org) and install
- Get [taglib](http://taglib.github.io) and build it with CMake
- Add the taglib bin path (e.g. `C:\Program Files\taglib\bin`) to the PATH variable in the QtCreator project build environment settings
- Add the GStreamer bin path (e.g. `C:\gstreamer\1.0\msvc_x86_64\bin`) to the PATH variable in the QtCreator project build environment settings
- Build and run the knowthelist project within QtCreator (Ctrl-R)

Build for release:
- Install [GStreamer runtime & devel](https://gstreamer.freedesktop.org/data/pkg/windows)
- Build knowthelist via QtCreator (qmake, build release)
- Copy all DLL files from the GStreamer bin folder (e.g. `C:\gstreamer\1.0\msvc_x86_64\bin`) into the target folder together with knowthelist.exe
- Copy all DLL files from the GStreamer plugins folder (e.g. `C:\gstreamer\1.0\msvc_x86_64\lib\gstreamer-1.0`) into a new folder named `plugin` alongside knowthelist.exe
- Run knowthelist.exe

Install packages:
-----------------
Prebuilt packages for Linux can be found on the [releases page](https://github.com/knowthelist/knowthelist/releases).

**Debian/Ubuntu:**

    $ sudo apt install knowthelist

Versions:
----------
- 2.4 (2026)   :  Qt6 compatibility
- 2.3 (2014-09):	Qt5 compatibility and usage of GStreamer 1.x
- 2.2 (2014-08):	Support for stored lists
- 2.1 (2014-05):	First public version; removed qt3support
- 2.0 (2011)   :	Qt-only + gstreamer version for multiple OS support
- 1.x (2005)   :  Only for KDE Linux with arts sound framework
