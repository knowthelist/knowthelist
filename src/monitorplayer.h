/*
    Copyright (C) 2005-2014 Mario Stephan <mstephan@shared-files.de>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MONITORPLAYER_H
#define MONITORPLAYER_H

#include <QtCore>
#include <QWidget>

#if defined(Q_OS_WIN32)
    #include <windows.h>
    #include <dsound.h>
#endif

#define GST_DISABLE_LOADSAVE 1
#define GST_DISABLE_REGISTRY 1
#define GST_DISABLE_DEPRECATED 1
#include <gst/gst.h>

typedef QPair<QString, QUuid> dsDevice;


class MonitorPlayer: public QWidget
{
   Q_OBJECT
public:
    MonitorPlayer(QWidget *parent = 0);
    ~MonitorPlayer();


     bool prepare();
     bool ready();
     bool canOpen(QString mime);
     void open(QUrl url);
     void play();
     void stop();
     void pause();
     bool close();
     void setPosition(QTime);
     QTime position();
     double  volume();
     void setVolume(double);

     QTime length();
     bool isPlaying();
     bool mediaPlayable();
     QStringList outputDevices();
     QString outputDeviceName();
     QString outputDeviceID();
     void setOutputDevice(QString deviceName);
     void readDevices();
     QString defaultDeviceID();

     double levelLeft() {return rms_l;}
     double levelRight() {return rms_r;}

        void newpad (GstElement *decodebin, GstPad *pad, gpointer data);
        static GstBusSyncReply  bus_cb (GstBus *bus, GstMessage *msg, gpointer data);
 Q_SIGNALS:
        void finish();
        void error();
        void levelChanged();
        void positionChanged();
        void loadFinished();
 private slots:
        void loadThreadFinished();
        void messageReceived(GstMessage* message);

 private:
    struct Private;
    Private * p;
        GstElement *pipeline;
        GstBus *bus;
        //QTimer *timer;
        gint64 Gstart, Glength;
        int m_length;
        int m_position;
        dsDevice dev;
        double rms_l,rms_r;

        void setLink(int, QUrl&);
        void asyncOpen(QUrl url);
        void cleanup();
        void sync_set_state(GstElement*, GstState);

};

#endif // MONITORPLAYER_H




