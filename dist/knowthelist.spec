#
#    Copyright (C) 2014 Mario Stephan <mstephan@shared-files.de>
#
%define name    knowthelist


%if 0%{?suse_version}
%define qmake /usr/bin/qmake
%else
%define qmake qmake-qt4

%endif



Summary: Knowthelist the awesome party music player
Name: %{name}
License: LGPL-3.0+
URL: https://github.com/knowthelist/knowthelist
Version: 1
Release: 1
Group: Multimedia
Source: %{name}_%{version}.tar.gz
Packager: Mario Stephan <mstephan@shared-files.de>
Distribution: %{distr}
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
%if 0%{?suse_version}
BuildRequires: update-desktop-files
BuildRequires: libtag-devel
BuildRequires: libqt4-devel >= 4.8 qwt6-devel
Requires:       gstreamer-0_10-plugins-base
Requires:       gstreamer-0_10-plugins-ugly
Requires:       gstreamer-0_10-plugins-good
Requires:       gstreamer-0_10-plugins-bad
Requires:       libgstreamer-0_10-0
Requires:       gstreamer-0_10
%else
BuildRequires: qt-devel >= 4.8
BuildRequires: taglib-devel      
Requires:       gstreamer-plugins-base
Requires:       gstreamer-plugins-good
Requires:       gstreamer-plugins-bad-free
Requires:       gstreamer
%endif

BuildRequires: glib2-devel
BuildRequires: pkgconfig(gstreamer-0.10)
BuildRequires: gcc-c++
BuildRequires: boost-devel
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
%{__install} -Dm 644 %{name}.desktop %{buildroot}%{_datadir}/applications/%{name}.desktop
%{__install} -Dm 644 %{name}.png %{buildroot}%{_datadir}/pixmaps/%{name}.png
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