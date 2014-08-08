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
- QT4.8			http://qt-project.org
			core,gui,
			xml,sql 
- taglib		http://taglib.github.io 
- gstreamer-0.10	http://gstreamer.freedesktop.org 
- gstreamer-0.10-plugins-* http://docs.gstreamer.com/display/GstSDK/Home 
- boost-devel
- alsa-devel		(Linux only)

Build:
----------
- cd ~/src
- git clone https://github.com/knowthelist/knowthelist.git
- cd knowthelist
- qmake (for MacOS: qmake -spec macx-g++)
- make
- ./knowthelist

Install packages:
-----------------
Prebuilt packages can be found here: http://opendesktop.org/content/show.php/Knowthelist?content=165335

Versions:
----------
- 2.2 (2014-08):	support for stored lists
- 2.1 (2014-05):	removed qt3support
- 2.0 (2011)   :	qt-only + gstreamer version for multiple OS support
- 1.x (2005)   :    	only for KDE Linux with arts sound framework
