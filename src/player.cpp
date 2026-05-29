/*
    Copyright (C) 2005-2026 Mario Stephan <mstephan@shared-files.de>

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

#include <QWidget>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace {
constexpr gdouble kMeterMinDb = -80.0;
constexpr bool kLogStateChanges = false;
constexpr bool kLogSeekDebug = true;

const char* gstStateName(GstState state)
{
    return gst_element_state_get_name(state);
}

void logPipelineStateSnapshot(const char* where, const QString& deckName, GstElement* pipeline)
{
    if (!kLogSeekDebug || pipeline == nullptr)
        return;

    GstState state = GST_STATE_NULL;
    GstState pending = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn res = gst_element_get_state(GST_ELEMENT(pipeline), &state, &pending, 0);
    qDebug() << where << ":" << deckName
             << "state=" << gstStateName(state)
             << "pending=" << gstStateName(pending)
             << "get_state_res=" << static_cast<int>(res)
             << "thread=" << QThread::currentThreadId();
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wcpp"
#endif

bool holdsLegacyValueArray(const GValue* value)
{
#ifdef G_TYPE_VALUE_ARRAY
    return value != nullptr && G_VALUE_HOLDS(value, G_TYPE_VALUE_ARRAY);
#else
    Q_UNUSED(value);
    return false;
#endif
}

guint legacyValueArrayCount(const GValue* value)
{
#ifdef G_TYPE_VALUE_ARRAY
    const GValueArray* values = static_cast<const GValueArray*>(g_value_get_boxed(value));
    return values != nullptr ? values->n_values : 0;
#else
    Q_UNUSED(value);
    return 0;
#endif
}

const GValue* legacyValueArrayAt(const GValue* value, guint index)
{
#ifdef G_TYPE_VALUE_ARRAY
    GValueArray* values = static_cast<GValueArray*>(g_value_get_boxed(value));
    return values != nullptr ? g_value_array_get_nth(values, index) : nullptr;
#else
    Q_UNUSED(value);
    Q_UNUSED(index);
    return nullptr;
#endif
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

void configureLevelMessaging(GstElement* levelElement)
{
    if (levelElement == nullptr)
        return;

    GObjectClass* klass = G_OBJECT_GET_CLASS(levelElement);
    if (klass == nullptr)
        return;

    if (g_object_class_find_property(klass, "message") != nullptr)
        g_object_set(levelElement, "message", TRUE, NULL);
    if (g_object_class_find_property(klass, "post-messages") != nullptr)
        g_object_set(levelElement, "post-messages", TRUE, NULL);
    if (g_object_class_find_property(klass, "interval") != nullptr)
        g_object_set(levelElement, "interval", static_cast<gint64>(50 * GST_MSECOND), NULL);
}

guint peakValueCount(const GValue* value)
{
    if (value == nullptr)
        return 0;
    if (GST_VALUE_HOLDS_LIST(value))
        return gst_value_list_get_size(value);
    if (GST_VALUE_HOLDS_ARRAY(value))
        return gst_value_array_get_size(value);
    if (holdsLegacyValueArray(value))
        return legacyValueArrayCount(value);
    return 1;
}

const GValue* peakValueAt(const GValue* value, guint index)
{
    if (value == nullptr)
        return nullptr;
    if (GST_VALUE_HOLDS_LIST(value))
        return gst_value_list_get_value(value, index);
    if (GST_VALUE_HOLDS_ARRAY(value))
        return gst_value_array_get_value(value, index);
    if (holdsLegacyValueArray(value))
        return legacyValueArrayAt(value, index);
    return index == 0 ? value : nullptr;
}

bool gValueToDouble(const GValue* value, gdouble* out)
{
    if (value == nullptr || out == nullptr)
        return false;

    if (G_VALUE_HOLDS_DOUBLE(value)) {
        *out = g_value_get_double(value);
        return true;
    }
    if (G_VALUE_HOLDS_FLOAT(value)) {
        *out = static_cast<gdouble>(g_value_get_float(value));
        return true;
    }
    if (G_VALUE_HOLDS_INT(value)) {
        *out = static_cast<gdouble>(g_value_get_int(value));
        return true;
    }
    if (G_VALUE_HOLDS_UINT(value)) {
        *out = static_cast<gdouble>(g_value_get_uint(value));
        return true;
    }
    if (G_VALUE_HOLDS_INT64(value)) {
        *out = static_cast<gdouble>(g_value_get_int64(value));
        return true;
    }
    if (G_VALUE_HOLDS_UINT64(value)) {
        *out = static_cast<gdouble>(g_value_get_uint64(value));
        return true;
    }

    if (GST_VALUE_HOLDS_LIST(value) || GST_VALUE_HOLDS_ARRAY(value) || holdsLegacyValueArray(value)) {
        const GValue* nested = peakValueAt(value, 0);
        return nested != nullptr && nested != value ? gValueToDouble(nested, out) : false;
    }

    return false;
}

const GValue* levelDbValues(const GstStructure* s, const char** fieldName)
{
    if (fieldName != nullptr)
        *fieldName = "rms";

    const GValue* values = gst_structure_get_value(s, "rms");
    if (values != nullptr)
        return values;

    if (fieldName != nullptr)
        *fieldName = "peak";
    return gst_structure_get_value(s, "peak");
}

void configureAudioSink(GstElement* sink)
{
    if (sink == nullptr)
        return;

    GObjectClass* klass = G_OBJECT_GET_CLASS(sink);
    if (klass == nullptr)
        return;

    // Starting without async preroll avoids startup stalls on some Linux setups.
    // Keep platform default behavior elsewhere (notably macOS dual-sink pipelines).
#if defined(Q_OS_LINUX)
    if (g_object_class_find_property(klass, "async") != nullptr)
        g_object_set(sink, "async", FALSE, NULL);
#else
    Q_UNUSED(klass);
#endif
}

bool hasWritableProperty(GstElement* element, const char* propertyName)
{
    if (element == nullptr || propertyName == nullptr)
        return false;

    GObjectClass* klass = G_OBJECT_GET_CLASS(element);
    if (klass == nullptr)
        return false;

    GParamSpec* spec = g_object_class_find_property(klass, propertyName);
    return spec != nullptr && (spec->flags & G_PARAM_WRITABLE) != 0;
}

GstElement* getTempoEffect(GstElement* pipeline)
{
    if (pipeline == nullptr)
        return nullptr;
    return gst_bin_get_by_name(GST_BIN(pipeline), "tempoeffect");
}

bool pipelineSupportsSmoothTempo(GstElement* pipeline)
{
    GstElement* tempoEffect = getTempoEffect(pipeline);
    if (tempoEffect == nullptr)
        return false;

    const bool supported = hasWritableProperty(tempoEffect, "tempo");
    gst_object_unref(tempoEffect);
    return supported;
}

void setTempoEffectRate(GstElement* pipeline, double rate)
{
    GstElement* tempoEffect = getTempoEffect(pipeline);
    if (tempoEffect == nullptr)
        return;

    if (hasWritableProperty(tempoEffect, "tempo"))
        g_object_set(G_OBJECT(tempoEffect), "tempo", rate, NULL);
    if (hasWritableProperty(tempoEffect, "pitch"))
        g_object_set(G_OBJECT(tempoEffect), "pitch", 1.0, NULL);
    if (hasWritableProperty(tempoEffect, "rate"))
        g_object_set(G_OBJECT(tempoEffect), "rate", 1.0, NULL);

    gst_object_unref(tempoEffect);
}

QString defaultAudioDeviceId()
{
#if defined(Q_OS_WIN32)
    return QString();
#elif defined(Q_OS_DARWIN)
    return QString("0");
#else
    return QString("default");
#endif
}

void applyAudioSinkDevice(GstElement* sink, const QString& deviceId)
{
    if (sink == nullptr)
        return;

    GObjectClass* sinkClass = G_OBJECT_GET_CLASS(sink);
    if (sinkClass == nullptr || g_object_class_find_property(sinkClass, "device") == nullptr)
        return;

#if defined(Q_OS_WIN32)
    const QByteArray encoded = deviceId.toLatin1();
    g_object_set(sink, "device", encoded.isEmpty() ? nullptr : encoded.constData(), NULL);
#elif defined(Q_OS_DARWIN)
    bool ok = false;
    int target = deviceId.toInt(&ok);
    if (!ok)
        target = 0;
    g_object_set(sink, "device", target, NULL);
#else
    QByteArray encoded;
    if (deviceId.isEmpty() || deviceId == QLatin1String("0") || deviceId == QLatin1String("default"))
        encoded = QByteArray("default");
    else
        encoded = QString("hw:%1").arg(deviceId).toLatin1();
    g_object_set(sink, "device", encoded.constData(), NULL);
#endif
}
}

void Player::sync_set_state(GstElement* element, GstState state)
{
    GstStateChangeReturn res;
    res = gst_element_set_state(GST_ELEMENT(element), state);
    if (res == GST_STATE_CHANGE_FAILURE)
        return;
    if (res == GST_STATE_CHANGE_ASYNC) {
        GstState state;
        res = gst_element_get_state(GST_ELEMENT(element), &state, NULL, GST_CLOCK_TIME_NONE);
        if (res == GST_STATE_CHANGE_FAILURE || res == GST_STATE_CHANGE_ASYNC)
            return;
    }
}

void cb_newpad(GstElement* src,
    GstPad* new_pad,
    gpointer data)
{
    Player* instance = (Player*)data;
    instance->newpad(src, new_pad, data);
}

void Player::newpad(GstElement* src,
    GstPad* new_pad,
    gpointer data)
{
    Q_UNUSED(src);
    Q_UNUSED(data);

    GstCaps* caps;
    GstStructure* str;
    GstPad* sink_pad;

    /* only link once */
    GstElement* bin = gst_bin_get_by_name(GST_BIN(pipeline), "convert");
    sink_pad = gst_element_get_static_pad(bin, "sink");
    gst_object_unref(bin);

    if (GST_PAD_IS_LINKED(sink_pad)) {
        g_object_unref(sink_pad);
        return;
    }

    /* check media type */
    caps = gst_pad_query_caps(new_pad, nullptr);
    str = gst_caps_get_structure(caps, 0);
    if (!g_strrstr(gst_structure_get_name(str), "audio")) {
        gst_caps_unref(caps);
        gst_object_unref(sink_pad);
        return;
    }
    gst_caps_unref(caps);

    /* link'n'play */
    gst_pad_link(new_pad, sink_pad);

    qDebug() << Q_FUNC_INFO << " "
             << "END";
}

struct PlayerPrivate {
    QFutureWatcher<void> watcher;
    QMutex mutex;
    bool isStarted;
    bool isLoaded;
    QString error;
    int length;
    int position;
    int playBasePosition;
    QElapsedTimer playTimer;
    double volume;
    double rate;
    double rms_l;
    double rms_r;
    double rmsout_l;
    double rmsout_r;
    QString monitorDeviceId;
    QString masterDeviceId;
    bool useMonitorOutput;
    double monitorVolume;  // desired monitor branch volume (0 = muted)
    int lastQueriedPosition;
    int stagnantQueryCount;
    QElapsedTimer rollRecoveryCooldown;
};

Player::Player(QWidget* parent)
    : QWidget(parent)
    , p(new PlayerPrivate)
    , pipeline(nullptr)
    , bus(nullptr)
    , Gstart(0)
    , Glength(0)
{
    p->isStarted = false;
    p->isLoaded = false;
    p->rate = 1.0;
    p->rms_l = 0;
    p->rms_r = 0;
    p->rmsout_l = 0;
    p->rmsout_r = 0;
    p->playBasePosition = 0;
    p->monitorDeviceId = QString();
    p->masterDeviceId = defaultAudioDeviceId();
    p->useMonitorOutput = false;
    p->monitorVolume = 1.0;
    p->lastQueriedPosition = -1;
    p->stagnantQueryCount = 0;

    connect(&p->watcher, SIGNAL(finished()), this, SLOT(loadThreadFinished()));
}

Player::~Player()
{
    if (p->watcher.isRunning())
        p->watcher.waitForFinished();

    cleanup();
    delete p;
    p = nullptr;
}

GstBusSyncReply Player::bus_cb(GstBus* bus, GstMessage* msg, gpointer data)
{
    Q_UNUSED(bus);
    Player* instance = (Player*)data;
    instance->messageReceived(msg);
    return GST_BUS_PASS;
}

void Player::cleanup()
{
    if (pipeline)
        sync_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
    if (bus)
        gst_object_unref(bus);
    if (pipeline)
        gst_object_unref(G_OBJECT(pipeline));
}

bool Player::prepare()
{
    // Init Gst
    qDebug() << Q_FUNC_INFO << " "
             << "START";
    QString caps_value;

    // On mac we bundle the gstreamer plugins with knowthelist
#if defined(Q_OS_DARWIN)
    QString scanner_path;
    QString plugin_path;
    QString registry_filename;

    QDir pd(QCoreApplication::applicationDirPath() + "/../plugins");
    scanner_path = QCoreApplication::applicationDirPath() + "/../plugins/gst-plugin-scanner";
    plugin_path = QCoreApplication::applicationDirPath() + "/../plugins/gstreamer";
    registry_filename = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QString("/gst-registry-%1-bin").arg(QCoreApplication::applicationVersion());

    if (pd.exists())
        setenv("GST_PLUGIN_SCANNER", scanner_path.toLocal8Bit().constData(), 1);

    if (pd.exists()) {
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
    if (pluginDir.exists())
        _putenv_s("GST_PLUGIN_PATH", plugin_path.toLocal8Bit());

#endif

    //_putenv_s("GST_DEBUG", "*:4"); // win
    //setenv("GST_DEBUG", "*:4", 1); // unix, mac

    gst_init(nullptr, nullptr);

    //prepare
    GstElement *src, *conv, *resample, *sink, *gain, *vol, *level, *equalizer;
    GstElement* levelout;
    GstCaps* caps;
    pipeline = gst_pipeline_new("pipeline");
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

    caps_value = "audio/x-raw";
    caps = gst_caps_new_simple(caps_value.toLatin1().data(),
        "channels", G_TYPE_INT, 2, NULL);

    src = gst_element_factory_make("uridecodebin", "source");
    g_signal_connect(src, "pad-added", G_CALLBACK(cb_newpad), this);

    conv = gst_element_factory_make("audioconvert", "convert");
    resample = gst_element_factory_make("audioresample", "resample");
    gain = gst_element_factory_make("audioamplify", "gain");
    level = gst_element_factory_make("level", "levelintern");
    vol = gst_element_factory_make("volume", "volume");
    levelout = gst_element_factory_make("level", "levelout");
    equalizer = gst_element_factory_make("equalizer-3bands", "equalizer");
#if defined(Q_OS_DARWIN)
    sink = gst_element_factory_make("osxaudiosink", "sink");
    if (sink != nullptr)
        applyAudioSinkDevice(sink, p->masterDeviceId);
#elif defined(Q_OS_WIN32)
    sink = gst_element_factory_make("autoaudiosink", "sink");
#else
    sink = gst_element_factory_make("alsasink", "sink");
    if (sink == nullptr)
        sink = gst_element_factory_make("pulsesink", "sink");
    if (sink == nullptr)
        sink = gst_element_factory_make("autoaudiosink", "sink");

    if (sink != nullptr)
        applyAudioSinkDevice(sink, p->masterDeviceId);
#endif

    configureAudioSink(sink);
    if (kLogStateChanges)
        qDebug() << Q_FUNC_INFO << "using audio sink" << (sink != nullptr ? GST_OBJECT_NAME(sink) : "<none>");

    configureLevelMessaging(level);
    configureLevelMessaging(levelout);
    g_object_set(level, "peak-ttl", 300000000000, NULL);

    // PFL (pre-fader listen) monitor branch: tee after equalizer so the
    // crossfader/volume control does not affect what the DJ hears on headphones.
    // Note: no valve element — data always flows so seeks never block waiting
    // for a downstream sink that never receives a post-seek buffer.
    // vol_mon starts at 0 (muted) and is raised to the desired level when MON is pressed.
    GstElement* teeElem = gst_element_factory_make("tee",    "tee");
    GstElement* qMain   = gst_element_factory_make("queue",  "q_main");
    GstElement* qMon    = gst_element_factory_make("queue",  "q_mon");
    GstElement* volMon  = gst_element_factory_make("volume", "vol_mon");

    // Keep queues small so seek flushes take effect immediately (no 1-second
    // audio lag after a FLUSH seek).  The monitor queue must be leaky=downstream
    // so that if sink_mon ever runs behind the tee's push thread, it drops the
    // oldest buffered packet instead of blocking the tee — and thereby blocking
    // the entire pipeline (including gst_element_seek() calls from the UI thread).
    if (qMain != nullptr)
        g_object_set(G_OBJECT(qMain),
            "max-size-time",    (guint64)0,
            "max-size-bytes",   (guint)0,
            "max-size-buffers", (guint)3,
            nullptr);
    if (qMon != nullptr)
        g_object_set(G_OBJECT(qMon),
            "max-size-time",    (guint64)0,
            "max-size-bytes",   (guint)0,
            "max-size-buffers", (guint)3,
            "leaky",            (gint)2,   // GST_QUEUE_LEAK_DOWNSTREAM
            nullptr);
#if defined(Q_OS_DARWIN)
    GstElement* sinkMon = gst_element_factory_make("osxaudiosink", "sink_mon");
    if (sinkMon != nullptr)
        g_object_set(sinkMon, "device", 0, NULL);   // placeholder; overwritten by setMonitorDeviceId()
#elif defined(Q_OS_WIN32)
    GstElement* sinkMon = gst_element_factory_make("autoaudiosink", "sink_mon");
#else
    GstElement* sinkMon = gst_element_factory_make("alsasink",      "sink_mon");
    if (sinkMon == nullptr) sinkMon = gst_element_factory_make("pulsesink",     "sink_mon");
    if (sinkMon == nullptr) sinkMon = gst_element_factory_make("autoaudiosink", "sink_mon");
#endif
    if (volMon  != nullptr) g_object_set(G_OBJECT(volMon), "volume", 0.0, NULL);  // starts muted
    configureAudioSink(sinkMon);

    // The monitor sink runs on a separate audio device with its own independent
    // hardware clock.  If sync=true (the GStreamer default), GStreamer tries to
    // align monitor output to the pipeline clock that is driven by the main sink.
    // Because the two CoreAudio devices run at slightly different rates they drift
    // apart, which produces:
    //   • audible delay between main and monitor outputs
    //   • periodic dropouts as GStreamer catches up after each drift cycle
    //   • broken main output after a seek, because sinkMon waits for the
    //     post-flush pipeline-clock timestamp before emitting any audio and this
    //     stalls the downstream half of the tee
    // Setting sync=false makes the monitor branch play audio immediately at the
    // hardware device's native rate, completely independent of the pipeline clock.
    // provide-clock=false prevents sinkMon from competing with the main sink as
    // the pipeline's clock source.  qos=false suppresses Quality-of-Service
    // upstream notifications that would otherwise cause the decoder to drop frames.
    if (sinkMon != nullptr) {
        GObjectClass* monKlass = G_OBJECT_GET_CLASS(sinkMon);
        if (monKlass != nullptr) {
            if (g_object_class_find_property(monKlass, "sync") != nullptr)
                g_object_set(G_OBJECT(sinkMon), "sync", FALSE, NULL);
            if (g_object_class_find_property(monKlass, "qos") != nullptr)
                g_object_set(G_OBJECT(sinkMon), "qos", FALSE, NULL);
            if (g_object_class_find_property(monKlass, "provide-clock") != nullptr)
                g_object_set(G_OBJECT(sinkMon), "provide-clock", FALSE, NULL);
        }
    }

    // Prefer a writable tempo effect so runtime tempo changes do not require seeks.
    GstElement* tempoEffect = gst_element_factory_make("pitch", "tempoeffect");

    if (tempoEffect != nullptr && hasWritableProperty(tempoEffect, "tempo")) {
        g_object_set(G_OBJECT(tempoEffect), "tempo", 1.0, NULL);
        if (hasWritableProperty(tempoEffect, "pitch"))
            g_object_set(G_OBJECT(tempoEffect), "pitch", 1.0, NULL);
        if (hasWritableProperty(tempoEffect, "rate"))
            g_object_set(G_OBJECT(tempoEffect), "rate", 1.0, NULL);

        gst_bin_add_many(GST_BIN(pipeline), src, conv, resample, tempoEffect, level, gain, equalizer, levelout, vol, sink, NULL);
        gst_element_link(conv, resample);
        gst_element_link(resample, tempoEffect);
        gst_element_link_filtered(tempoEffect, level, caps);
    } else {
        if (tempoEffect != nullptr)
            gst_object_unref(tempoEffect);

        tempoEffect = gst_element_factory_make("scaletempo", "tempoeffect");
        if (tempoEffect != nullptr) {
            gst_bin_add_many(GST_BIN(pipeline), src, conv, resample, tempoEffect, level, gain, equalizer, levelout, vol, sink, NULL);
            gst_element_link(conv, resample);
            gst_element_link(resample, tempoEffect);
            gst_element_link_filtered(tempoEffect, level, caps);
        } else {
            qDebug() << Q_FUNC_INFO << "no smooth tempo element available – tempo changes will use segment seeks";
            gst_bin_add_many(GST_BIN(pipeline), src, conv, resample, level, gain, equalizer, levelout, vol, sink, NULL);
            gst_element_link(conv, resample);
            gst_element_link_filtered(resample, level, caps);
        }
    }

    // Add monitor branch elements (common to all tempo variants)
    gst_bin_add_many(GST_BIN(pipeline), teeElem, qMain, qMon, volMon, sinkMon, NULL);

    gst_element_link(level, gain);
    gst_element_link(gain, equalizer);
    // Tap after equalizer so the crossfader/volume does not affect monitor (PFL)
    gst_element_link(equalizer, teeElem);
    // Main branch: tee → q_main → vol → levelout → sink
    gst_element_link(teeElem, qMain);
    gst_element_link(qMain, vol);
    gst_element_link_filtered(vol, levelout, caps);
    gst_element_link(levelout, sink);
    // Monitor branch: tee → q_mon → vol_mon → sink_mon
    // vol_mon=0 means muted (MON off); raised to monitorVolume when MON is pressed.
    gst_element_link(teeElem, qMon);
    gst_element_link(qMon, volMon);
    gst_element_link(volMon, sinkMon);

    gst_bus_set_sync_handler(bus, bus_cb, this, nullptr);

    qDebug() << Q_FUNC_INFO << " "
             << "END";

    return pipeline;
}

bool Player::ready()
{
    return pipeline;
}

void Player::setGain(double g)
{
    gdouble gain_value = 1.00 * g;

    GstElement* gain = gst_bin_get_by_name(GST_BIN(pipeline), "gain");
    g_object_set(G_OBJECT(gain), "amplification", gain_value, NULL);
    gst_object_unref(gain);
}

void Player::setEqualizer(QString band, double gain)
{
    gdouble gain_value = 1.00 * gain;

    GstElement* equalizer = gst_bin_get_by_name(GST_BIN(pipeline), "equalizer");
    g_object_set(G_OBJECT(equalizer), band.toLatin1().data(), gain_value, NULL);
    gst_object_unref(equalizer);
}

void Player::open(QUrl url)
{
    //To avoid delays load track in another thread
    qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName() << " url=" << url;
    QFuture<void> future = QtConcurrent::run([this, url]() { asyncOpen(url); });
    p->watcher.setFuture(future);
}

void Player::asyncOpen(QUrl url)
{
    QMutexLocker locker(&p->mutex);
    p->length = 0;
    p->position = 0;
    p->rate = 1.0;
    p->isLoaded = false;
    p->error = "";
    lastError = "";

    setTempoEffectRate(pipeline, 1.0);

    sync_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);

    // Re-apply monitor device — osxaudiosink may reset it after NULL transition
    if (!p->monitorDeviceId.isEmpty()) {
        GstElement* sinkMon = gst_bin_get_by_name(GST_BIN(pipeline), "sink_mon");
        if (sinkMon != nullptr) {
            applyAudioSinkDevice(sinkMon, p->monitorDeviceId);
            gst_object_unref(sinkMon);
        }
    }

    GstElement* src = gst_bin_get_by_name(GST_BIN(pipeline), "source");
    g_object_set(G_OBJECT(src), "uri", (const char*)url.toString().toUtf8(), NULL);

    qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName();

    sync_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
    setPosition(QTime(0, 0));

    gst_object_unref(src);
}

void Player::loadThreadFinished()
{
    // async load in player done
    qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName();

    p->isLoaded = true;
    emit loadFinished();

    if (p->isStarted) {
        play();
    }
}

void Player::play()
{
    p->isStarted = true;
    qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName() << "isLoaded=" << p->isLoaded;
    logPipelineStateSnapshot("Player::play(before set_state)", parentWidget()->objectName(), pipeline);
    if (p->isLoaded) {
        qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName() << " call GST_STATE_PLAYING";
        const GstStateChangeReturn setRes = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
        qDebug() << "Player::play:" << parentWidget()->objectName()
                 << "set_state PLAYING res=" << static_cast<int>(setRes)
                 << "thread=" << QThread::currentThreadId();
        p->playBasePosition = p->position;
        p->playTimer.restart();
        p->lastQueriedPosition = -1;
        p->stagnantQueryCount = 0;
        logPipelineStateSnapshot("Player::play(after set_state)", parentWidget()->objectName(), pipeline);
    } else {
        qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName() << " is not loaded, deferring play until load completes";
    }
}
void Player::stop()
{
    p->isStarted = false;
    p->rate = 1.0;
    p->playBasePosition = 0;
    p->playTimer.invalidate();
    p->lastQueriedPosition = -1;
    p->stagnantQueryCount = 0;
    setTempoEffectRate(pipeline, 1.0);
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_READY);
}

void Player::pause()
{
    if (isPlaying()) {
        p->isStarted = false;
        const QTime now = position();
        p->position = QTime(0, 0).msecsTo(now);
        p->playBasePosition = p->position;
        p->playTimer.invalidate();
        p->lastQueriedPosition = -1;
        p->stagnantQueryCount = 0;
        gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
    }
}

bool Player::close()
{
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
    return true;
}

void Player::setPosition(QTime position)
{
    if (pipeline == nullptr)
        return;

    const int time_milliseconds = QTime(0, 0).msecsTo(position);
    const gint64 time_nanoseconds = (static_cast<gint64>(time_milliseconds) * GST_MSECOND);
    const gdouble seekRate = pipelineSupportsSmoothTempo(pipeline) ? 1.0 : p->rate;

    if (kLogSeekDebug) {
        qDebug() << "Player::setPosition(req):" << parentWidget()->objectName()
                 << "targetMs=" << time_milliseconds
                 << "seekRate=" << seekRate
                 << "isStarted=" << p->isStarted
                 << "isLoaded=" << p->isLoaded
                 << "thread=" << QThread::currentThreadId();
    }
    logPipelineStateSnapshot("Player::setPosition(before seek)", parentWidget()->objectName(), pipeline);
    GstSeekFlags flags = static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE);

    const gboolean seekOk = gst_element_seek(pipeline, seekRate, GST_FORMAT_TIME, flags,
        GST_SEEK_TYPE_SET, time_nanoseconds,
        GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
    if (kLogSeekDebug) {
        qDebug() << "Player::setPosition(seek result):" << parentWidget()->objectName()
                 << "targetMs=" << time_milliseconds
                 << "seekOk=" << static_cast<bool>(seekOk);
    }

    // Immediately anchor both the position cache and the timer-based fallback to
    // the seek target.  gst_element_query_position() returns false for a short
    // window after a FLUSH seek (pipeline is transitioning).  Without this,
    // position() falls back to  playBasePosition + elapsed  which still points
    // to wherever the pipeline was BEFORE the seek, producing a completely wrong
    // value that corrupts the scrub anchor on the very next call.
    p->position         = time_milliseconds;
    p->playBasePosition = time_milliseconds;
    if (p->playTimer.isValid())
        p->playTimer.restart();

    if (kLogSeekDebug && pipeline) {
        gint64 queried = 0;
        const gboolean queriedOk = gst_element_query_position(pipeline, GST_FORMAT_TIME, &queried);
        qDebug() << "Player::setPosition(post query):" << parentWidget()->objectName()
                 << "queriedOk=" << static_cast<bool>(queriedOk)
                 << "queriedMs=" << static_cast<int>(queried / GST_MSECOND);
    }
    logPipelineStateSnapshot("Player::setPosition(after seek)", parentWidget()->objectName(), pipeline);

    emit positionChanged();
}

void Player::setRate(double rate)
{
    if (pipeline == nullptr)
        return;

    if (rate < 0.50)
        rate = 0.50;
    else if (rate > 2.00)
        rate = 2.00;

    if (qFuzzyCompare(rate, p->rate))
        return;

    const double previousRate = p->rate;
    p->rate = rate;

    if (!p->isLoaded)
        return;

    if (pipelineSupportsSmoothTempo(pipeline)) {
        setTempoEffectRate(pipeline, rate);
        if (p->isStarted)
            p->playTimer.restart();
        return;
    }

    gint64 pos = 0;
    if (!gst_element_query_position(pipeline, GST_FORMAT_TIME, &pos))
        pos = static_cast<gint64>(p->position) * GST_MSECOND;

    p->position = static_cast<int>(pos / GST_MSECOND);
    p->playBasePosition = p->position;
    if (p->isStarted)
        p->playTimer.restart();

    GstSeekFlags flags = GST_SEEK_FLAG_ACCURATE;
    if (!p->isStarted)
        flags = static_cast<GstSeekFlags>(flags | GST_SEEK_FLAG_FLUSH);

    gst_element_seek(pipeline, rate, GST_FORMAT_TIME,
        flags,
        GST_SEEK_TYPE_SET, pos,
        GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
}

double Player::rate() const
{
    return p->rate;
}

bool Player::isLoaded() const
{
    return p->isLoaded;
}

bool Player::supportsSmoothTempo() const
{
    return pipelineSupportsSmoothTempo(pipeline);
}

void Player::setMonitorDeviceId(const QString& deviceId)
{
    p->monitorDeviceId = deviceId.trimmed();
    if (pipeline == nullptr)
        return;

    GstElement* sinkMon = gst_bin_get_by_name(GST_BIN(pipeline), "sink_mon");
    if (sinkMon == nullptr)
        return;

    GstState cur_state = GST_STATE_NULL;
    gst_element_get_state(GST_ELEMENT(pipeline), &cur_state, nullptr, 0);

    if (cur_state == GST_STATE_NULL) {
        applyAudioSinkDevice(sinkMon, p->monitorDeviceId);
    } else {
        // Mute the monitor branch so there are no audio artefacts while we
        // cycle sink_mon through NULL to apply the new device.
        GstElement* volMonElem = gst_bin_get_by_name(GST_BIN(pipeline), "vol_mon");
        if (volMonElem != nullptr)
            g_object_set(G_OBJECT(volMonElem), "volume", 0.0, NULL);

        gst_element_set_locked_state(sinkMon, TRUE);
        gst_element_set_state(sinkMon, GST_STATE_NULL);
        gst_element_get_state(sinkMon, nullptr, nullptr, 1 * GST_SECOND);
        applyAudioSinkDevice(sinkMon, p->monitorDeviceId);
        gst_element_set_state(sinkMon, cur_state);
        gst_element_get_state(sinkMon, nullptr, nullptr, 1 * GST_SECOND);
        gst_element_set_locked_state(sinkMon, FALSE);

        // Restore volume
        if (volMonElem != nullptr) {
            applyOutputRouting();
            gst_object_unref(volMonElem);
        }
    }
    gst_object_unref(sinkMon);
}

void Player::setUseMonitorOutput(bool enabled)
{
    p->useMonitorOutput = enabled;
    applyOutputRouting();
}

bool Player::useMonitorOutput() const
{
    return p->useMonitorOutput;
}

void Player::applyOutputRouting()
{
    if (pipeline == nullptr)
        return;

    GstElement* volMon = gst_bin_get_by_name(GST_BIN(pipeline), "vol_mon");
    if (volMon == nullptr)
        return;

    // Mute or unmute the monitor branch — no pipeline state change needed.
    // Using volume=0 rather than a valve element avoids seek hangs that occur
    // when a downstream sink never receives the post-flush buffer.
    const gdouble v = (p->useMonitorOutput && !p->monitorDeviceId.isEmpty())
        ? p->monitorVolume
        : 0.0;
    g_object_set(G_OBJECT(volMon), "volume", v, NULL);
    gst_object_unref(volMon);
}

void Player::setMonitorVolume(double v)
{
    p->monitorVolume = v;
    applyOutputRouting();  // re-applies v if monitor is currently active
}

QTime Player::position()
{
    if (pipeline) {

        gint64 value = 0;

        if (gst_element_query_position(pipeline, GST_FORMAT_TIME, &value)) {
            const int queriedMs = static_cast<int>(value / GST_MSECOND);
            p->position = queriedMs;
            p->playBasePosition = p->position;
            if (p->isStarted && !p->playTimer.isValid())
                p->playTimer.start();

            if (p->isStarted && isPlaying()) {
                if (queriedMs == p->lastQueriedPosition)
                    ++p->stagnantQueryCount;
                else
                    p->stagnantQueryCount = 0;

                const bool shouldRecoverRoll = (queriedMs == 0
                                                && p->stagnantQueryCount >= 8
                                                && p->playTimer.isValid()
                                                && p->playTimer.elapsed() > 500
                                                && (!p->rollRecoveryCooldown.isValid()
                                                    || p->rollRecoveryCooldown.elapsed() > 1500));
                if (shouldRecoverRoll) {
                    p->rollRecoveryCooldown.restart();
                    qDebug() << "Player::position() ROLL RECOVERY:" << parentWidget()->objectName()
                             << "stagnantQueryCount=" << p->stagnantQueryCount
                             << "elapsedSincePlayMs=" << p->playTimer.elapsed();

                    const gboolean recoverOk = gst_element_seek(
                        pipeline,
                        1.0,
                        GST_FORMAT_TIME,
                        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                        GST_SEEK_TYPE_SET,
                        0,
                        GST_SEEK_TYPE_NONE,
                        GST_CLOCK_TIME_NONE);

                    qDebug() << "Player::position() ROLL RECOVERY RESULT:" << parentWidget()->objectName()
                             << "recoverOk=" << static_cast<bool>(recoverOk);
                }
            }
            p->lastQueriedPosition = queriedMs;

            qDebug() << "Player::position() QUERY SUCCESS:" << parentWidget()->objectName()
                     << "queriedMs=" << p->position
                     << "stagnantCount=" << p->stagnantQueryCount;
            return QTime(0, 0).addMSecs(p->position); // nanosec -> msec
        }

        if (p->isStarted && p->playTimer.isValid()) {
            p->position = p->playBasePosition + static_cast<int>(p->playTimer.elapsed() * p->rate);
            qDebug() << "Player::position() FALLBACK (timer):" << parentWidget()->objectName()
                     << "playBasePosition=" << p->playBasePosition
                     << "elapsed=" << p->playTimer.elapsed()
                     << "rate=" << p->rate
                     << "resultMs=" << p->position;
            return QTime(0, 0).addMSecs(p->position);
        }

        qDebug() << "Player::position() FALLBACK (cached):" << parentWidget()->objectName()
                 << "cachedMs=" << p->position;
        return QTime(0, 0).addMSecs(p->position); // nanosec -> msec
    }
    return QTime(0, 0);
}

QTime Player::length()
{
    gint64 value = 0;

    if (p->length == 0 && pipeline) {

        if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &value)) {
            p->length = static_cast<int>((value / GST_MSECOND));
        } else
            qDebug() << Q_FUNC_INFO << ": Can not get duration";
    }
    return QTime(0, 0).addMSecs(p->length); // nanosec -> msec
}

double Player::volume()
{
    gdouble vol = 0;

    GstElement* volume = gst_bin_get_by_name(GST_BIN(pipeline), "volume");
    g_object_get(G_OBJECT(volume), "volume", &vol, nullptr);
    gst_object_unref(volume);

    return static_cast<double>(vol);
}

void Player::setVolume(double v)
{
    gdouble vol = static_cast<gdouble>(v);
    if (vol < 0.001) {
        vol = 0.001;
    }

    GstElement* volume = gst_bin_get_by_name(GST_BIN(pipeline), "volume");
    g_object_set(G_OBJECT(volume), "volume", vol, nullptr);
    gst_object_unref(volume);
}

bool Player::mediaPlayable()
{
    GstState st;
    gst_element_get_state(GST_ELEMENT(pipeline), &st, nullptr, 0);
    //qDebug()<<gst_element_state_get_name(st);
    return (st != GST_STATE_NULL);
}

bool Player::isPlaying()
{
    GstState st;
    gst_element_get_state(GST_ELEMENT(pipeline), &st, nullptr, 0);
    return (st == GST_STATE_PLAYING);
}

double Player::levelLeft() { return p->rms_l; }
double Player::levelRight() { return p->rms_r; }
double Player::levelOutLeft() { return p->rmsout_l; }
double Player::levelOutRight() { return p->rmsout_r; }

void Player::messageReceived(GstMessage* message)
{

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        if (p->error == "") {
            GError* err;
            gchar* debug;
            gst_message_parse_error(message, &err, &debug);
            p->error = "Error #" + QString::number(err->code) + " in module " + QString::number(err->domain) + "\n" + QString::fromUtf8(err->message);
            if (err->domain != GST_STREAM_ERROR && err->code != GST_STREAM_ERROR_FAILED) {
                p->error += "\nMay be you should install more of gstreamer plugins";
                lastError = QString::fromUtf8(err->message);
            }
            qDebug() << Q_FUNC_INFO << ": Gstreamer error:" << p->error;
            g_error_free(err);
            g_free(debug);
            Q_EMIT error();
        }
        break;
    }
    case GST_MESSAGE_EOS: {
        qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName() << " End of track reached";
        Q_EMIT finish();
        break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
        GstState old_state, new_state;
        gst_message_parse_state_changed(message, &old_state, &new_state, nullptr);
        const GstObject* source = GST_MESSAGE_SRC(message);
        if (kLogStateChanges) {
            qDebug() << Q_FUNC_INFO << "state-changed from" << GST_OBJECT_NAME(source)
                     << gstStateName(old_state) << "->" << gstStateName(new_state);
        }
        if (source == GST_OBJECT(pipeline)) {
            if (kLogSeekDebug) {
                qDebug() << "Player::messageReceived(STATE_CHANGED):" << parentWidget()->objectName()
                         << gstStateName(old_state) << "->" << gstStateName(new_state)
                         << "thread=" << QThread::currentThreadId();
            }
            switch (new_state) {
            case GST_STATE_PAUSED:
            case GST_STATE_NULL:
                p->rms_l = p->rms_r = 0;
                p->rmsout_l = p->rmsout_r = 0;
            default:
                break;
            }
        }
        break;
    }

    case GST_MESSAGE_ASYNC_DONE: {
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline)) {
            if (kLogSeekDebug)
                qDebug() << "Player::messageReceived(ASYNC_DONE):" << parentWidget()->objectName();
            logPipelineStateSnapshot("Player::messageReceived(ASYNC_DONE)", parentWidget()->objectName(), pipeline);
        }
        break;
    }

    case GST_MESSAGE_ELEMENT: {
        const GstStructure* s = gst_message_get_structure(message);
        const gchar* src_name = GST_MESSAGE_SRC_NAME(message);
        const gchar* structure_name = s != nullptr ? gst_structure_get_name(s) : "<no-structure>";

        if (kLogStateChanges) {
            qDebug() << Q_FUNC_INFO << "element message from" << src_name
                     << "structure" << structure_name;
        }

        if (strcmp(src_name, "levelintern") == 0) {
            const GValue* peakValues = levelDbValues(s, nullptr);
            const guint channels = peakValueCount(peakValues);

            for (guint i = 0; i < channels; ++i) {
                const GValue* peakValue = peakValueAt(peakValues, i);
                gdouble peak_dB = 0.0;
                if (!gValueToDouble(peakValue, &peak_dB)) {
                    if (kLogStateChanges) {
                        qDebug() << Q_FUNC_INFO << "levelintern channel" << i
                                 << "unsupported value type"
                                 << (peakValue != nullptr ? G_VALUE_TYPE_NAME(peakValue) : "<null>");
                    }
                    continue;
                }

                /* Keep the raw deck meter calmer: compress mid-level energy so red reflects real peaks. */
                const gdouble linear = qBound(0.0, (peak_dB - kMeterMinDb) / (-kMeterMinDb), 1.0);
                const gdouble level = pow(linear, 1.6);
                if (i == 0)
                    p->rms_l = level;
                else
                    p->rms_r = level;
            }
        }
        if (strcmp(src_name, "levelout") == 0) {
            const GValue* peakValues = levelDbValues(s, nullptr);
            const guint channels = peakValueCount(peakValues);

            for (guint i = 0; i < channels; ++i) {
                const GValue* peakValue = peakValueAt(peakValues, i);
                gdouble peak_dB = 0.0;
                if (!gValueToDouble(peakValue, &peak_dB)) {
                    if (kLogStateChanges) {
                        qDebug() << Q_FUNC_INFO << "levelout channel" << i
                                 << "unsupported value type"
                                 << (peakValue != nullptr ? G_VALUE_TYPE_NAME(peakValue) : "<null>");
                    }
                    continue;
                }

                /* Map roughly [-80..0] dBFS to [0..1] so very quiet output still shows. */
                const gdouble level = qBound(0.0, (peak_dB - kMeterMinDb) / (-kMeterMinDb), 1.0);
                if (i == 0)
                    p->rmsout_l = level;
                else
                    p->rmsout_r = level;
            }
        }
    } break;
    default:
        break;
    }
}
