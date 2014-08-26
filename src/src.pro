#
# Knowthelist
# Copyright (C) 2011-2014 Mario Stephan <mstephan@shared-files.de>
# License: LGPL-3.0+
#

DEFINES += APP_VERSION="\\\"2.2.3\\\""
QT += core \
    gui \
    xml \
    sql \
    location
TARGET = knowthelist
TEMPLATE = app
SOURCES += main.cpp \
    knowthelist.cpp \
    player.cpp \
    vumeter.cpp \
    qvumeter.cpp \
    playerwidget.cpp \
    qled.cpp \
    playlistitem.cpp \
    playlist.cpp \
    progressbar.cpp \
    collectiondb.cpp \
    settingsdialog.cpp \
    track.cpp \
    trackanalyser.cpp \
    djsession.cpp \
    dj.cpp \
    filter.cpp \
    djwidget.cpp \
    djfilterwidget.cpp \
    fancytabwidget.cpp \
    stylehelper.cpp \
    filebrowser.cpp \
    collectionwidget.cpp \
    collectiontree.cpp \
    collectionupdater.cpp \
    collectiontreeitem.cpp \
    monitorplayer.cpp \
    collectionsetupmodel.cpp \
    stackdisplay.cpp \
    djsettings.cpp \
    modeselector.cpp \
    playlistbrowser.cpp \
    playlistwidget.cpp \
    djbrowser.cpp \
    ratingwidget.cpp
HEADERS += knowthelist.h \
    vumeter.h \
    qvumeter.h \
    playerwidget.h \
    qled.h \
    playlistitem.h \
    playlist.h \
    player.h \
    progressbar.h \
    collectiondb.h \
    settingsdialog.h \
    track.h \
    trackanalyser.h \
    djsession.h \
    dj.h \
    filter.h \
    djwidget.h \
    djfilterwidget.h \
    fancytabwidget.h \
    stylehelper.h \
    filebrowser.h \
    collectionwidget.h \
    collectiontree.h \
    collectionupdater.h \
    collectiontreeitem.h \
    monitorplayer.h \
    collectionsetupmodel.h \
    stackdisplay.h \
    djsettings.h \
    modeselector.h \
    playlistbrowser.h \
    playlistwidget.h \
    djbrowser.h \
    ratingwidget.h
FORMS += \
    settingsdialog.ui \
    djwidget.ui \
    djfilterwidget.ui \
    playerwidget.ui \
    knowthelist.ui \
    djsettings.ui \
    modeselector.ui \
    playlistwidget.ui
TRANSLATIONS += \
    ../locale/knowthelist_cs.ts \
    ../locale/knowthelist_de.ts \
    ../locale/knowthelist_hu.ts \
    ../locale/knowthelist_fr.ts \
    ../locale/knowthelist_nl.ts \
    ../locale/knowthelist_ru.ts \
    ../locale/knowthelist_es.ts \
    ../locale/knowthelist_tr.ts \
    ../locale/knowthelist_it.ts

win32 { 
    INCLUDEPATH += $$quote(C:\Program Files (x86)\gstreamer-sdk\0.10\x86\include\gstreamer-0.10) \
        $$quote(C:\Program Files (x86)\gstreamer-sdk\0.10\x86\include\glib-2.0) \
        $$quote(C:\Program Files (x86)\gstreamer-sdk\0.10\x86\lib\glib-2.0\include) \
        $$quote(C:\Program Files (x86)\taglib-1.6.1\include) \
        $$quote(C:\Program Files (x86)\gstreamer-sdk\0.10\x86\include\dsound) \
        $$quote(C:\Program Files (x86)\boost_1_50_0)
    LIBS += $$quote(C:\Program Files (x86)\gstreamer-sdk\0.10\x86\lib\gstreamer-0.10.lib) \
        $$quote(C:\Program Files (x86)\gstreamer-sdk\0.10\x86\lib\gobject-2.0.lib) \
        $$quote(C:\Program Files (x86)\gstreamer-sdk\0.10\x86\lib\glib-2.0.lib) \
        $$quote(C:\Program Files (x86)\taglib-1.6.1\lib\libtaglib.a) \
        -ldsound \
        -lwinmm

    RC_FILE = knowthelist.rc
}
macx { 
    INCLUDEPATH += /usr/local/include/gstreamer-0.10 \
        /usr/local/include/glib-2.0 \
        /usr/local/lib/glib-2.0/include \
        /usr/local/include
    LIBS += -L/usr/local/lib \
        -lgstreamer-0.10 \
        -lglib-2.0 \
        -lgobject-2.0 \
        -ltag \
        -framework CoreAudio \
        -framework CoreFoundation

}
unix:!macx {
            isEmpty(PREFIX):PREFIX = /usr
            BINDIR = $$PREFIX/bin
            DATADIR = $$PREFIX/share
            target.path = $$BINDIR
            icon.path = $$DATADIR/pixmaps
            icon.files += ../dist/knowthelist.png
            desktop.path = $$DATADIR/applications
            desktop.files += ../dist/Knowthelist.desktop
            INSTALLS += target icon desktop
    CONFIG += link_pkgconfig \
        gstreamer
    PKGCONFIG += gstreamer-0.10 \
        taglib alsa
}
RESOURCES += ../images/icons.qrc \
    ../locale/locale.qrc
ICON = ../dist/headset.icns

isEmpty(QMAKE_LRELEASE) {
win32:QMAKE_LRELEASE = $$[QT_INSTALL_BINS]\lrelease.exe
else:QMAKE_LRELEASE = $$[QT_INSTALL_BINS]/lrelease
}

lrelease.commands = $$QMAKE_LRELEASE ${QMAKE_FILE_IN}
lrelease.input = TRANSLATIONS
lrelease.output = ../locale/${QMAKE_FILE_BASE}.qm
lrelease.CONFIG = no_link target_predeps
QMAKE_EXTRA_COMPILERS += lrelease

DESTDIR = ../

OTHER_FILES +=
