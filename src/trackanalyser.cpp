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
static const guint spect_bands = 64;

struct TrackAnalyser_Private
{
        QFutureWatcher<void> watcher;
        QMutex mutex;
        guint64 fft_res;
        float lastSpectrum[spect_bands];
        QList<float> spectralFlux;
    QList<float> spectralFluxLow;
    QList<GstClockTime> spectralFluxTimes;
        int bpm;
    GstClockTime tempoStartTimestamp;
    bool tempoWindowStarted;
    int tempoScanDurationSeconds;
        GstElement *src, *conv, *sink, *cutter, *audio, *analysis, *spectrum;
        QTimer* tempoTimeout;
        bool finishQueued;
        bool shuttingDown;
            bool tempoFallbackTried;
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
    p->tempoScanDurationSeconds = 24;
    p->finishQueued = false;
    p->shuttingDown = false;
    p->tempoFallbackTried = false;
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
        p->spectralFluxLow.clear();
        p->spectralFluxTimes.clear();
        p->bpm = 0;
        p->tempoStartTimestamp = 0;
        p->tempoWindowStarted = false;
        p->finishQueued = false;
        p->tempoFallbackTried = false;
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
        QTime tempoStart = QTime(0, 0);
        const int scanWindowMs = p->tempoScanDurationSeconds * 1000;
        const int trackLengthMs = QTime(0, 0).msecsTo(length());
        const int maxStartMs = qMax(0, trackLengthMs - scanWindowMs - 1000);

        int preferredStartMs = 0;
        if (trackLengthMs > 300000)
            preferredStartMs = qMax(60000, (trackLengthMs / 2) - (scanWindowMs / 2));
        else if (trackLengthMs > 210000)
            preferredStartMs = qMax(45000, (trackLengthMs * 2) / 5);
        else if (trackLengthMs > 150000)
            preferredStartMs = qMax(30000, trackLengthMs / 3);
        else if (trackLengthMs > 90000)
            preferredStartMs = qMax(15000, trackLengthMs / 5);
        else if (trackLengthMs > 60000)
            preferredStartMs = 5000;

        if (maxStartMs > 0)
            tempoStart = QTime(0, 0).addMSecs(qMin(preferredStartMs, maxStartMs));

        qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName()
                 << " tempoStart=" << tempoStart
                 << " trackLengthMs=" << trackLengthMs
                 << " scanWindowMs=" << scanWindowMs;
        setPosition(tempoStart);
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
                                    float lowFlux = 0;
                  for (i = 0; i < spect_bands; ++i) {
                    //gdouble freq = (gdouble) ((AUDIOFREQ / 2) * i + AUDIOFREQ / 4) / spect_bands;
                    mag = gst_value_list_get_value (magnitudes, i);
                    mag_value = pow (10.0, g_value_get_float (mag)/ 20.0);
                    float value = (mag_value - p->lastSpectrum[i]);
                    p->lastSpectrum[i] = mag_value;
                                        const float positiveValue = value < 0 ? 0 : value;
                                        flux += positiveValue;

                                        // With 64 bands at 32 kHz, the first few bins cover the real low end
                                        // much more cleanly than the old 8-band setup, so we can bias the
                                        // onset envelope toward kick and bass guitar instead of snare energy.
                                        if (i < 6) {
                                            const float bassWeight = (i == 0) ? 4.6f
                                                              : ((i == 1) ? 3.4f
                                                                  : ((i == 2) ? 2.4f
                                                                      : ((i == 3) ? 1.7f
                                                                          : ((i == 4) ? 1.2f : 0.9f))));
                                            lowFlux += positiveValue * bassWeight;
                                        }
                    //qDebug() << Q_FUNC_INFO <<"freq:"<<freq<<" flux:"<<flux;
                  }
                  //Spectral flux (comparing the power spectrum for one frame against the previous frame)
                  //for onset detection
                  {
                      QMutexLocker locker(&p->mutex);
                      p->spectralFlux.append(flux);
                                            p->spectralFluxLow.append(lowFlux);
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
    int collectedTempoFrames = 0;
    {
        QMutexLocker locker(&p->mutex);
        if (m_finished)
            return;
        m_finished = true;
        p->finishQueued = false;
        mode = p->analysisMode;
        collectedTempoFrames = p->spectralFlux.size();
    }

    p->tempoTimeout->stop();
    // Non-blocking stop to avoid potential bus callback deadlocks.
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);

    if (mode == TEMPO) {
        const int minimumUsefulFrames = qMax(120, static_cast<int>(p->fft_res) * 2);
        if (!p->tempoFallbackTried && collectedTempoFrames < minimumUsefulFrames) {
            qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName()
                     << "retry tempo scan from start after short window frames=" << collectedTempoFrames;

            {
                QMutexLocker locker(&p->mutex);
                p->tempoFallbackTried = true;
                p->spectralFlux.clear();
                p->spectralFluxLow.clear();
                p->spectralFluxTimes.clear();
                p->tempoStartTimestamp = 0;
                p->tempoWindowStarted = false;
                p->finishQueued = false;
                m_finished = false;
                p->bpm = 0;
                for (guint i = 0; i < spect_bands; ++i)
                    p->lastSpectrum[i] = 0.0f;
            }

            sync_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
            setPosition(QTime(0, 0));
            start();
            return;
        }

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
    QList<float> spectralFlux;
    QList<float> spectralFluxLow;
    {
        QMutexLocker locker(&p->mutex);
        spectralFlux = p->spectralFlux;
        spectralFluxLow = p->spectralFluxLow;
    }

    if (spectralFlux.isEmpty()) {
        p->bpm = 0;
        m_BeatPosition = m_StartPosition;
        return;
    }

    if (spectralFluxLow.size() != spectralFlux.size())
        spectralFluxLow = spectralFlux;

    auto buildOnsetEnvelope = [](const QList<float>& input, int thresholdWindow, float thresholdMultiplier) {
        QList<float> threshold;
        QList<float> pruned;
        QList<float> smoothed;

        threshold.reserve(input.size());
        pruned.reserve(input.size());
        smoothed.reserve(input.size());

        for (int i = 0; i < input.size(); ++i) {
            const int start = qMax(0, i - thresholdWindow);
            const int end = qMin(input.size() - 1, i + thresholdWindow);
            float mean = 0.0f;
            for (int j = start; j <= end; ++j)
                mean += input.at(j);
            mean /= qMax(1, end - start + 1);
            threshold.append(mean * thresholdMultiplier);
        }

        for (int i = 0; i < input.size(); ++i) {
            const float value = input.at(i) - threshold.at(i);
            pruned.append(value > 0.0f ? value : 0.0f);
        }

        for (int i = 0; i < pruned.size(); ++i) {
            const float prev = (i > 0) ? pruned.at(i - 1) : pruned.at(i);
            const float curr = pruned.at(i);
            const float next = (i + 1 < pruned.size()) ? pruned.at(i + 1) : pruned.at(i);
            smoothed.append((prev + 2.0f * curr + next) * 0.25f);
        }

        return smoothed;
    };

    const QList<float> fullEnv = buildOnsetEnvelope(spectralFlux, 12, 1.35f);
    const QList<float> lowEnv = buildOnsetEnvelope(spectralFluxLow, 14, 1.20f);

    auto pickOnsets = [](const QList<float>& env, int minDistanceFrames) {
        QVector<int> onsets;
        onsets.reserve(env.size() / 4);
        for (int i = 1; i < env.size() - 1; ++i) {
            if (env.at(i) <= 0.0f || env.at(i) < env.at(i - 1) || env.at(i) <= env.at(i + 1))
                continue;

            if (onsets.isEmpty()) {
                onsets.append(i);
                continue;
            }

            const int last = onsets.last();
            if (i - last < minDistanceFrames) {
                if (env.at(i) > env.at(last))
                    onsets.last() = i;
            } else {
                onsets.append(i);
            }
        }
        return onsets;
    };

    const int minDistance = qMax(1, qRound((p->fft_res * 60.0) / 240.0));
    const QVector<int> onsetsFull = pickOnsets(fullEnv, minDistance);
    const QVector<int> onsetsLow = pickOnsets(lowEnv, minDistance + 2);

    QVector<double> score(241, 0.0);

    auto voteTempo = [&](const QVector<int>& onsets, const QList<float>& env, double weight, bool lowBandSource) {
        for (int i = 0; i < onsets.size(); ++i) {
            const int base = onsets.at(i);
            const float baseWeight = qMax(0.01f, env.at(base));
            const int upper = qMin(onsets.size(), i + 18);
            for (int j = i + 1; j < upper; ++j) {
                const int delta = onsets.at(j) - base;
                if (delta <= 0)
                    continue;

                double bpm = (static_cast<double>(p->fft_res) * 60.0) / static_cast<double>(delta);
                while (bpm < static_cast<double>(kMinBpm))
                    bpm *= 2.0;
                while (bpm > static_cast<double>(kMaxBpm))
                    bpm *= 0.5;

                if (bpm >= static_cast<double>(kMinBpm) && bpm <= static_cast<double>(kMaxBpm)) {
                    const int bpmBin = qBound(kMinBpm, qRound(bpm), kMaxBpm);
                    const float pairWeight = qMax(0.01f, env.at(onsets.at(j)));
                    const double pairDistancePenalty = 1.0 / (1.0 + 0.08 * (j - i - 1));
                    const double contribution = weight * static_cast<double>(baseWeight * pairWeight) * pairDistancePenalty;
                    score[bpmBin] += contribution;

                    if (lowBandSource && bpmBin >= 72 && bpmBin <= 90) {
                        const int doubledBin = bpmBin * 2;
                        if (doubledBin >= 140 && doubledBin <= 180)
                            score[doubledBin] += contribution * 0.65;
                    }
                }
            }
        }
    };

    voteTempo(onsetsFull, fullEnv, 1.0, false);
    voteTempo(onsetsLow, lowEnv, 2.25, true);

    QVector<double> smoothedScore = score;
    for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin) {
        const double prev = score[qMax(kMinBpm, bpmBin - 1)];
        const double curr = score[bpmBin];
        const double next = score[qMin(kMaxBpm, bpmBin + 1)];
        smoothedScore[bpmBin] = 0.2 * prev + 1.0 * curr + 0.2 * next;
    }

    int bestBpm = 0;
    double bestScore = 0.0;
    for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin) {
        if (smoothedScore[bpmBin] > bestScore) {
            bestScore = smoothedScore[bpmBin];
            bestBpm = bpmBin;
        }
    }

    auto supportFor = [&](int bpmBin) {
        if (bpmBin < kMinBpm || bpmBin > kMaxBpm)
            return 0.0;

        double support = 0.0;
        for (int d = -2; d <= 2; ++d) {
            const int idx = qBound(kMinBpm, bpmBin + d, kMaxBpm);
            support += smoothedScore[idx] * ((d == 0) ? 1.0 : 0.7);
        }

        // Let high-BPM candidates absorb some evidence from their half-time alias,
        // but do not let low-BPM aliases steal support from the real doubled tempo.
        if (bpmBin * 2 <= kMaxBpm && bpmBin >= 100)
            support += 0.30 * smoothedScore[bpmBin * 2];
        if (bpmBin / 2 >= kMinBpm)
            support += 0.20 * smoothedScore[bpmBin / 2];

        return support;
    };

    auto strongestNear = [&](int center, int radius) {
        int bestBpmBin = center;
        double bestSupport = 0.0;
        const int start = qMax(kMinBpm, center - radius);
        const int end = qMin(kMaxBpm, center + radius);
        for (int candidate = start; candidate <= end; ++candidate) {
            const double candidateSupport = supportFor(candidate);
            if (candidateSupport > bestSupport) {
                bestSupport = candidateSupport;
                bestBpmBin = candidate;
            }
        }
        return QPair<int, double>(bestBpmBin, bestSupport);
    };

    auto lagCorrelation = [&](const QList<float>& env, double lagFrames) {
        const int lag = qRound(lagFrames);
        if (lag <= 0 || lag >= env.size())
            return 0.0;

        double corr = 0.0;
        double energy = 0.0;
        for (int i = 0; i + lag < env.size(); ++i) {
            const double a = env.at(i);
            const double b = env.at(i + lag);
            corr += a * b;
            energy += a * a + b * b;
        }

        return energy > 0.0 ? (2.0 * corr) / energy : 0.0;
    };

    auto strongestLagCorrelation = [&](const QList<float>& env, double lagFrames, int radius) {
        double best = 0.0;
        for (int delta = -radius; delta <= radius; ++delta)
            best = qMax(best, lagCorrelation(env, lagFrames + delta));
        return best;
    };

    int votedBpm = bestBpm;
    QStringList debugNotes;

    auto topBinsToString = [&](const QVector<double>& values, int count) {
        QStringList parts;
        QVector<bool> used(values.size(), false);

        for (int pick = 0; pick < count; ++pick) {
            int bestIdx = -1;
            double bestValue = 0.0;
            for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin) {
                if (used[bpmBin])
                    continue;
                if (values[bpmBin] > bestValue) {
                    bestValue = values[bpmBin];
                    bestIdx = bpmBin;
                }
            }
            if (bestIdx < 0 || bestValue <= 0.0)
                break;
            used[bestIdx] = true;
            parts << QString::number(bestIdx) + "=" + QString::number(bestValue, 'f', 3);
        }

        return parts.join(", ");
    };

    QList<float> combinedEnv;
    combinedEnv.reserve(fullEnv.size());
    for (int i = 0; i < fullEnv.size(); ++i)
        combinedEnv.append(0.6f * fullEnv.at(i) + 0.4f * lowEnv.at(i));

    QVector<double> lagStrength(kMaxBpm + 1, 0.0);
    double maxLagStrength = 0.0;
    for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin) {
        const double lag = (static_cast<double>(p->fft_res) * 60.0) / bpmBin;
        const double combinedBase = strongestLagCorrelation(combinedEnv, lag, 1);
        const double lowBase = strongestLagCorrelation(lowEnv, lag, 1);
        const double combinedDouble = strongestLagCorrelation(combinedEnv, lag * 2.0, 2);
        const double lowDouble = strongestLagCorrelation(lowEnv, lag * 2.0, 2);
        const double combinedHalf = strongestLagCorrelation(combinedEnv, lag * 0.5, 1);
        const double lowHalf = strongestLagCorrelation(lowEnv, lag * 0.5, 1);

        double strength = 0.55 * combinedBase + 1.00 * lowBase;
        if (bpmBin >= 118)
            strength += 0.45 * combinedDouble + 0.85 * lowDouble;
        else
            strength += 0.12 * combinedHalf + 0.18 * lowHalf;

        lagStrength[bpmBin] = strength;
        maxLagStrength = qMax(maxLagStrength, strength);
    }

    if (maxLagStrength > 0.0) {
        for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin)
            lagStrength[bpmBin] /= maxLagStrength;
    }

    const double bestSupportOverall = qMax(0.0001, supportFor(bestBpm));

    const int autoCorrBpm = qRound(AutoCorrelation(combinedEnv, combinedEnv.count(), kMinBpm, kMaxBpm, p->fft_res));
    const int autoCorrLowBpm = qRound(AutoCorrelation(lowEnv, lowEnv.count(), kMinBpm, kMaxBpm, p->fft_res));

    int autoCorrConsensus = autoCorrBpm;
    if (autoCorrLowBpm >= kMinBpm && autoCorrLowBpm <= kMaxBpm) {
        if (autoCorrConsensus < kMinBpm || autoCorrConsensus > kMaxBpm)
            autoCorrConsensus = autoCorrLowBpm;
        else if (qAbs(autoCorrLowBpm - autoCorrConsensus) <= 4)
            autoCorrConsensus = qRound(0.5 * autoCorrConsensus + 0.5 * autoCorrLowBpm);
        else if (qAbs(autoCorrLowBpm * 2 - autoCorrConsensus) <= 4)
            autoCorrConsensus = autoCorrLowBpm * 2;
        else if (qAbs(autoCorrLowBpm - autoCorrConsensus * 2) <= 4)
            autoCorrConsensus = qRound(autoCorrLowBpm * 0.5);
    }

    QVector<int> candidates;
    auto addCandidate = [&](int bpm) {
        if (bpm < kMinBpm || bpm > kMaxBpm)
            return;
        if (!candidates.contains(bpm))
            candidates.append(bpm);
    };

    auto addTopPeaks = [&](const QVector<double>& values, int count, int minSpacing) {
        QVector<bool> used(values.size(), false);
        for (int pick = 0; pick < count; ++pick) {
            int peak = -1;
            double peakValue = 0.0;
            for (int bpmBin = kMinBpm; bpmBin <= kMaxBpm; ++bpmBin) {
                if (used[bpmBin])
                    continue;
                if (values[bpmBin] > peakValue) {
                    peakValue = values[bpmBin];
                    peak = bpmBin;
                }
            }
            if (peak < 0 || peakValue <= 0.0)
                break;

            addCandidate(peak);
            const int start = qMax(kMinBpm, peak - minSpacing);
            const int end = qMin(kMaxBpm, peak + minSpacing);
            for (int bpmBin = start; bpmBin <= end; ++bpmBin)
                used[bpmBin] = true;
        }
    };

    addCandidate(votedBpm);
    addCandidate(bestBpm);
    addCandidate(qRound(votedBpm * 0.5));
    addCandidate(votedBpm * 2);
    addCandidate(qRound(votedBpm * 1.5));
    addCandidate(qRound(votedBpm / 1.5));
    addCandidate(autoCorrConsensus);
    addCandidate(autoCorrLowBpm);
    addCandidate(autoCorrConsensus * 2);
    addCandidate(autoCorrLowBpm * 2);
    addCandidate(qRound(autoCorrConsensus * 1.5));
    addCandidate(qRound(autoCorrConsensus / 1.5));
    addTopPeaks(smoothedScore, 6, 3);

    auto candidateStrength = [&](int bpm) {
        double strength = supportFor(bpm);

        const int half = qRound(bpm * 0.5);
        const int doub = bpm * 2;
        const int threeHalf = qRound(bpm * 1.5);
        const int twoThird = qRound(bpm / 1.5);

        if (half >= kMinBpm)
            strength += 0.10 * supportFor(half);
        // Only high-BPM candidates may absorb evidence from their doubled harmonic.
        // Giving low-BPM candidates credit from their 2x causes persistent half-time aliasing
        // (e.g. 73 BPM candidate steals evidence from the real 146 BPM tempo).
        if (doub <= kMaxBpm && bpm >= 100)
            strength += 0.28 * supportFor(doub);
        if (threeHalf <= kMaxBpm)
            strength += 0.24 * supportFor(threeHalf);
        if (twoThird >= kMinBpm)
            strength += 0.12 * supportFor(twoThird);

        // For low BPM candidates, prefer promoted 1.5x harmonics when similarly strong.
        if (bpm < 95) {
            // Removed: doub credit — causes half-time aliasing for rock/punk tracks.
            if (threeHalf <= kMaxBpm)
                strength += 0.12 * supportFor(threeHalf);
        }

        if (autoCorrConsensus >= kMinBpm && autoCorrConsensus <= kMaxBpm) {
            if (qAbs(bpm - autoCorrConsensus) <= 4)
                strength *= 1.12;
            else if (qAbs(bpm * 2 - autoCorrConsensus) <= 4 || qAbs(autoCorrConsensus * 2 - bpm) <= 4)
                strength *= 1.06;
            else if (qAbs(qRound(bpm * 1.5) - autoCorrConsensus) <= 4 || qAbs(qRound(autoCorrConsensus * 1.5) - bpm) <= 4)
                strength *= 1.05;
        }

        if (bpm >= kMinBpm && bpm <= kMaxBpm) {
            const double lagBoost = lagStrength[bpm];
            const double supportGate = qMin(1.0, supportFor(bpm) / bestSupportOverall);
            strength += supportFor(bpm) * ((bpm >= 118) ? 0.45 : 0.18) * lagBoost * supportGate;
        }

        return strength;
    };

    int finalBpm = 0;
    double finalStrength = 0.0;
    for (int candidate : candidates) {
        const double strength = candidateStrength(candidate);
        if (strength > finalStrength) {
            finalStrength = strength;
            finalBpm = candidate;
        }
    }

    if (finalBpm == 0)
        finalBpm = (autoCorrConsensus >= kMinBpm && autoCorrConsensus <= kMaxBpm) ? autoCorrConsensus : votedBpm;

    QStringList candidateDebug;
    QVector<bool> usedCandidate(candidates.size(), false);
    const int debugCandidateCount = qMin(8, candidates.size());
    for (int pick = 0; pick < debugCandidateCount; ++pick) {
        int bestCandidateIndex = -1;
        double bestCandidateStrength = -1.0;
        for (int i = 0; i < candidates.size(); ++i) {
            if (usedCandidate[i])
                continue;
            const double strength = candidateStrength(candidates.at(i));
            if (strength > bestCandidateStrength) {
                bestCandidateStrength = strength;
                bestCandidateIndex = i;
            }
        }
        if (bestCandidateIndex < 0)
            break;
        usedCandidate[bestCandidateIndex] = true;
        const int candidate = candidates.at(bestCandidateIndex);
        candidateDebug << QString::number(candidate)
                            + "(strength=" + QString::number(bestCandidateStrength, 'f', 3)
                            + ",support=" + QString::number(supportFor(candidate), 'f', 3) + ")";
    }

    // Low-BPM aliases are common in rock/pop (half tempo, 2:3 relation).
    // Promote 2x or 1.5x candidates when evidence is close.
    if (finalBpm >= 68 && finalBpm <= 95) {
        const double currentSupport = supportFor(finalBpm);
        const int doubledBpm = finalBpm * 2;
        const bool hasDoubled = (doubledBpm >= kMinBpm && doubledBpm <= kMaxBpm);
        QPair<int, double> doubledCandidate = hasDoubled ? strongestNear(doubledBpm, 6) : QPair<int, double>(0, 0.0);
        double doubledSupport = doubledCandidate.second;
        const int tripletBpm = qRound(finalBpm * 1.5);
        const double tripletSupport = (tripletBpm >= kMinBpm && tripletBpm <= kMaxBpm)
            ? supportFor(tripletBpm)
            : 0.0;

        // Hard-bias against half-time lock for fast punk/rock style material.
        if (finalBpm <= 80 && hasDoubled) {
            double requiredRatio = 0.38;
            if (autoCorrConsensus >= kMinBpm && autoCorrConsensus <= kMaxBpm
                && qAbs(doubledCandidate.first - autoCorrConsensus) <= 5) {
                requiredRatio = 0.30;
                doubledSupport *= 1.08;
            }
            if (autoCorrLowBpm >= kMinBpm && autoCorrLowBpm <= kMaxBpm
                && qAbs(doubledCandidate.first - autoCorrLowBpm) <= 5) {
                requiredRatio = qMin(requiredRatio, 0.26);
                doubledSupport *= 1.06;
            }

            // Fast 4/4 disco and pop often land around 76-80 BPM as a half-time alias.
            // Prefer the strongest nearby 2x candidate unless the 3:2 interpretation is clearly stronger.
            if (doubledCandidate.first >= 150)
                requiredRatio = qMin(requiredRatio, 0.30);

            debugNotes << QString("half-time guard: base=%1 support=%2 doubled=%3 support=%4 triplet=%5 support=%6 ratio=%7")
                              .arg(finalBpm)
                              .arg(currentSupport, 0, 'f', 3)
                              .arg(doubledCandidate.first)
                              .arg(doubledSupport, 0, 'f', 3)
                              .arg(tripletBpm)
                              .arg(tripletSupport, 0, 'f', 3)
                              .arg(requiredRatio, 0, 'f', 3);

            if (doubledSupport >= currentSupport * requiredRatio
                && doubledSupport >= tripletSupport * 0.92) {
                finalBpm = doubledCandidate.first;
                debugNotes << QString("half-time guard promoted to %1").arg(finalBpm);
            } else {
                debugNotes << QString("half-time guard kept %1").arg(finalBpm);
            }
        }

        const double refreshedSupport = supportFor(finalBpm);
        int promotedBpm = finalBpm;
        double promotedScore = refreshedSupport;

        const int candidatesToPromote[2] = { finalBpm * 2, qRound(finalBpm * 1.5) };
        for (int i = 0; i < 2; ++i) {
            const int candidate = candidatesToPromote[i];
            if (candidate < kMinBpm || candidate > kMaxBpm)
                continue;

            double score = supportFor(candidate);
            if (autoCorrConsensus >= kMinBpm && autoCorrConsensus <= kMaxBpm
                && qAbs(candidate - autoCorrConsensus) <= 4) {
                score *= 1.12;
            }
            if (autoCorrLowBpm >= kMinBpm && autoCorrLowBpm <= kMaxBpm
                && qAbs(candidate - autoCorrLowBpm) <= 4) {
                score *= 1.08;
            }

            if (score > promotedScore) {
                promotedScore = score;
                promotedBpm = candidate;
            }
        }

        debugNotes << QString("promotion scan: base=%1 support=%2 best=%3 score=%4")
                          .arg(finalBpm)
                          .arg(refreshedSupport, 0, 'f', 3)
                          .arg(promotedBpm)
                          .arg(promotedScore, 0, 'f', 3);

        if (promotedBpm != finalBpm && promotedScore >= refreshedSupport * 0.90) {
            finalBpm = promotedBpm;
            debugNotes << QString("promotion scan switched to %1").arg(finalBpm);
        }
    }

    // Final hard guard against half-time lock-in for tracks that commonly sit
    // around 140-165 BPM but produce a 70-82 BPM alias.
    if (finalBpm >= 70 && finalBpm <= 82) {
        const int doubled = finalBpm * 2;
        if (doubled <= kMaxBpm) {
            const double baseSupport = supportFor(finalBpm);
            QPair<int, double> doubledCandidate = strongestNear(doubled, 6);
            double doubledSupport = doubledCandidate.second;
            const int triplet = qRound(finalBpm * 1.5);
            const double tripletSupport = (triplet >= kMinBpm && triplet <= kMaxBpm)
                ? supportFor(triplet)
                : 0.0;

            if (autoCorrConsensus >= kMinBpm && autoCorrConsensus <= kMaxBpm
                && qAbs(doubledCandidate.first - autoCorrConsensus) <= 5) {
                doubledSupport *= 1.10;
            }
            if (autoCorrLowBpm >= kMinBpm && autoCorrLowBpm <= kMaxBpm
                && qAbs(doubledCandidate.first - autoCorrLowBpm) <= 5) {
                doubledSupport *= 1.08;
            }

            // Prefer 2x unless 1x is clearly stronger.
            debugNotes << QString("final hard guard: base=%1 support=%2 doubled=%3 support=%4 triplet=%5 support=%6")
                              .arg(finalBpm)
                              .arg(baseSupport, 0, 'f', 3)
                              .arg(doubledCandidate.first)
                              .arg(doubledSupport, 0, 'f', 3)
                              .arg(triplet)
                              .arg(tripletSupport, 0, 'f', 3);

            if (doubledSupport >= baseSupport * 0.34
                && doubledSupport >= tripletSupport * 0.92) {
                finalBpm = doubledCandidate.first;
                debugNotes << QString("final hard guard promoted to %1").arg(finalBpm);
            } else {
                debugNotes << QString("final hard guard kept %1").arg(finalBpm);
            }
        }
    }

    p->bpm = qBound(0, finalBpm, kMaxBpm);
    const QString analyserName = parentWidget() ? parentWidget()->objectName() : QString();
    qDebug() << Q_FUNC_INFO << ":" << analyserName << "frames:" << spectralFlux.size()
             << "fullOnsets:" << onsetsFull.size()
             << "lowOnsets:" << onsetsLow.size();
    qDebug() << Q_FUNC_INFO << ":" << analyserName << "smoothed peaks:" << topBinsToString(smoothedScore, 8);
    qDebug() << Q_FUNC_INFO << ":" << analyserName << "lag peaks:" << topBinsToString(lagStrength, 8);
    qDebug() << Q_FUNC_INFO << ":" << analyserName << "autocorr full:" << autoCorrBpm
             << "low:" << autoCorrLowBpm
             << "consensus:" << autoCorrConsensus;
    qDebug() << Q_FUNC_INFO << ":" << analyserName << "candidates:" << candidateDebug.join(", ");
    if (!debugNotes.isEmpty())
        qDebug() << Q_FUNC_INFO << ":" << analyserName << "decision path:" << debugNotes.join(" | ");
    qDebug() << Q_FUNC_INFO << ":" << analyserName << "multi-feature bpm:" << p->bpm;

    // Use a strong early kick/bass transient as phase anchor when available.
    int beatAnchorIdx = -1;
    const QList<float>& anchorEnv = lowEnv.isEmpty() ? fullEnv : lowEnv;
    float strongestAnchor = 0.0f;
    for (int i = 0; i < anchorEnv.size(); ++i)
        strongestAnchor = qMax(strongestAnchor, anchorEnv.at(i));

    if (strongestAnchor > 0.0f) {
        const float earlyThreshold = strongestAnchor * 0.60f;
        const int earlyLimit = qMin(anchorEnv.size(), static_cast<int>(p->fft_res * 20));
        for (int i = 1; i < earlyLimit - 1; ++i) {
            if (anchorEnv.at(i) >= earlyThreshold
                && anchorEnv.at(i) >= anchorEnv.at(i - 1)
                && anchorEnv.at(i) > anchorEnv.at(i + 1)) {
                beatAnchorIdx = i;
                break;
            }
        }
    }

    if (beatAnchorIdx < 0) {
        for (int i = 0; i < anchorEnv.size(); ++i) {
            if (anchorEnv.at(i) > strongestAnchor * 0.95f) {
                beatAnchorIdx = i;
                break;
            }
        }
    }

    if (beatAnchorIdx >= 0) {
        const qint64 offsetMs = static_cast<qint64>((1000.0 * beatAnchorIdx) / p->fft_res);
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
