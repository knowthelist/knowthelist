#
# Knowthelist
# Copyright (C) 2014 Mario Stephan <mstephan@shared-files.de>
# License: LGPL-3.0+
#
# Patched version of GStreamer sink plugin for Windows to enable selection of audio output device

DEFINES += PACKAGE="\\\"knowthelist\\\""

TARGET = libgstdirectsoundsink


TEMPLATE = lib
CONFIG  += dll
DESTDIR = $${OUT_PWD}/../

win32 { 
    GST_HOME = $$quote($$(GSTREAMER_1_0_ROOT_X86))
    isEmpty(GST_HOME) {
        message(\"GSTREAMER_1_0_ROOT_X86\" not detected ...)
    }
    else {
        message(\"GSTREAMER_1_0_ROOT_X86\" detected in \"$${GST_HOME}\")
    }

    INCLUDEPATH += $${GST_HOME}\include\gstreamer-1.0 \
        $${GST_HOME}\include\glib-2.0 \
        $${GST_HOME}\lib\glib-2.0\include \
        $${GST_HOME}\include \

    LIBS += $${GST_HOME}\lib\libgstreamer-1.0.dll.a \
            $${GST_HOME}\lib\glib-2.0.lib \
            $${GST_HOME}\lib\gobject-2.0.lib \
            $${GST_HOME}\lib\libgstaudio-1.0.dll.a \
            $${GST_HOME}\lib\libgstbase-1.0.dll.a \
            -ldsound \
            -lwinmm \
            -ldxerr9 \
            -lole32
}

HEADERS += \
    directsound/gstdirectsoundsink.h

SOURCES += \
    directsound/gstdirectsoundplugin.c \
    directsound/gstdirectsoundsink.c






