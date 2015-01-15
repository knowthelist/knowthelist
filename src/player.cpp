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

#include "player.h"

#include <QtGui>
#if QT_VERSION >= 0x050000
 #include <QtConcurrent/QtConcurrent>
#else
 #include <QtConcurrentRun>
#endif


void Player::sync_set_state(GstElement* element, GstState state)
{ GstStateChangeReturn res; \
        res = gst_element_set_state (GST_ELEMENT (element), state); \
        if(res == GST_STATE_CHANGE_FAILURE) return; \
        if(res == GST_STATE_CHANGE_ASYNC) { \
                GstState state; \
                        res = gst_element_get_state(GST_ELEMENT (element), &state, NULL, GST_CLOCK_TIME_NONE); \
                        if(res == GST_STATE_CHANGE_FAILURE || res == GST_STATE_CHANGE_ASYNC) return; \
} }

struct Player::Private
{
        QFutureWatcher<void> watcher;
        QMutex mutex;
        bool isStarted;
        bool isLoaded;
        QString error;
};

void cb_newpad (GstElement *decodebin,
                   GstPad     *pad,
                   gpointer    data)
{
    Player* instance = (Player*)data;
            instance->newpad(decodebin, pad, data);
}


void Player::newpad (GstElement *decodebin,
                   GstPad     *pad,
                   gpointer    data)
{
    Q_UNUSED(decodebin);
    Q_UNUSED(data);

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

        GstElement *valve = gst_bin_get_by_name(GST_BIN(pipeline), "valve");
        g_object_set (valve, "drop", FALSE, NULL);
}

Player::Player(QWidget *parent) :
        QWidget(parent),
    pipeline(0), bus(0), Gstart(0), Glength(0)
  ,m_position(0)
    , p( new Private )
{
    p->isStarted=false;
    p->isLoaded=false;

    connect(&p->watcher, SIGNAL(finished()), this, SLOT(loadThreadFinished()));
}

Player::~Player()
{
    delete p;
    p=0;
    cleanup();
}

GstBusSyncReply Player::bus_cb (GstBus *bus, GstMessage *msg, gpointer data)
{
    Q_UNUSED(bus);
    Player* instance = (Player*)data;
            instance->messageReceived(msg);
    return GST_BUS_PASS;
}

void Player::cleanup()
{
        if(pipeline) sync_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
        if(bus) gst_object_unref (bus);
        if(pipeline) gst_object_unref(G_OBJECT(pipeline));
}

bool Player::prepare()
{
    //Init Gst
    //
    QString caps_value = "audio/x-raw";

      // On mac we bundle the gstreamer plugins with knowthelist
#if defined(Q_OS_DARWIN)
      QString scanner_path;
      QString plugin_path;
      QString registry_filename;

      QDir pd(QCoreApplication::applicationDirPath() + "/../plugins");
      scanner_path = QCoreApplication::applicationDirPath() + "/../plugins/gst-plugin-scanner";
      plugin_path = QCoreApplication::applicationDirPath() + "/../plugins/gstreamer";
      registry_filename = QDesktopServices::storageLocation(QDesktopServices::DataLocation) +
              QString("/gst-registry-%1-bin").arg(QCoreApplication::applicationVersion());

      if ( pd.exists())
        setenv("GST_PLUGIN_SCANNER", scanner_path.toLocal8Bit().constData(), 1);

      if ( pd.exists()) {
        setenv("GST_PLUGIN_PATH", plugin_path.toLocal8Bit().constData(), 1);
        // Never load plugins from anywhere else.
        setenv("GST_PLUGIN_SYSTEM_PATH", plugin_path.toLocal8Bit().constData(), 1);
      }

      if (!registry_filename.isEmpty()) {
        setenv("GST_REGISTRY", registry_filename.toLocal8Bit().constData(), 1);
      }
#elif defined(Q_OS_WIN32)
      QString plugin_path = QCoreApplication::applicationDirPath() + "/plugins";
      QDir pluginDir(plugin_path);
      if ( pluginDir.exists())
        _putenv_s("GST_PLUGIN_PATH", plugin_path.toLocal8Bit());

#endif

      //_putenv_s("GST_DEBUG", "*:4"); //win
      //setenv("GST_DEBUG", "*:3", 1); //unix


      gst_init (0, 0);

    //prepare

        GstElement *dec, *conv,*resample,*sink, *gain, *audio, *vol, *level, *equalizer;
        GstElement *levelout;
        GstPad *audiopad;
        GstCaps *caps;
        pipeline = gst_pipeline_new ("pipeline");
        bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));


#ifdef GST_API_VERSION_1
        dec = gst_element_factory_make ("decodebin", "decoder");
#else
        dec = gst_element_factory_make ("decodebin2", "decoder");
        caps_value = "audio/x-raw-int";
#endif
        caps = gst_caps_new_simple (caps_value.toLatin1().data(),
                                    "channels", G_TYPE_INT, 2, NULL);
        g_signal_connect (dec, "pad-added", G_CALLBACK (cb_newpad), this);
        gst_bin_add (GST_BIN (pipeline), dec);

        audio = gst_bin_new ("audiobin");
        conv = gst_element_factory_make ("audioconvert", "aconv");
        resample = gst_element_factory_make ("audioresample", "resample");
        audiopad = gst_element_get_static_pad (conv, "sink");
        gain = gst_element_factory_make ("audioamplify", "gain");
        level = gst_element_factory_make ("level", "levelintern");
        vol = gst_element_factory_make ("volume", "volume");
        levelout = gst_element_factory_make ("level", "levelout");
        equalizer = gst_element_factory_make ("equalizer-3bands", "equalizer");
        sink = gst_element_factory_make ("autoaudiosink", "sink");

        g_object_set (level, "message", TRUE, NULL);
        g_object_set (levelout, "message", TRUE, NULL);
        g_object_set (level, "peak-ttl", 300000000000, NULL);


        gst_bin_add_many (GST_BIN (audio), conv, resample, level, gain, equalizer, levelout, vol, sink, NULL);
        gst_element_link (conv,resample);

        gst_element_link_filtered (resample, level, caps);
        gst_element_link (level, gain);
        gst_element_link (gain, equalizer);
        gst_element_link (equalizer, vol);
        gst_element_link_filtered (vol, levelout, caps);
        gst_element_link (levelout,sink);

        gst_element_add_pad (audio, gst_ghost_pad_new ("sink", audiopad));
        gst_bin_add (GST_BIN (pipeline), audio);


        GstElement *l_src;
        l_src = gst_element_factory_make ("filesrc", "localsrc");
        gst_bin_add_many (GST_BIN (pipeline), l_src, NULL);
        gst_element_set_state (l_src, GST_STATE_NULL);
        gst_element_link ( l_src,dec);

#ifdef GST_API_VERSION_1
        gst_bus_set_sync_handler (bus, bus_cb, this, NULL);
#else
        gst_bus_set_sync_handler (bus, bus_cb, this);
#endif
        gst_object_unref (audiopad);

        return pipeline;
}


bool Player::ready()
{
        return pipeline;
}

void Player::setGain(double g)
{
        gdouble gain_value = 1.00 * g;

        GstElement *gain = gst_bin_get_by_name(GST_BIN(pipeline), "gain");
        g_object_set (G_OBJECT(gain), "amplification", gain_value, NULL);
        gst_object_unref(gain);
}

void Player::setEqualizer(QString band, double gain)
{
        gdouble gain_value = 1.00 * gain;

        GstElement *equalizer = gst_bin_get_by_name(GST_BIN(pipeline), "equalizer");
        g_object_set (G_OBJECT(equalizer), band.toLatin1().data(), gain_value, NULL);
        gst_object_unref(equalizer);
}

void Player::open(QUrl url)
{
    //To avoid delays load track in another thread
    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName()<<" url="<<url;
    QFuture<void> future = QtConcurrent::run( this, &Player::asyncOpen,url);
    p->watcher.setFuture(future);
}

void Player::asyncOpen(QUrl url)
{
    p->mutex.lock();
    m_length = 0;
    m_position = 0;
    p->isLoaded=false;
    p->error="";
    lastError="";

    sync_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

    GstElement *l_src = gst_bin_get_by_name(GST_BIN(pipeline), "localsrc");
    g_object_set (G_OBJECT (l_src), "location", (const char*)url.toLocalFile().toUtf8(), NULL);
    sync_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);
    setPosition(QTime(0,0));

    gst_object_unref(l_src);
    p->mutex.unlock();
}

void Player::loadThreadFinished()
{
    // async load in player done
    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName();
    p->isLoaded=true;
    emit loadFinished();

    if (p->isStarted) {
        play();
    }
}

void Player::play()
{
    p->isStarted=true;
    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName();
    if (p->isLoaded) {
          gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
    }
}
void Player::stop()
{
    p->isStarted=false;
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY);
}

void Player::pause()
{
    if(isPlaying()){
        p->isStarted=false;
        gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);
      }
}

bool Player::close()
{
     gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
     return true;
}

void Player::setPosition(QTime position)
{
        int time_milliseconds=QTime(0,0).msecsTo(position);
        gint64 time_nanoseconds=( time_milliseconds * GST_MSECOND );
        gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                                 GST_SEEK_TYPE_SET, time_nanoseconds,
                                 GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
        m_position=time_milliseconds;
        emit positionChanged();
}

QTime Player::position()
{
    if (pipeline) {

        gint64 value=0;

#ifdef GST_API_VERSION_1
        if(gst_element_query_position(pipeline, GST_FORMAT_TIME, &value)) {
#else
        GstFormat fmt = GST_FORMAT_TIME;
        if(gst_element_query_position(pipeline, &fmt, &value)) {
#endif
            m_position = static_cast<uint>( ( value / GST_MSECOND ) );
            return QTime(0,0).addMSecs( m_position ); // nanosec -> msec
        }
        return  QTime(0,0).addMSecs(  m_position ); // nanosec -> msec
    }
    return QTime(0,0);
}

QTime Player::length()
{
    gint64 value=0;

    if ( m_length == 0 && pipeline){

#ifdef GST_API_VERSION_1
        if(gst_element_query_duration(pipeline, GST_FORMAT_TIME, &value)) {
#else
        GstFormat fmt = GST_FORMAT_TIME;
        if(gst_element_query_duration(pipeline, &fmt, &value)) {
#endif
            m_length = static_cast<uint>( ( value / GST_MSECOND ));
        }
        qDebug() << Q_FUNC_INFO <<": Can not get duration";
    }
    return QTime(0,0).addMSecs(  m_length ); // nanosec -> msec
}

double  Player::volume()
{
        gdouble vol = 0;

                GstElement *volume = gst_bin_get_by_name(GST_BIN(pipeline), "volume");
                g_object_get (G_OBJECT(volume), "volume", &vol, NULL);
                gst_object_unref(volume);

        return vol;
}

void Player::setVolume(double v)
{
        gdouble vol = 1.00 * v;
        //gdouble vol = 0.01 * v;
                GstElement *volume = gst_bin_get_by_name(GST_BIN(pipeline), "volume");
                g_object_set (G_OBJECT(volume), "volume", vol, NULL);
                gst_object_unref(volume);
}

bool Player::mediaPlayable()
{
    GstState st;
    gst_element_get_state (GST_ELEMENT (pipeline), &st, 0, 0);
    //qDebug()<<gst_element_state_get_name(st);
    return (st != GST_STATE_NULL);
}

bool Player::isPlaying()
{
    GstState st;
    gst_element_get_state (GST_ELEMENT (pipeline), &st, 0, 0);
    return (st == GST_STATE_PLAYING);
}


void Player::messageReceived(GstMessage *message)
{

                switch (GST_MESSAGE_TYPE (message)) {
                case GST_STATE_CHANGE_FAILURE: {
                    qDebug() << Q_FUNC_INFO <<": Gstreamer error:"<< p->error;
                }
                case GST_MESSAGE_ERROR: {
                        if ( p->error == "")
                        {
                            GError *err;
                            gchar *debug;
                            gst_message_parse_error (message, &err, &debug);
                            p->error = "Error #"+QString::number(err->code)+" in module "+QString::number(err->domain)+"\n"+QString::fromUtf8(err->message);
                            if(err->domain != GST_STREAM_ERROR && err->code != GST_STREAM_ERROR_FAILED) {
                                    p->error += "\nMay be you should install more of gstreamer plugins";
                                    lastError = QString::fromUtf8(err->message);
                            }
                            qDebug() << Q_FUNC_INFO <<": Gstreamer error:"<< p->error;
                            g_error_free (err);
                            g_free (debug);
                            Q_EMIT error();
                        }
                        break;
                }
                case GST_MESSAGE_EOS:{
                    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName()<<" End of track reached";
                    Q_EMIT finish();
                    break;
                }
                case GST_MESSAGE_STATE_CHANGED: {
                    GstState old_state, new_state;
                    gst_message_parse_state_changed (message, &old_state, &new_state, NULL);
                    switch(new_state){
                    case GST_STATE_PAUSED:
                    case GST_STATE_NULL:
                        rms_l=rms_r=0;
                        rmsout_l=rmsout_r=0;
                    default:
                            break;
                    }
                    break;
                  }

                case GST_MESSAGE_ELEMENT:{
                        const GstStructure *s = gst_message_get_structure (message);
                        const gchar *src_name=GST_MESSAGE_SRC_NAME (message);

                        if (strcmp (src_name, "levelintern") == 0) {
                            gint channels;
                            gdouble peak_dB;
                            gdouble rms;
                            gint i;

#ifdef GST_API_VERSION_1
                            const GValue *array_val;
                            GValueArray *peak_arr;


                            array_val = gst_structure_get_value (s, "peak");
                            peak_arr = (GValueArray *) g_value_get_boxed (array_val);
                            channels = peak_arr->n_values;

                            for (i = 0; i < channels; ++i) {
                              peak_dB = g_value_get_double (peak_arr->values+i);
#else
                             const GValue *list;
                             const GValue *value;

                             list = gst_structure_get_value (s, "peak");
                             channels = gst_value_list_get_size (list);

                             for (i = 0; i < channels; ++i) {
                                 list = gst_structure_get_value (s, "peak");
                                 value = gst_value_list_get_value (list, i);
                                 peak_dB = g_value_get_double (value);
#endif
                              /* converting from dB to normal gives us a value between 0.0 and 1.0 */
                              rms = pow (10, peak_dB / 20);
                              if (i==0)
                                  rms_l=rms;
                              else
                                  rms_r=rms;
                            }
                        }
                        if (strcmp (src_name, "levelout") == 0) {
                            gint channels;
                            gdouble peak_dB;
                            gdouble rms;
                            gint i;

#ifdef GST_API_VERSION_1
                            const GValue *array_val;
                            GValueArray *peak_arr;


                            array_val = gst_structure_get_value (s, "peak");
                            peak_arr = (GValueArray *) g_value_get_boxed (array_val);
                            channels = peak_arr->n_values;

                            for (i = 0; i < channels; ++i) {
                              peak_dB = g_value_get_double (peak_arr->values+i);
#else
                             const GValue *list;
                             const GValue *value;

                             list = gst_structure_get_value (s, "peak");
                             channels = gst_value_list_get_size (list);

                             for (i = 0; i < channels; ++i) {
                                 list = gst_structure_get_value (s, "peak");
                                 value = gst_value_list_get_value (list, i);
                                 peak_dB = g_value_get_double (value);
#endif

                              /* converting from dB to normal gives us a value between 0.0 and 1.0 */
                              rms = pow (10, peak_dB / 20);
                              if (i==0)
                                  rmsout_l=rms;
                              else
                                  rmsout_r=rms;

                            }

                        }

                    }
                        break;
                default:
                        break;
                }

}


