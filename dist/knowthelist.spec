#
#    Copyright (C) 2014 Mario Stephan <mstephan@shared-files.de>
#
%define name    knowthelist


%if 0%{?suse_version}
%define qmake /usr/bin/qmake
%else
%define qmake qmake-qt4

%endif



Summary: Knowthelist - the awesome party music player
Name: %{name}
License: LGPL-3.0+
URL: https://github.com/knowthelist/knowthelist
Version: 1
Release: 1
Group: Multimedia
Source: %{name}_%{version}.orig.tar.gz
Packager: Mario Stephan <mstephan@shared-files.de>
Distribution: %{distr}
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
%if 0%{?suse_version}
BuildRequires: update-desktop-files
BuildRequires: libtag-devel
BuildRequires: libqt5-devel
Requires:       gstreamer-10-plugins-base
Requires:       gstreamer-10-plugins-ugly
Requires:       gstreamer-10-plugins-good
Requires:       gstreamer-10-plugins-bad
Requires:       libgstreamer-10-0
Requires:       gstreamer-10
%else
BuildRequires: qt-devel >= 5.0
BuildRequires: taglib-devel      
Requires:       gstreamer-plugins-base
Requires:       gstreamer-plugins-good
Requires:       gstreamer-plugins-bad-free
Requires:       gstreamer
%endif

BuildRequires: glib2-devel
BuildRequires: pkgconfig(gstreamer-1.0)
BuildRequires: gcc-c++
BuildRequires: alsa-devel

Requires:       taglib
Requires:       alsa


%prep
%setup
%build
%{qmake} -makefile %{name}.pro
%{qmake}
make

%install
%{__install} -Dm 755 -s %{name} %{buildroot}%{_bindir}/%{name}
%{__install} -Dm 644 dist/%{name}.desktop %{buildroot}%{_datadir}/applications/%{name}.desktop
%{__install} -Dm 644 dist/%{name}.png %{buildroot}%{_datadir}/pixmaps/%{name}.png
%if 0%{?suse_version} > 0
%suse_update_desktop_file -r %{name} AudioVideo Player
%endif

%clean
[ "%{buildroot}" != "/" ] && %{__rm} -rf %{buildroot}


%files
%defattr(-,root,root,-)
%{_bindir}/*
%{_datadir}/applications/%{name}.desktop
%{_datadir}/pixmaps/%{name}.png



%description
Easy to use for all party guests
Quick search for tracks in collection
Two players with separate playlists
Mixer with fader, 3 channel EQ and gain
Auto fader and auto gain
Trackanalyser search for song start/end and gain setting
Auto DJ function with multiple filters for random play
Monitor player for pre listen tracks (via 2nd sound card e.g. USB)

%changelog
* Thu Aug 26 2014 Mario Stephan <mstephan@shared-files.de>
- 2.2.3
- Get rid of dependency to Boost
- Bugfix where adding a song caused a segmentation fault
- Switched to Homebrew package installer for MacOS
- Set CUE button to untranslatable
- Translation updates

* Thu Aug 06 2014 Mario Stephan <mstephan@shared-files.de>
- 2.2.0
- Added a new left side tab "Lists" to manage lists, dynamic and stored lists
- Added a new feature to handle track ratings
- Added a combo box for AutoDJ artist and genre filters to be able to select also from a list
- Added a new way in how to add and remove items of AutoDj and lists
- Added "Open File Location" at playlist context menu
- Added a playlist info label (count,time) to player
- Added French translation (thanks to Geiger David and Adrien D.)
- Changed to a better way to summarise count and length of tracks for AutoDJ
- Optimized for smaller screens
- Fix to be more flexible for empty tags
- Enhanced algorithm to fill playlist and simplified handling of current and next item
- Fixed some size issues and cosmetical issues
- Stabilized to avoid crashed in some cases 

* Thu Jul 03 2014 Mario Stephan <mstephan@shared-files.de>
- 2.1.3
-  Added new widget ModeSelector to select collection tree mode
-  Added a counter for played songs
-  New: Generate a default cover image if the tag provides none
-  Optimized: gain dial moves smoothly now to avoid hard skips of volume
-  Optimized function to decouple database requests from GUI activities
-  Optimized for size scaling of form
* Tue Jun 10 2014 Mario Stephan <mstephan@shared-files.de>
- 2.1.2
- Added translation for hu_HU (thanks to László Farkas)
- AutoDJ panel rearrangements, new record case stack display added
- AutoDJ names settings in settings dialog added  
* Tue Jun 03 2014 Mario Stephan <mstephan@shared-files.de>
- 2.1.1
- Add localization, cs_CZ (thanks to Pavel Fric), de_DE
* Thu May 29 2014 Mario Stephan <mstephan@shared-files.de>
- 2.1.0
- First Release