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

#include "trackanalyser.h"

#include <QtGui>
#if QT_VERSION >= 0x050000
 #include <QtConcurrent/QtConcurrent>
#else
 #include <QtConcurrentRun>
#endif

struct TrackAnalyser::Private
{
        QFutureWatcher<void> watcher;
        QMutex mutex;
};

TrackAnalyser::TrackAnalyser(QWidget *parent) :
        QWidget(parent),
    pipeline(0), m_finished(false)
    , p( new Private )
{

    gst_init (0, 0);
    prepare();
    connect(&p->watcher, SIGNAL(finished()), this, SLOT(loadThreadFinished()));

}

void TrackAnalyser::sync_set_state(GstElement* element, GstState state)
{ GstStateChangeReturn res; \
        res = gst_element_set_state (GST_ELEMENT (element), state); \
        if(res == GST_STATE_CHANGE_FAILURE) return; \
        if(res == GST_STATE_CHANGE_ASYNC) { \
                GstState state; \
                        res = gst_element_get_state(GST_ELEMENT (element), &state, NULL, 1000000000/*GST_CLOCK_TIME_NONE*/); \
                        if(res == GST_STATE_CHANGE_FAILURE || res == GST_STATE_CHANGE_ASYNC) return; \
} }


TrackAnalyser::~TrackAnalyser()
{
    delete p;
    p=0;
    cleanup();
}


void cb_newpad_ta (GstElement *decodebin,
                   GstPad     *pad,
                   gpointer    data)
{
    TrackAnalyser* instance = (TrackAnalyser*)data;
            instance->newpad(decodebin, pad, data);
}


void TrackAnalyser::newpad (GstElement *decodebin,
                   GstPad     *pad,
                   gpointer    data)
{
        GstCaps *caps;
        GstStructure *str;
        GstPad *audiopad;

        /* only link once */
        GstElement *audio = gst_bin_get_by_name(GST_BIN(pipeline), "audiobin");
        audiopad = gst_element_get_static_pad (audio, "sink");
        gst_object_unref(audio);

        if (GST_PAD_IS_LINKED (audiopad)) {
                g_object_unref (audiopad);
                return;
        }

        /* check media type */
#ifdef GST_API_VERSION_1
        caps = gst_pad_query_caps (pad,NULL);
#else
        caps = gst_pad_get_caps (pad);
#endif
        str = gst_caps_get_structure (caps, 0);
        if (!g_strrstr (gst_structure_get_name (str), "audio")) {
                gst_caps_unref (caps);
                gst_object_unref (audiopad);
                return;
        }
        gst_caps_unref (caps);

        /* link'n'play */
        gst_pad_link (pad, audiopad);
}

GstBusSyncReply TrackAnalyser::bus_cb (GstBus *bus, GstMessage *msg, gpointer data)
{
    TrackAnalyser* instance = (TrackAnalyser*)data;
            instance->messageReceived(msg);
    return GST_BUS_PASS;
}

void TrackAnalyser::cleanup()
{
        if(pipeline) sync_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
        if(bus) gst_object_unref (bus);
        if(pipeline) gst_object_unref(G_OBJECT(pipeline));
}

bool TrackAnalyser::prepare()
{
        GstElement *dec, *conv, *sink, *cutter, *audio, *analysis;
        GstPad *audiopad;
        GstCaps *caps;



        pipeline = gst_pipeline_new ("pipeline");
        bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

#ifdef GST_API_VERSION_1
        caps = gst_caps_new_simple ("audio/x-raw",
                                    "channels", G_TYPE_INT, 2, NULL);
        dec = gst_element_factory_make ("decodebin", "decoder");
#else
        caps = gst_caps_new_simple ("audio/x-raw-int",
                                    "channels", G_TYPE_INT, 2, NULL);
        dec = gst_element_factory_make ("decodebin2", "decoder");
#endif
        g_signal_connect (dec, "pad-added", G_CALLBACK (cb_newpad_ta), this);
        gst_bin_add (GST_BIN (pipeline), dec);

        audio = gst_bin_new ("audiobin");
        conv = gst_element_factory_make ("audioconvert", "conv");
        audiopad = gst_element_get_static_pad (conv, "sink");
        analysis = gst_element_factory_make ("rganalysis", "analysis");
        cutter = gst_element_factory_make ("cutter", "cutter");
        sink = gst_element_factory_make ("fakesink", "sink");

        g_object_set (analysis, "message", TRUE, NULL);
        g_object_set (analysis, "num-tracks", 1, NULL);
        g_object_set (cutter, "threshold-dB", -25.0, NULL);

        gst_bin_add_many (GST_BIN (audio), conv, analysis, cutter, sink, NULL);
        gst_element_link (conv, analysis);
        gst_element_link_filtered (analysis, cutter, caps);
        gst_element_link (cutter, sink);
        gst_element_add_pad (audio, gst_ghost_pad_new ("sink", audiopad));

        gst_bin_add (GST_BIN (pipeline), audio);

        GstElement *l_src;
        l_src = gst_element_factory_make ("filesrc", "localsrc");
        gst_bin_add_many (GST_BIN (pipeline), l_src, NULL);
        gst_element_set_state (l_src, GST_STATE_NULL);
        gst_element_link ( l_src,dec);

        gst_object_unref (audiopad);

#ifdef GST_API_VERSION_1
        gst_bus_set_sync_handler (bus, bus_cb, this, NULL);
#else
        gst_bus_set_sync_handler (bus, bus_cb, this);
#endif

        return pipeline;
}


double TrackAnalyser::gainDB()
{
    return  m_GainDB;
}

double TrackAnalyser::gainFactor()
{
    return pow (10, m_GainDB / 20);
}

QTime TrackAnalyser::startPosition()
{
    return m_StartPosition;
}

QTime TrackAnalyser::endPosition()
{
    return m_EndPosition;
}

void TrackAnalyser::open(QUrl url)
{
    //To avoid delays load track in another thread
    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName()<<" url="<<url;
    QFuture<void> future = QtConcurrent::run( this, &TrackAnalyser::asyncOpen,url);
    p->watcher.setFuture(future);
}

void TrackAnalyser::asyncOpen(QUrl url)
{
    p->mutex.lock();
    m_GainDB = GAIN_INVALID;
    m_StartPosition = QTime(0,0);

    sync_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);


    GstElement *l_src = gst_bin_get_by_name(GST_BIN(pipeline), "localsrc");
    g_object_set (G_OBJECT (l_src), "location", (const char*)url.toLocalFile().toUtf8(), NULL);

    sync_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);

    m_MaxPosition=length();
    m_EndPosition=m_MaxPosition;
    m_finished=false;

    gst_object_unref(l_src);
    p->mutex.unlock();
}

void TrackAnalyser::loadThreadFinished()
{
    // async load in player done
    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName();
    start();
}

void TrackAnalyser::start()
{
    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName();
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
}


bool TrackAnalyser::close()
{
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
    return true;
}


QTime TrackAnalyser::length()
{
    if (pipeline) {

        gint64 value=0;

#ifdef GST_API_VERSION_1
        if(gst_element_query_duration(pipeline, GST_FORMAT_TIME, &value)) {
#else
        GstFormat fmt = GST_FORMAT_TIME;
        if(gst_element_query_duration(pipeline, &fmt, &value)) {
#endif

            return QTime(0,0).addMSecs( static_cast<uint>( ( value / GST_MSECOND ) )); // nanosec -> msec
        }
    }
    return m_MaxPosition;
}


void TrackAnalyser::messageReceived(GstMessage *message)
{
        switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_ERROR: {
                GError *err;
                gchar *debug;
                gst_message_parse_error (message, &err, &debug);
                QString str;
                str = "Error #"+QString::number(err->code)+" in module "+QString::number(err->domain)+"\n"+QString::fromUtf8(err->message);
                if(err->code == 6 && err->domain == 851) {
                        str += "\nMay be you should to install gstreamer0.10-plugins-ugly or gstreamer0.10-plugins-bad";
                }
                qDebug()<< "Gstreamer error:"<< str;
                g_error_free (err);
                g_free (debug);
                need_finish();
                break;
        }
        case GST_MESSAGE_EOS:{
                qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName()<<" End of track reached";
                need_finish();
                break;
        }
        case GST_MESSAGE_ELEMENT:{
                GstClockTime timestamp;

                const GstStructure *s = gst_message_get_structure (message);
                const gchar *name = gst_structure_get_name (s);

                if (strcmp (name, "cutter") == 0) {
                    gst_structure_get_clock_time (s, "timestamp", &timestamp);
                    const GValue *value;
                    value=gst_structure_get_value (s, "above");
                    bool isSilent=!g_value_get_boolean(value);

                    //if we detect a falling edge, set EndPostion to this
                    if (isSilent)
                        m_EndPosition=QTime(0,0).addMSecs( static_cast<uint>( ( timestamp / GST_MSECOND ) )); // nanosec -> msec
                    else
                    {
                        //if this is the first rising edge, set StartPosition
                        if (m_StartPosition==QTime(0,0) && m_EndPosition==m_MaxPosition)
                            m_StartPosition=QTime(0,0).addMSecs( static_cast<uint>( ( timestamp / GST_MSECOND ) )); // nanosec -> msec

                        //if we detect a rising edge, set EndPostion to track end
                        m_EndPosition=m_MaxPosition;
                    }
                    //qDebug() << Q_FUNC_INFO <<QTime(0,0).addMSecs( static_cast<uint>( ( timestamp / GST_MSECOND ) ))<< " silent:" << isSilent;
                }
            break;
          }

        case GST_MESSAGE_TAG:{

                GstTagList *tags = NULL;
                gst_message_parse_tag (message, &tags);
                if (gst_tag_list_get_double (tags, GST_TAG_TRACK_GAIN, &m_GainDB))
                {
                    qDebug() << Q_FUNC_INFO << ": Gain-db:" << m_GainDB;
                    qDebug() << Q_FUNC_INFO << ": Gain-norm:" << pow (10, m_GainDB / 20);
                }
            }

        default:
                break;
        }

}

void TrackAnalyser::need_finish()
{
        m_finished=true;
        Q_EMIT finish();
}



