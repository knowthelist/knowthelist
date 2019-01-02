#
# Knowthelist
# Copyright (C) 2011-2019 Mario Stephan <mstephan@shared-files.de>
# License: LGPL-3.0+
#

DEFINES += APP_VERSION="\\\"2.3.1\\\""

QT += core \
    gui \
    xml \
    sql

greaterThan(QT_MAJOR_VERSION, 4){
     #use qt5 and gstreamer 1.x
     QT += widgets
     DEFINES += GST_API_VERSION_1
}
#else use qt4 and gstreamer 0.10

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
    DEFINES += GST_API_VERSION_1
    INCLUDEPATH += /usr/local/include/gstreamer-1.0 \
        /usr/local/include/glib-2.0 \
        /usr/local/lib/glib-2.0/include \
        /usr/local/include
    LIBS += -L/usr/local/lib \
        -lgstreamer-1.0 \
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

contains(DEFINES, GST_API_VERSION_1) {
    CONFIG += link_pkgconfig \
        gstreamer-1.0
    PKGCONFIG += gstreamer-1.0 \
        taglib alsa
}
else {
    CONFIG += link_pkgconfig \
        gstreamer
    PKGCONFIG += gstreamer-0.10 \
        taglib alsa
}

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
QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter
QMAKE_CXXFLAGS_WARN_ON += -Wno-reorder





