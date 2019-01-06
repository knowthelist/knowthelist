/*
    Copyright (C) 2005-2019 Mario Stephan <mstephan@shared-files.de>
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

#include <QtGui>
#if QT_VERSION >= 0x050000
 #include <QtConcurrent/QtConcurrent>
#else
 #include <QtConcurrentRun>
#endif

#if defined(Q_OS_DARWIN)
    #include <CoreAudio/CoreAudio.h>
    #include <CoreAudio/AudioHardware.h>
#elif defined(Q_OS_WIN32)
    #include <dsound.h>
#elif defined(Q_OS_UNIX)
    #include <alsa/asoundlib.h>
#endif

#include "monitorplayer.h"

void MonitorPlayer::sync_set_state(GstElement* element, GstState state)
{ GstStateChangeReturn res; \
        res = gst_element_set_state (GST_ELEMENT (element), state); \
        if(res == GST_STATE_CHANGE_FAILURE) return; \
        if(res == GST_STATE_CHANGE_ASYNC) { \
                GstState state; \
                        res = gst_element_get_state(GST_ELEMENT (element), &state, NULL, 1000000000/*GST_CLOCK_TIME_NONE*/); \
                        if(res == GST_STATE_CHANGE_FAILURE || res == GST_STATE_CHANGE_ASYNC) return; \
} }



void cb_newpad_mp (GstElement *src,
                   GstPad     *new_pad,
                   gpointer    data)
{
    MonitorPlayer* instance = (MonitorPlayer*)data;
            instance->newpad(src, new_pad, data);
}


void MonitorPlayer::newpad (GstElement *src,
                   GstPad     *new_pad,
                   gpointer    data)
{
        GstCaps *caps;
        GstStructure *str;
        GstPad *sink_pad;

        /* only link once */
        GstElement *bin = gst_bin_get_by_name(GST_BIN(pipeline), "convert");
        sink_pad = gst_element_get_static_pad (bin, "sink");

        gst_object_unref(bin);

        if (GST_PAD_IS_LINKED (sink_pad)) {
                g_object_unref (sink_pad);
                return;
        }

        /* check media type */
#ifdef GST_API_VERSION_1
        caps = gst_pad_query_caps (new_pad, nullptr);
#else
        caps = gst_pad_get_caps (new_pad);
#endif
        str = gst_caps_get_structure (caps, 0);
        if (!g_strrstr (gst_structure_get_name (str), "audio")) {
                gst_caps_unref (caps);
                gst_object_unref (sink_pad);
                return;
        }
        gst_caps_unref (caps);

        /* link'n'play */
        gst_pad_link (new_pad, sink_pad);
}

struct MonitorPlayerPrivate
{
    QFutureWatcher<void> watcher;
    QMutex mutex;
    bool isStarted;
    bool isLoaded;
    bool isDisabled;
    QString error;
    QString deviceName;
    QString deviceID;
    QMap<QString, QString> devices;
    uint length;
    uint position;
    dsDevice dev;
    double rms_l;
    double rms_r;
};

MonitorPlayer::MonitorPlayer(QWidget *parent):
        QWidget(parent)
    , p( new MonitorPlayerPrivate )
    , pipeline(nullptr), bus(nullptr), Gstart(0), Glength(0)
{
    p->isStarted=false;
    p->isLoaded=false;
    readDevices();
    p->deviceID=defaultDeviceID();

    connect(&p->watcher, SIGNAL(finished()), this, SLOT(loadThreadFinished()));
}

MonitorPlayer::~MonitorPlayer()
{
    cleanup();
    delete p;
    p = nullptr;
}

GstBusSyncReply MonitorPlayer::bus_cb (GstBus *bus, GstMessage *msg, gpointer data)
{
    Q_UNUSED(bus);

    MonitorPlayer* instance = (MonitorPlayer*)data;
            instance->messageReceived(msg);
    return GST_BUS_PASS;
}

void MonitorPlayer::cleanup()
{
        if(pipeline) sync_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
        if(bus) gst_object_unref (bus);
        if(pipeline) gst_object_unref(G_OBJECT(pipeline));
}

bool MonitorPlayer::prepare()
{
    // Init Gst
    qDebug() << Q_FUNC_INFO << " " << "START";
    QString caps_value;

    gst_init (nullptr, nullptr);

    //prepare
    GstElement *src, *conv, *resample, *sink, *level, *vol;
    GstCaps *caps;
    pipeline = gst_pipeline_new ("pipeline");
    bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    src = gst_element_factory_make ("uridecodebin", "source");


#ifdef GST_API_VERSION_1
        caps_value = "audio/x-raw";
#else
        caps_value = "audio/x-raw-int";
#endif
        caps = gst_caps_new_simple (caps_value.toLatin1().data(),
                                    "channels", G_TYPE_INT, 2, NULL);
        g_signal_connect (src, "pad-added", G_CALLBACK (cb_newpad_mp), this);


        conv = gst_element_factory_make ("audioconvert", "convert");
        vol = gst_element_factory_make ("volume", "volume");
        resample = gst_element_factory_make ("audioresample", "resample");
        level = gst_element_factory_make ("level", "level");

        #if defined(Q_OS_DARWIN)
            sink = gst_element_factory_make ("osxaudiosink", "sink");
            g_object_set (sink, "device", 0, NULL);
            //g_object_set (sink, "volume", 9.5, NULL);
        #elif defined(Q_OS_WIN32)
            sink = gst_element_factory_make ("directsoundsink", "sink");
            g_object_set (sink, "device", NULL, NULL);
            //g_object_set (sink, "volume", 99.5, NULL);
        #elif defined(Q_OS_UNIX)
            sink = gst_element_factory_make ("alsasink", "sink");
            g_object_set (sink, "device", "default", NULL);
            //g_object_set (sink, "volume", 0.5, NULL);
        #else
            sink = gst_element_factory_make ("fakesink", "sink");
            g_object_set (sink, "device", NULL, NULL);
        #endif

        gst_bin_add_many (GST_BIN (pipeline), src, conv, resample, level, vol, sink, NULL);
        gst_element_link (conv, resample);
        gst_element_link_filtered (resample, level, caps);
        gst_element_link (level,vol);
        gst_element_link (vol, sink);

        gst_element_set_state (src, GST_STATE_NULL);

#ifdef GST_API_VERSION_1
        gst_bus_set_sync_handler (bus, bus_cb, this, nullptr);
#else
        gst_bus_set_sync_handler (bus, bus_cb, this);
#endif

        qDebug() << Q_FUNC_INFO << " " << "END";

        return pipeline;
}

bool MonitorPlayer::ready()
{
        return pipeline;
}

void MonitorPlayer::open(QUrl url)
{
    if (p->isDisabled) {
        return;
    }
    // To avoid delays, load track in another thread
    qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName() << " url=" << url;
    QFuture<void> future = QtConcurrent::run( this, &MonitorPlayer::asyncOpen,url);
    p->watcher.setFuture(future);
}

void MonitorPlayer::asyncOpen(QUrl url)
{
    p->mutex.lock();
    p->length = 0;
    p->isLoaded = false;
    p->error = "";

    sync_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

    GstElement *src = gst_bin_get_by_name(GST_BIN(pipeline), "source");
    g_object_set (G_OBJECT (src), "uri", (const char*)url.toString().toUtf8(), NULL);

    sync_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);
    setPosition(QTime(0,0));

    gst_object_unref(src);
    p->mutex.unlock();
}

void MonitorPlayer::loadThreadFinished()
{
    // async load in MonitorPlayerGst done
    qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName();
    p->isLoaded=true;
    if (p->isStarted) {
        play();
    }
    emit loadFinished();
}

void MonitorPlayer::play()
{

    p->isStarted = true;
    qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName();
    if (p->isLoaded) {
          gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
    }
}

void MonitorPlayer::stop()
{
    p->isStarted=false;
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY);
}

void MonitorPlayer::pause()
{
    if(isPlaying()){
        p->isStarted=false;
        gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);
      }
}

bool MonitorPlayer::close()
{
     gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
     return true;
}

void MonitorPlayer::setPosition(QTime position)
{
        int time_milliseconds = QTime(0,0).msecsTo(position);
        gint64 time_nanoseconds = ( time_milliseconds * GST_MSECOND );
        gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                                 GST_SEEK_TYPE_SET, time_nanoseconds,
                                 GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
        p->position = time_milliseconds;
        emit positionChanged();
}

QTime MonitorPlayer::position()
{
    if (pipeline) {

        gint64 value=0;

#ifdef GST_API_VERSION_1
        if(gst_element_query_position(pipeline, GST_FORMAT_TIME, &value)) {
#else
        GstFormat fmt = GST_FORMAT_TIME;
        if(gst_element_query_position(pipeline, &fmt, &value)) {
#endif
            p->position = static_cast<uint>( ( value / GST_MSECOND ) );
            return QTime(0,0).addMSecs( p->position ); // nanosec -> msec
        }
        return  QTime(0,0).addMSecs(  p->position ); // nanosec -> msec
    }
    return QTime(0,0);
}

QTime MonitorPlayer::length()
{
    gint64 value=0;

    if ( p->length == 0 && pipeline){

#ifdef GST_API_VERSION_1
        if(gst_element_query_duration(pipeline, GST_FORMAT_TIME, &value)) {
#else
        GstFormat fmt = GST_FORMAT_TIME;
        if(gst_element_query_duration(pipeline, &fmt, &value)) {
#endif
            p->length = static_cast<uint>( ( value / GST_MSECOND ));
        }
    }
    return QTime(0,0).addMSecs(  p->length ); // nanosec -> msec
}

double  MonitorPlayer::volume()
{
        gdouble vol = 0;

                GstElement *volume = gst_bin_get_by_name(GST_BIN(pipeline), "volume");
                g_object_get (G_OBJECT(volume), "volume", &vol, NULL);
                gst_object_unref(volume);

        return vol;
}

void MonitorPlayer::setVolume(double v)
{
        gdouble vol = 1.00 * v;
        GstElement *volume = gst_bin_get_by_name(GST_BIN(pipeline), "volume");
        g_object_set (G_OBJECT(volume), "volume", vol, NULL);
        gst_object_unref(volume);
}

void MonitorPlayer::disable()
{
    p->isDisabled = true;
}

void MonitorPlayer::enable()
{
    p->isDisabled = false;
}

bool MonitorPlayer::isDisabled()
{
    return p->isDisabled;
}

double MonitorPlayer::levelLeft()
{
    return p->rms_l;
}

double MonitorPlayer::levelRight()
{
    return p->rms_r;
}

bool MonitorPlayer::mediaPlayable()
{
    GstState st;
    gst_element_get_state (GST_ELEMENT (pipeline), &st, 0, 0);
    //qDebug()<<gst_element_state_get_name(st);
    return (st != GST_STATE_NULL);
}

bool MonitorPlayer::isPlaying()
{
    GstState st;
    gst_element_get_state (GST_ELEMENT (pipeline), &st, 0, 0);
    return (st == GST_STATE_PLAYING);
}

QStringList MonitorPlayer::outputDevices()
{
    QStringList outList;
    QMapIterator<QString, QString> i(p->devices);
    while (i.hasNext()) {
         i.next();
         outList << i.value();
     }

    return outList;
}

QString MonitorPlayer::outputDeviceName()
{
    return p->deviceName;
}


QString MonitorPlayer::outputDeviceID()
{
    return p->deviceID;
}

void MonitorPlayer::setOutputDevice(QString deviceName)
{
    // for Mac device is DeviceID
    QMapIterator<QString, QString> i(p->devices);
    while (i.hasNext()) {
         i.next();
         if (i.value()==deviceName)
         {
            p->deviceID = i.key();
            p->deviceName = i.value();
         }
    }

    qDebug()<<"Monitor setDevice to DeviceID:" << p->deviceID << " DevicenName:" << p->deviceName;

    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
#if defined(Q_OS_WIN32)
    g_object_set (sink, "device", p->deviceID.toLatin1().data(), NULL);
#elif defined(Q_OS_DARWIN)
    g_object_set (sink, "device", p->deviceID.toInt(), NULL);
#elif defined(Q_OS_UNIX)
    g_object_set (sink, "device", QString("hw:%1").arg(p->deviceID).toLatin1().data(), NULL);
#endif
    gst_object_unref(sink);

}

#if defined(Q_OS_WIN32)
BOOL CALLBACK DSEnumProc(LPGUID lpGUID, const WCHAR* lpszDesc,
                         const WCHAR* lpszDrvName, void *ctx)
{
        if ( lpGUID )
        {
                QList<dsDevice> *l =reinterpret_cast<QList<dsDevice> *>(ctx);
                *l << dsDevice(QString::fromUtf16(reinterpret_cast<const ushort*>(lpszDesc)), QUuid(*lpGUID));
        }

        return(true);
}
#endif

QString MonitorPlayer::defaultDeviceID()
{
#if defined(Q_OS_WIN32)
    GUID guid;
    //DSDEVID_DefaultVoicePlayback="{DEF00000-9C6D-47ED-AAF1-4DDA8F2B5C03}"
    const GUID defaultguid=QUuid("{DEF00000-9C6D-47ED-AAF1-4DDA8F2B5C03}");
    if (GetDeviceID(&defaultguid, &guid) == DS_OK) {
        return QUuid(guid).toString();
    }
    else
        return QString();
#elif defined(Q_OS_DARWIN)
    return QString("0");
#elif defined(Q_OS_UNIX)
    return QString("0");
#endif

}

void MonitorPlayer::readDevices()
{
    #if defined(Q_OS_WIN32)
        p->devices.clear();
        QList<dsDevice> qlOutput;

        DirectSoundEnumerate(DSEnumProc, reinterpret_cast<void *>(&qlOutput));

        foreach(dev, qlOutput) {
                p->devices.insert(dev.second.toString(),dev.first);
        }


  #elif defined(Q_OS_DARWIN)
    UInt32 dataSize = 0;
    AudioObjectPropertyAddress propertyAddress;
    propertyAddress.mSelector = kAudioHardwarePropertyDevices;
    propertyAddress.mScope  = kAudioDevicePropertyScopeOutput;
    propertyAddress.mElement  = kAudioObjectPropertyElementMaster;


      OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &dataSize);
      if(kAudioHardwareNoError != status)
      {
        qDebug()<<"Unable to get number of audio devices. Error: "<<status;
      }

      UInt32 deviceCount = dataSize / sizeof(AudioDeviceID);

      AudioDeviceID *audioDevices = (AudioDeviceID*)malloc(dataSize);

      status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &dataSize, audioDevices);
      if(kAudioHardwareNoError != status)
      {
        qDebug()<<"AudioObjectGetPropertyData failed when getting device IDs. Error:"<<status;
        free(audioDevices), audioDevices = NULL;
        return;
      }

      p->devices.clear();

      for(UInt32 i = 0; i < deviceCount; i++)
      {
        CFStringRef deviceName = NULL;
        propertyAddress.mSelector = kAudioDevicePropertyStreamConfiguration;  // probe for Inputstream
        if (AudioObjectGetPropertyData(audioDevices[i], &propertyAddress, 0, NULL, &dataSize, &deviceName) != noErr) {
            dataSize = sizeof(deviceName);
            propertyAddress.mSelector = kAudioDevicePropertyDeviceNameCFString;
            propertyAddress.mScope  = kAudioDevicePropertyScopeOutput;

            status = AudioObjectGetPropertyData(audioDevices[i], &propertyAddress, 0, NULL, &dataSize, &deviceName);
            QString devName = CFStringGetCStringPtr(deviceName,CFStringGetSystemEncoding());
            p->devices.insert(QString::number(audioDevices[i]),devName.trimmed() );
        }
      }

      free(audioDevices);
#elif defined(Q_OS_UNIX)
    int idx = 0;
    char* name;

        while(snd_card_get_name(idx,&name) == 0) {
            p->devices.insert(QString::number(idx),QString(name) );
            idx++;
        }
#endif
}

void MonitorPlayer::messageReceived(GstMessage *message)
{

                switch (GST_MESSAGE_TYPE (message)) {
                case GST_MESSAGE_ERROR: {
                        if ( p->error == "")
                        {
                            GError *err;
                            gchar *debug;
                            gst_message_parse_error (message, &err, &debug);
                            p->error = "Error #"+QString::number(err->code)+" in module "+QString::number(err->domain)+"\n"+QString::fromUtf8(err->message);
                            if(err->code == 6 && err->domain == 851) {
                                    p->error += "\nMay be you should to install gstreamerX.XX-plugins-ugly or gstreamerX.XX-plugins-bad";
                            }
                            qDebug()<< "Gstreamer error:"<< p->error;
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
                        p->rms_l = p->rms_r = 0;
                    default:
                            break;
                    }
                    break;
                  }

                case GST_MESSAGE_ELEMENT:{
                        const GstStructure *s = gst_message_get_structure (message);
                        const gchar *src_name=GST_MESSAGE_SRC_NAME (message);

                        if (strcmp (src_name, "level") == 0) {
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
                                  p->rms_l = rms;
                              else
                                  p->rms_r = rms;
                            }

                        }

                    }
                        break;
                default:
                        break;
                }

}
