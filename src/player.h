/*
    Copyright (C) 2011 Mario Stephan <mstephan@shared-files.de>

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
#ifndef PLAYER_H
#define PLAYER_H

#include <QtCore>
#include <QWidget>

#define GST_DISABLE_LOADSAVE 1
#define GST_DISABLE_REGISTRY 1
#define GST_DISABLE_DEPRECATED 1

#include <gst/gst.h>

class Player : public QWidget
{
    Q_OBJECT
public:
    Player(QWidget *parent = 0);
    ~Player();

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
     void setGain(double);
     void setEqualizer(QString, double);

     QTime length();
     bool isPlaying();
     bool mediaPlayable();
     QString lastError;

     double levelLeft() {return rms_l;}
     double levelRight() {return rms_r;}
     double levelOutLeft() {return rmsout_l;}
     double levelOutRight() {return rmsout_r;}

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

        GstElement *pipeline;
        GstBus *bus;
        //QTimer *timer;
        gint64 Gstart, Glength;
        int m_length;
        int m_position;

        struct Private;
        Private * p;

        double rms_l,rms_r,rmsout_l,rmsout_r;

        void setLink(int, QUrl&);
        void asyncOpen(QUrl url);
        void cleanup();
        void sync_set_state(GstElement*, GstState);
   };

#endif
