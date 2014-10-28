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

static guint spect_bands = 9;

struct TrackAnalyser_Private
{
        QFutureWatcher<void> watcher;
        QMutex mutex;
        guint64 fft_res;
        float lastSpectrum[9];
        QList<float> spectralFlux;
        int bpm;
        GstElement *conv, *sink, *cutter, *audio, *analysis, *spectrum;
        TrackAnalyser::modeType analysisMode;
};

TrackAnalyser::TrackAnalyser(QWidget *parent) :
        QWidget(parent),
    pipeline(0), m_finished(false)
    , p( new TrackAnalyser_Private )
{
    p->fft_res = 343; //sample rate for fft samples in Hz
    for (int i=0;i<spect_bands;i++)
        p->lastSpectrum[i]=0.0;

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

#define AUDIOFREQ 32000

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
        GstElement *dec, *audio;
        GstPad *audiopad;

        pipeline = gst_pipeline_new ("pipeline");
        bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

#ifdef GST_API_VERSION_1
        dec = gst_element_factory_make ("decodebin", "decoder");
#else
        dec = gst_element_factory_make ("decodebin2", "decoder");
#endif
        g_signal_connect (dec, "pad-added", G_CALLBACK (cb_newpad_ta), this);
        gst_bin_add (GST_BIN (pipeline), dec);

        audio = gst_bin_new ("audiobin");
        p->conv = gst_element_factory_make ("audioconvert", "conv");
        p->spectrum = gst_element_factory_make ("spectrum", "spectrum");
        p->analysis = gst_element_factory_make ("rganalysis", "analysis");
        p->cutter = gst_element_factory_make ("cutter", "cutter");
        p->sink = gst_element_factory_make ("fakesink", "sink");
        audiopad = gst_element_get_static_pad (p->conv, "sink");

        g_object_set (p->analysis, "message", TRUE, NULL);
        g_object_set (p->analysis, "num-tracks", 1, NULL);
        g_object_set (p->cutter, "threshold-dB", -25.0, NULL);

        g_object_set (G_OBJECT (p->spectrum), "bands", spect_bands, "threshold", -25,
              "post-messages", TRUE, "interval", GST_SECOND / p->fft_res, NULL);


        gst_bin_add_many (GST_BIN (audio), p->conv, p->analysis, p->cutter, p->spectrum, p->sink, NULL);
        gst_element_link (p->conv, p->analysis);
        gst_element_link (p->analysis, p->cutter);
        gst_element_link (p->cutter, p->sink);
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

int TrackAnalyser::bpm()
{
    return  p->bpm;
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

void TrackAnalyser::setMode(modeType mode)
{
    p->analysisMode = mode;
    sync_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

    //divide in multiple analyser due to different running times
    switch (p->analysisMode)
    {
        case TEMPO:
        gst_element_unlink (p->conv, p->analysis);
        gst_element_unlink (p->analysis, p->cutter);
        gst_element_unlink (p->cutter, p->sink);

        gst_element_link (p->conv, p->analysis);
        gst_element_link (p->analysis, p->cutter);
        gst_element_link (p->cutter, p->spectrum); //spectrum take too much time
        gst_element_link (p->spectrum, p->sink);
        break;
    default:
        gst_element_unlink (p->conv, p->analysis);
        gst_element_unlink (p->analysis, p->cutter);
        gst_element_unlink (p->cutter, p->spectrum);
        gst_element_unlink (p->spectrum, p->sink);

        gst_element_link (p->conv, p->analysis);
        gst_element_link (p->analysis, p->cutter);
        gst_element_link (p->cutter, p->sink);
    }
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
    p->spectralFlux.clear();

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

                const GstStructure *s = gst_message_get_structure (message);
                const gchar *name = gst_structure_get_name (s);
                GstClockTime timestamp;
                gst_structure_get_clock_time (s, "timestamp", &timestamp);

                // data for tempo detection
                if (strcmp (name, "spectrum") == 0) {
                  const GValue *magnitudes;
                  const GValue *mag;
                  float mag_value;
                  guint i;

                  magnitudes = gst_structure_get_value (s, "magnitude");

                  float flux = 0;
                  for (i = 0; i < spect_bands; ++i) {
                    //freq = (gdouble) ((AUDIOFREQ / 2) * i + AUDIOFREQ / 4) / spect_bands;
                    mag = gst_value_list_get_value (magnitudes, i);
                    mag_value = pow (10.0, g_value_get_float (mag)/ 20.0);
                    float value = (mag_value - p->lastSpectrum[i]);
                    p->lastSpectrum[i] = mag_value;
                    flux += value < 0? 0: value;
                  }
                  //Spectral flux (comparing the power spectrum for one frame against the previous frame)
                  //for onset detection
                  p->spectralFlux.append( flux );

                }
                // data for Start and End time detection
                if (strcmp (name, "cutter") == 0) {

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
    switch (p->analysisMode)
    {
        case TEMPO:
            detectTempo();
            Q_EMIT finishTempo();
            break;
        default:
            Q_EMIT finishGain();
    }
}

void TrackAnalyser::detectTempo()
{
    int THRESHOLD_WINDOW_SIZE = 10;
    float MULTIPLIER = 1.5f;
    QList<float> prunedSpectralFlux;
    QList<float> threshold;
    QList<float> peaks;

    //calculate the running average for spectral flux.
    for( int i = 0; i < p->spectralFlux.size(); i++ )
    {
       int start = qMax( 0, i - THRESHOLD_WINDOW_SIZE );
       int end = qMin( p->spectralFlux.size() - 1, i + THRESHOLD_WINDOW_SIZE );
       float mean = 0;
       for( int j = start; j <= end; j++ )
          mean += p->spectralFlux.at(j);
       mean /= (end - start);
       threshold.append( mean * MULTIPLIER );
    }

    //take only the signifikat onsets above threshold
    for( int i = 0; i < threshold.size(); i++ )
    {
       if( threshold.at(i) <= p->spectralFlux.at(i) )
          prunedSpectralFlux.append( p->spectralFlux.at(i) - threshold.at(i) );
       else
          prunedSpectralFlux.append( (float)0 );
    }

    //peak detection
    for( int i = 0; i < prunedSpectralFlux.size() - 1; i++ )
    {
       if( prunedSpectralFlux.at(i) > prunedSpectralFlux.at(i+1) )
          peaks.append( prunedSpectralFlux.at(i) );
       else
          peaks.append( (float)0 );
    }

    //time = index * p->fft_res
    int duration = 90 * p->fft_res; //sec

    //use autocorrelation to retrieve time periode of peaks
    float bpm = AutoCorrelation(peaks, duration, 60, 240, p->fft_res);
    qDebug() << Q_FUNC_INFO << "autocorrelation bpm:"<<bpm;

    //tempo-harmonics issue
    if ( bpm < 72.0 ) {
        bpm *= 2;
        qDebug() << Q_FUNC_INFO << "guess bpm:"<<bpm;
    }
    p->bpm = bpm;
}

float TrackAnalyser::AutoCorrelation( QList<float> buffer, int frames, int minBpm, int maxBpm, int sampleRate)
{

    float maxCorr = 0;
    int maxLag = 0;
    float std_bpm = 120.0f;
    float std_dev = 0.8f;

    int maxOffset = sampleRate / (minBpm / 60);
    int minOffset = sampleRate / (maxBpm / 60);
    if (frames > buffer.count()) frames=buffer.count();

    for (int lag = minOffset; lag < maxOffset; lag++)
    {
        float corr = 0;
        for (int i = 0; i < frames-lag; i++)
        {
            corr += (buffer.at(i+lag) * buffer.at(i));
        }

        float bpm = sampleRate * 60 / lag;

        //calculate rating according then common bpm of 120 (log normal distribution)
        float rate = (float) qExp( -0.5 * qPow(( log( bpm / std_bpm ) / log(2) / std_dev),2.0));
        corr = corr * rate;

        if (corr > maxCorr)
        {
            maxCorr = corr;
            maxLag = lag;
        }

    }
    if (maxLag>0)
        return sampleRate * 60 / maxLag;
    else
        return 0.0;
}
