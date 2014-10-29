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
#ifndef TRACKANALYSER_H
#define TRACKANALYSER_H

#include <QtCore>
#include <QWidget>

#define GST_DISABLE_LOADSAVE 1
#define GST_DISABLE_REGISTRY 1
#define GST_DISABLE_DEPRECATED 1
#include <gst/gst.h>

class TrackAnalyser : public QWidget
{
    Q_OBJECT
public:
    TrackAnalyser(QWidget *parent = 0);
    ~TrackAnalyser();

    enum modeType { STANDARD, TEMPO };

    bool prepare();
    void open(QUrl url);
    void start();
    bool close();

    double gainDB();
    double gainFactor();
    QTime startPosition();
    QTime endPosition();
    int bpm();
    bool finished() {return m_finished;}
    void setMode(modeType mode);
    void setPosition(QTime position);

    QTime length();
    static const int GAIN_INVALID=-99;

    void need_finish();
    void newpad (GstElement *decodebin, GstPad *pad, gpointer data);
    static GstBusSyncReply  bus_cb (GstBus *bus, GstMessage *msg, gpointer data);

 Q_SIGNALS:
        void finishGain();
        void finishTempo();

 private slots:
    void messageReceived(GstMessage* message);
    void loadThreadFinished();

 private:
    struct TrackAnalyser_Private *p;
        GstElement *pipeline;
        GstBus *bus;

        double m_GainDB;
        QTime m_StartPosition;
        QTime m_EndPosition;
        QTime m_MaxPosition;
        bool m_finished;

        void detectTempo();
        float AutoCorrelation( QList<float> buffer, int frames, int minBpm, int maxBpm, int sampleRate);

        void cleanup();
        void asyncOpen(QUrl url);
        void sync_set_state(GstElement*, GstState);
   };

#endif // TRACKANALYSER_H
