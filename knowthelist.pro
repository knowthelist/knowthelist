DEFINES += APP_VERSION="\\\"2.1.0\\\""
QT += core \
    gui \
    xml \
    sql
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
    collectionsetupmodel.cpp
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
    collectionsetupmodel.h
FORMS += \
    settingsdialog.ui \
    djwidget.ui \
    djfilterwidget.ui \
    playerwidget.ui \
    knowthelist.ui
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
    INCLUDEPATH += /opt/local/include/gstreamer-0.10 \
        /opt/local/include/glib-2.0 \
        /opt/local/lib/glib-2.0/include \
        /opt/local/include
    LIBS += -L/usr/local/lib \
        -L/opt/local/lib \
        -lgstreamer-0.10 \
        -lglib-2.0 \
        -lgobject-2.0 \
        -ltag \
        -framework CoreAudio \
        -framework CoreFoundation

}
unix:!macx {
    CONFIG += link_pkgconfig \
        gstreamer
    PKGCONFIG += gstreamer-0.10 \
        taglib alsa
}
RESOURCES += icons.qrc
ICON = headset.icns
target.path = $$PREFIX/bin
INSTALLS += target
