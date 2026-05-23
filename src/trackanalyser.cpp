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

#include <QWidget>
#include <QMutexLocker>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <QVector>
#include <QMetaObject>

#define AUDIOFREQ 32000
#define SCAN_DURATION 60
static const guint spect_bands = 8;

struct TrackAnalyser_Private
{
        QFutureWatcher<void> watcher;
        QMutex mutex;
        guint64 fft_res;
        float lastSpectrum[spect_bands];
        QList<float> spectralFlux;
    QList<GstClockTime> spectralFluxTimes;
        int bpm;
    GstClockTime tempoStartTimestamp;
    bool tempoWindowStarted;
    int tempoScanDurationSeconds;
        GstElement *src, *conv, *sink, *cutter, *audio, *analysis, *spectrum;
        QTimer* tempoTimeout;
        bool finishQueued;
        bool shuttingDown;
        TrackAnalyser::modeType analysisMode;
};

TrackAnalyser::TrackAnalyser(QWidget *parent) :
        QWidget(parent),
    pipeline(nullptr), m_finished(false)
    , p( new TrackAnalyser_Private )
{
    p->fft_res = 120; // lower message rate reduces analysis cost while keeping enough onset resolution
    p->bpm = 0;
    p->analysisMode = STANDARD;
    p->tempoStartTimestamp = 0;
    p->tempoWindowStarted = false;
    p->tempoScanDurationSeconds = 20;
    p->finishQueued = false;
    p->shuttingDown = false;
    p->tempoTimeout = new QTimer(this);
    p->tempoTimeout->setSingleShot(true);
    connect(p->tempoTimeout, &QTimer::timeout, this, [this]() {
        if (p->analysisMode == TEMPO)
            need_finish();
    });
    for (guint i = 0; i < spect_bands; ++i)
        p->lastSpectrum[i]=0.0;

    gst_init (nullptr, nullptr);
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
    {
        QMutexLocker locker(&p->mutex);
        p->shuttingDown = true;
    }

    p->tempoTimeout->stop();
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);

    if (p->watcher.isRunning())
        p->watcher.waitForFinished();

    cleanup();
    delete p;
    p = nullptr;
}


void cb_newpad_ta (GstElement *src,
                   GstPad     *new_pad,
                   gpointer    data)
{
    TrackAnalyser* instance = (TrackAnalyser*)data;
            instance->newpad(src, new_pad, data);
}


void TrackAnalyser::newpad (GstElement *src,
                   GstPad     *new_pad,
                   gpointer    data)
{
        GstCaps *caps;
        GstStructure *str;
        GstPad *sink_pad;

        /* only link once */
        GstElement *bin = gst_bin_get_by_name(GST_BIN(pipeline), "convert");
        sink_pad = gst_element_get_static_pad (bin, "sink");

        if (GST_PAD_IS_LINKED (sink_pad)) {
                g_object_unref (sink_pad);
                return;
        }

        /* check media type */
        caps = gst_pad_query_caps(new_pad, nullptr);
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
        pipeline = gst_pipeline_new ("pipeline");
        bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
        p->src = gst_element_factory_make ("uridecodebin", "source");

        g_signal_connect (p->src, "pad-added", G_CALLBACK (cb_newpad_ta), this);

        p->conv = gst_element_factory_make ("audioconvert", "convert");
        p->spectrum = gst_element_factory_make ("spectrum", "spectrum");
        p->analysis = gst_element_factory_make ("rganalysis", "analysis");
        p->cutter = gst_element_factory_make ("cutter", "cutter");
        p->sink = gst_element_factory_make ("fakesink", "sink");

        g_object_set (p->analysis, "message", TRUE, NULL);
        g_object_set (p->analysis, "num-tracks", 1, NULL);
        // ReplayGain reference levels are typically positive dB values; a negative
        // value is rejected by the property on this build.
        g_object_set (p->analysis, "reference-level", 89.0, NULL);
        g_object_set (p->cutter, "threshold-dB", -25.0, NULL);

        g_object_set (G_OBJECT (p->spectrum), "bands", spect_bands, "threshold", -80,
              "post-messages", TRUE, "interval", GST_SECOND / p->fft_res, NULL);


        gst_bin_add_many (GST_BIN (pipeline), p->src, p->conv, p->analysis, p->cutter, p->spectrum, p->sink, NULL);
        gst_element_link (p->conv, p->analysis);
        gst_element_link (p->analysis, p->cutter);
        gst_element_link (p->cutter, p->sink);

        gst_bus_set_sync_handler(bus, bus_cb, this, nullptr);

        return pipeline;
}

int TrackAnalyser::bpm()
{
    return  p->bpm;
}

QTime TrackAnalyser::beatPosition()
{
    return m_BeatPosition;
}

int TrackAnalyser::tempoScanDurationSeconds() const
{
    return p->tempoScanDurationSeconds;
}

void TrackAnalyser::setTempoScanDurationSeconds(int seconds)
{
    p->tempoScanDurationSeconds = qBound(5, seconds, 60);
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

void TrackAnalyser::setPosition(QTime position)
{
        int time_milliseconds=QTime(0,0).msecsTo(position);
        gint64 time_nanoseconds=( time_milliseconds * GST_MSECOND );
        gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                                 GST_SEEK_TYPE_SET, time_nanoseconds,
                                 GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
        qDebug() << Q_FUNC_INFO <<":"<<" position="<<position;
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

        gst_element_link (p->conv, p->spectrum);//spectrum take too much time
        gst_element_link (p->spectrum, p->sink);
        break;
    default:
        gst_element_unlink (p->conv, p->spectrum);
        gst_element_unlink (p->spectrum, p->sink);

        gst_element_link (p->conv, p->analysis);
        gst_element_link (p->analysis, p->cutter);
        gst_element_link (p->cutter, p->sink);
        m_StartPosition = m_MaxPosition = QTime(0,0);
    }
}

void TrackAnalyser::open(QUrl url)
{
    //To avoid delays load track in another thread
    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName()<<" url="<<url;
    if (p->watcher.isRunning())
        p->watcher.waitForFinished();

    QFuture<void> future = QtConcurrent::run([this, url]() { asyncOpen(url); });
    p->watcher.setFuture(future);
}

void TrackAnalyser::asyncOpen(QUrl url)
{
    {
        QMutexLocker locker(&p->mutex);
        if (p->shuttingDown)
            return;
        m_GainDB = GAIN_INVALID;
        p->spectralFlux.clear();
        p->spectralFluxTimes.clear();
        p->bpm = 0;
        p->tempoStartTimestamp = 0;
        p->tempoWindowStarted = false;
        p->finishQueued = false;
        m_finished = false;
        m_BeatPosition = QTime();
    }

    sync_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

    {
        QMutexLocker locker(&p->mutex);
        if (p->shuttingDown)
            return;
    }

    GstElement *src = gst_bin_get_by_name(GST_BIN(pipeline), "source");
    g_object_set (G_OBJECT (src), "uri", (const char*)url.toString().toUtf8(), nullptr);

    sync_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);

    gst_object_unref(src);
}

void TrackAnalyser::loadThreadFinished()
{
    // async load in player done
    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName()<<" analysisMode="<<p->analysisMode;

    if ( p->analysisMode == TrackAnalyser::TEMPO ){
        //setPosition( m_EndPosition.addSecs(-SCAN_DURATION) );
        setPosition(m_StartPosition);
    }
    else {
        m_EndPosition=length();
    }
    start();
}

void TrackAnalyser::start()
{
    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName();
    if (p->analysisMode == TEMPO)
        p->tempoTimeout->start((p->tempoScanDurationSeconds + 2) * 1000);
    else
        p->tempoTimeout->stop();
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

        if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &value)) {
            //qDebug() << Q_FUNC_INFO <<  " new value:" <<value;
            m_MaxPosition = QTime(0,0).addMSecs( static_cast<uint>( ( value / GST_MSECOND ) )); // nanosec -> msec
        }
    }
    //qDebug() << Q_FUNC_INFO <<  " return:" <<m_MaxPosition;
    return m_MaxPosition;
}


void TrackAnalyser::messageReceived(GstMessage *message)
{
    {
        QMutexLocker locker(&p->mutex);
            if (p->shuttingDown || m_finished || p->finishQueued)
        return;
    }

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
                    //gdouble freq = (gdouble) ((AUDIOFREQ / 2) * i + AUDIOFREQ / 4) / spect_bands;
                    mag = gst_value_list_get_value (magnitudes, i);
                    mag_value = pow (10.0, g_value_get_float (mag)/ 20.0);
                    float value = (mag_value - p->lastSpectrum[i]);
                    p->lastSpectrum[i] = mag_value;
                    flux += value < 0? 0: value;
                    //qDebug() << Q_FUNC_INFO <<"freq:"<<freq<<" flux:"<<flux;
                  }
                  //Spectral flux (comparing the power spectrum for one frame against the previous frame)
                  //for onset detection
                  {
                      QMutexLocker locker(&p->mutex);
                      p->spectralFlux.append(flux);
                      p->spectralFluxTimes.append(timestamp);
                  }

                  if (p->analysisMode == TrackAnalyser::TEMPO) {
                      if (!p->tempoWindowStarted) {
                          p->tempoWindowStarted = true;
                          p->tempoStartTimestamp = timestamp;
                      }

                      const GstClockTime scanWindow = static_cast<GstClockTime>(p->tempoScanDurationSeconds) * GST_SECOND;
                      const int maxFrames = p->tempoScanDurationSeconds * static_cast<int>(p->fft_res);
                      if (timestamp >= p->tempoStartTimestamp + scanWindow
                          || p->spectralFlux.size() >= maxFrames) {
                          need_finish();
                      }
                  }

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
                        m_EndPosition=length();
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
    {
        QMutexLocker locker(&p->mutex);
        if (m_finished || p->finishQueued)
            return;
        p->finishQueued = true;
    }

    // Finalize on the Qt thread to avoid UI-thread races from Gst callback threads.
    QMetaObject::invokeMethod(this, "finalizeAnalysis", Qt::QueuedConnection);
}

void TrackAnalyser::finalizeAnalysis()
{
    TrackAnalyser::modeType mode;
    {
        QMutexLocker locker(&p->mutex);
        if (m_finished)
            return;
        m_finished = true;
        p->finishQueued = false;
        mode = p->analysisMode;
    }

    p->tempoTimeout->stop();
    // Non-blocking stop to avoid potential bus callback deadlocks.
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);

    if (mode == TEMPO) {
        detectTempo();
        Q_EMIT finishTempo();
    } else {
        Q_EMIT finishGain();
    }
}

void TrackAnalyser::detectTempo()
{
    static const int kMinBpm = 70;
    static const int kMaxBpm = 200;
    const int THRESHOLD_WINDOW_SIZE = 10;
    const float MULTIPLIER = 1.5f;
    QList<float> prunedSpectralFlux;
    QList<float> threshold;
    QList<float> peaks;

    QList<float> spectralFlux;
    {
        QMutexLocker locker(&p->mutex);
        spectralFlux = p->spectralFlux;
    }

    if (spectralFlux.isEmpty()) {
        p->bpm = 0;
        m_BeatPosition = m_StartPosition;
        return;
    }

    //calculate the running average for spectral flux.
     for( int i = 0; i < spectralFlux.size(); i++ )
    {
       int start = qMax( 0, i - THRESHOLD_WINDOW_SIZE );
       int end = qMin( spectralFlux.size() - 1, i + THRESHOLD_WINDOW_SIZE );
       float mean = 0;
       for( int j = start; j <= end; j++ )
          mean += spectralFlux.at(j);
         mean /= (end - start + 1);
       threshold.append( mean * MULTIPLIER );
    }

    //take only the signifikat onsets above threshold
    for( int i = 0; i < threshold.size(); i++ )
    {
         if( threshold.at(i) <= spectralFlux.at(i) )
             prunedSpectralFlux.append( spectralFlux.at(i) - threshold.at(i) );
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

    // Build an onset list and estimate BPM by weighted interval voting.
    QVector<int> onsets;
    onsets.reserve(peaks.size() / 3);
    for (int i = 1; i < peaks.size() - 1; ++i) {
        if (peaks.at(i) > 0.0f && peaks.at(i) >= peaks.at(i - 1) && peaks.at(i) > peaks.at(i + 1))
            onsets.append(i);
    }

    QVector<double> score(241, 0.0);
    for (int i = 0; i < onsets.size(); ++i) {
        const int base = onsets.at(i);
        const float baseWeight = qMax(0.01f, peaks.at(base));
        const int upper = qMin(onsets.size(), i + 10);
        for (int j = i + 1; j < upper; ++j) {
            const int delta = onsets.at(j) - base;
            if (delta <= 0)
                continue;

            double bpm = (static_cast<double>(p->fft_res) * 60.0) / static_cast<double>(delta);

            // Fold harmonics into a broad DJ-relevant range.
            while (bpm < static_cast<double>(kMinBpm))
                bpm *= 2.0;
            while (bpm > static_cast<double>(kMaxBpm))
                bpm *= 0.5;

            if (bpm >= static_cast<double>(kMinBpm) && bpm <= static_cast<double>(kMaxBpm)) {
                const int bpmBin = qBound(kMinBpm, qRound(bpm), kMaxBpm);
                const float pairWeight = qMax(0.01f, peaks.at(onsets.at(j)));
                score[bpmBin] += static_cast<double>(baseWeight * pairWeight);
            }
        }
    }

    int bestBpm = 0;
    double bestScore = 0.0;
    for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin) {
        if (score[bpmBin] > bestScore) {
            bestScore = score[bpmBin];
            bestBpm = bpmBin;
        }
    }

    auto supportFor = [&](int bpmBin) {
        if (bpmBin < kMinBpm || bpmBin > kMaxBpm)
            return 0.0;

        double support = 0.0;
        for (int d = -2; d <= 2; ++d) {
            const int idx = qBound(kMinBpm, bpmBin + d, kMaxBpm);
            support += score[idx] * ((d == 0) ? 1.0 : 0.7);
        }

        if (bpmBin * 2 <= kMaxBpm)
            support += 0.30 * score[bpmBin * 2];
        if (bpmBin / 2 >= kMinBpm)
            support += 0.20 * score[bpmBin / 2];

        return support;
    };

    int votedBpm = bestBpm;
    double votedSupport = supportFor(votedBpm);
    if (votedBpm > 0) {
        const int halfBpm = qRound(votedBpm * 0.5);
        const int doubleBpm = votedBpm * 2;
        const double halfSupport = supportFor(halfBpm);
        const double doubleSupport = supportFor(doubleBpm);

        if (halfSupport > votedSupport * 1.15) {
            votedBpm = halfBpm;
            votedSupport = halfSupport;
        }
        if (doubleSupport > votedSupport * 1.15) {
            votedBpm = doubleBpm;
            votedSupport = doubleSupport;
        }
    }

    const int autoCorrBpm = qRound(AutoCorrelation(peaks, peaks.count(), kMinBpm, kMaxBpm, p->fft_res));
    int finalBpm = votedBpm;
    if (finalBpm == 0) {
        finalBpm = autoCorrBpm;
    } else if (autoCorrBpm >= kMinBpm && autoCorrBpm <= kMaxBpm) {
        if (qAbs(autoCorrBpm - finalBpm) <= 3) {
            finalBpm = qRound(0.6 * finalBpm + 0.4 * autoCorrBpm);
        } else if (supportFor(autoCorrBpm) > votedSupport * 1.20) {
            finalBpm = autoCorrBpm;
        }
    }

    p->bpm = qBound(0, finalBpm, kMaxBpm);
    qDebug() << Q_FUNC_INFO << "interval-vote bpm:" << p->bpm;

    // Use the strongest early onset as a stable beat-cue phase anchor.
    int strongestPeakIdx = -1;
    float strongestPeak = 0.0f;
    for (int i = 0; i < peaks.size(); ++i) {
        if (peaks.at(i) > strongestPeak) {
            strongestPeak = peaks.at(i);
            strongestPeakIdx = i;
        }
    }

    if (strongestPeakIdx >= 0 && strongestPeak > 0.0f) {
        const qint64 offsetMs = static_cast<qint64>((1000.0 * strongestPeakIdx) / p->fft_res);
        m_BeatPosition = m_StartPosition.addMSecs(static_cast<int>(offsetMs));
    } else {
        m_BeatPosition = m_StartPosition;
    }
}

float TrackAnalyser::AutoCorrelation( QList<float> buffer, int frames, int minBpm, int maxBpm, int sampleRate)
{

    float maxCorr = 0;
    int maxLag = 0;
    int maxOffset = sampleRate * 60 / minBpm;
    int minOffset = sampleRate * 60 / maxBpm;
    if (frames > buffer.count()) frames=buffer.count();

    for (int lag = minOffset; lag < maxOffset; lag++)
    {
        float corr = 0;
        for (int i = 0; i < frames-lag; i++)
        {
            corr += (buffer.at(i+lag) * buffer.at(i));
        }

        //float bpm = sampleRate * 60.0 / lag;

        //calculate rating according then common bpm of 120 (log normal distribution)
        //float rate = (float) qExp( -0.5 * qPow(( log( bpm / std_bpm ) / log(2) / std_dev),2.0));
        //corr = corr * rate;
        //We dont care about tempo-harmonics issue -> music fits anyway -> factor: 2x or 0.5x

        if (corr > maxCorr)
        {
            //qDebug() << Q_FUNC_INFO << "corr: "<<corr<<" bpm:"<<bpm;
            maxCorr = corr;
            maxLag = lag;
        }

    }
    if (maxLag>0)
        return sampleRate * 60.0 / maxLag;
    else
        return 0.0;
}
