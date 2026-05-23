#
# Knowthelist
# Copyright (C) 2011-2024 Mario Stephan <mstephan@shared-files.de>
# License: LGPL-3.0+
#

DEFINES += APP_VERSION="\\\"2.4.0\\\""

QT += core \
    gui \
    xml \
    sql \
    widgets \
    concurrent

CONFIG += c++17
DEFINES += GST_API_VERSION_1

TARGET = knowthelist
TEMPLATE = app
SOURCES += main.cpp \
    knowthelist.cpp \
    player.cpp \
    vumeter.cpp \
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
    ratingwidget.cpp \
    customdial.cpp
HEADERS += knowthelist.h \
    vumeter.h \
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
    ratingwidget.h \
    customdial.h
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

OTHER_FILES += \
    knowthelist.rc

DESTDIR = ../

win32 { 
    GST_HOME = $$quote($$(GSTREAMER_1_0_ROOT_X86))
    isEmpty(GST_HOME) {
        message(\"GSTREAMER_1_0_ROOT_X86\" not detected ...)
    }
    else {
        message(\"GSTREAMER_1_0_ROOT_X86\" detected in \"$${GST_HOME}\")
    }

    #TAGLIB_HOME = $$quote(C:\Program Files (x86)\taglib-1.9.1)

    INCLUDEPATH += $${GST_HOME}\include\gstreamer-1.0 \
        $${GST_HOME}\include\glib-2.0 \
        $${GST_HOME}\lib\glib-2.0\include \
        $${GST_HOME}\include \

    LIBS += $${GST_HOME}\lib\gstreamer-1.0.lib \
        $${GST_HOME}\lib\gobject-2.0.lib \
        $${GST_HOME}\lib\glib-2.0.lib \
        $${GST_HOME}\lib\libtag.dll.a \
        -ldsound \
        -lwinmm

    RC_FILE = knowthelist.rc

    #DEPLOY_COMMAND = $$[QT_INSTALL_BINS]\windeployqt.exe
    #DEPLOY_TARGET = $$DESTDIR$${TARGET}$${TARGET_EXT}.exe
    #QMAKE_POST_LINK = $$DEPLOY_COMMAND $$DEPLOY_TARGET  $$escape_expand(\\n\\t)


    #EXTRA_BINFILES += \
        #$${GST_HOME}bin\*.dll \
        #$${GST_HOME}\bin\*.dll
    #for(FILE,EXTRA_BINFILES){
    #            message($$QMAKE_COPY \"$$FILE\" \"$${DESTDIR}\" $$escape_expand(\\n\\t))
    #            QMAKE_POST_LINK += $$QMAKE_COPY \"$$FILE\" \"$${DESTDIR}\" $$escape_expand(\\n\\t)
    #}

    # copy patched version of directsoundsink.dll direct to GStreamer plugin path
    QMAKE_POST_LINK = $$QMAKE_COPY \"$${DESTDIR}\libgstdirectsoundsink.dll\" \"$${GST_HOME}lib\gstreamer-1.0\" $$escape_expand(\\n\\t)
}
macx { 
    BREW_PREFIX = $$system(brew --prefix 2>/dev/null)
    isEmpty(BREW_PREFIX): BREW_PREFIX = /usr/local

    # Official GStreamer framework takes priority over Homebrew
    GST_FRAMEWORK = /Library/Frameworks/GStreamer.framework/Versions/1.0
    exists($$GST_FRAMEWORK/include/gstreamer-1.0/gst/gst.h) {
        GST_PREFIX = $$GST_FRAMEWORK
    } else {
        GST_PREFIX = $${BREW_PREFIX}
    }

    INCLUDEPATH += $${GST_PREFIX}/include/gstreamer-1.0 \
        $${GST_PREFIX}/include/glib-2.0 \
        $${GST_PREFIX}/lib/glib-2.0/include \
        $${BREW_PREFIX}/lib/glib-2.0/include \
        $${GST_PREFIX}/include \
        $${BREW_PREFIX}/include
    # TagLib: use exact brew opt path to avoid picking up GStreamer's bundled TagLib 1.x
    TAGLIB_PREFIX = $$system(brew --prefix taglib 2>/dev/null)
    isEmpty(TAGLIB_PREFIX): TAGLIB_PREFIX = $${BREW_PREFIX}/opt/taglib
    LIBS += -L$${TAGLIB_PREFIX}/lib -ltag

    LIBS += -L$${GST_PREFIX}/lib \
        -lgstreamer-1.0 \
        -lglib-2.0 \
        -lgobject-2.0

    LIBS += -framework CoreAudio \
        -framework CoreFoundation

    # Add GStreamer framework lib path to rpath so @rpath/libgstreamer-1.0.0.dylib
    # resolves to the framework (1.28.3) at runtime rather than the dyld fallback
    QMAKE_LFLAGS += -Wl,-rpath,$${GST_PREFIX}/lib

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

    CONFIG += link_pkgconfig
    PKGCONFIG += gstreamer-1.0 taglib alsa

}
RESOURCES += ../images/icons.qrc \
    ../locale/locale.qrc
ICON = ../dist/headset.icns

win32 {
    QMAKE_LRELEASE = $$[QT_INSTALL_BINS]\\lrelease.exe
}
macx {
    QMAKE_LRELEASE = $$[QT_INSTALL_BINS]/lrelease
}
unix:!macx {
    exists(/usr/bin/lrelease) {
        QMAKE_LRELEASE = /usr/bin/lrelease
    } else {
        QMAKE_LRELEASE = $$[QT_INSTALL_BINS]/lrelease
    }
}

lrelease.commands = $$QMAKE_LRELEASE ${QMAKE_FILE_IN}
lrelease.input = TRANSLATIONS
lrelease.output = ../locale/${QMAKE_FILE_BASE}.qm
lrelease.CONFIG = no_link target_predeps
QMAKE_EXTRA_COMPILERS += lrelease
QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter
QMAKE_CXXFLAGS_WARN_ON += -Wno-reorder





