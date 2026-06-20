knowthelist
===========

Knowthelist - the awesome party music player

- Easy to use for all party guests
- Fast collection search while tracks are playing
- Two players with separate playlists
- Mixer with crossfader, 3-band EQ and gain per deck
- Auto fade and auto gain control (AGC)
- Track analysis for cue points, loudness and BPM
- Beat-synced transitions with visual sync support
- Auto DJ with multiple filters and smart random play
- Monitor player for pre-listening on a second sound device
- ... more details can be found on the [Wiki](https://github.com/knowthelist/knowthelist/wiki)

Runs under Linux, MacOS and Windows

![](https://github.com/knowthelist/knowthelist/blob/gh-pages/images/knowthelist_2.4_mac_s.png)

Needed packages for building:
------------------
Linux (Debian/Ubuntu):

        $ sudo apt update
        $ sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools qmake6 \
            libtag1-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
            gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
            libasound2-dev

`libtag1-dev` is the Debian/Ubuntu package name for TagLib development headers.

Build:
----------
- cd ~/src
- git clone https://github.com/knowthelist/knowthelist.git
- cd knowthelist

qmake build:

    $ qmake6
    $ make
    $ ./knowthelist

cmake build:

    $ cmake -S . -B build
    $ cmake --build build -j
    $ ./build/knowthelist

Note: JUCE is cached outside `build/` (default: `~/.cache/knowthelist/fetchcontent` on macOS/Linux),
so `rm -rf build` will not re-download JUCE every time.
You can override this path with:

    $ cmake -S . -B build -DKNOWTHELIST_FETCHCONTENT_DIR=/path/to/cache

Recreate ui_*.h after .ui changes:
----------------------------------
`ui_*.h` files are generated from Qt Designer `.ui` files. Do not edit generated headers manually.

For CMake builds (recommended):

    $ cmake -S . -B build
    $ cmake --build build --target knowthelist_autogen

If needed, force a full regenerate:

    $ rm -rf build
    $ cmake -S . -B build
    $ cmake --build build -j

Generated headers are placed under:

    build/knowthelist_autogen/include/ui_*.h

For qmake builds:

    $ qmake6 knowthelist.pro
    $ make clean
    $ make -j

macOS:
----------
Knowthelist works well on macOS.

It can be compiled to a .app bundle, suitable for placing in /Applications.
Install the required dependencies using [Homebrew](https://brew.sh):

```
brew install qt taglib
qmake6
make

# or with CMake:

cmake -S . -B build
cmake --build build -j

# clean before
cmake --build build --target clean
cmake --build build -j

# or

rm -rf build && cmake -B build && cmake --build build -j

```
JUCE download is reused from the persistent fetch cache, so clean builds stay fast.

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
- 2.4 (2026)   :  Qt6 compatibility and BPM mode
- 2.3 (2014-09):	Qt5 compatibility and usage of GStreamer 1.x
- 2.2 (2014-08):	Support for stored lists
- 2.1 (2014-05):	First public version; removed qt3support
- 2.0 (2011)   :	Qt-only + gstreamer version for multiple OS support
- 1.x (2005)   :  Only for KDE Linux with arts sound framework
