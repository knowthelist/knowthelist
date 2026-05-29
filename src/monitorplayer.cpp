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

#include <QWidget>
#include <QMutexLocker>
#include <QtConcurrent/QtConcurrent>
#include <cstring>
#include <cmath>

#if defined(Q_OS_DARWIN)
#include <CoreAudio/AudioHardware.h>
#include <CoreAudio/CoreAudio.h>
#elif defined(Q_OS_WIN32)
#include <dsound.h>
#elif defined(Q_OS_UNIX)
#include <alsa/asoundlib.h>
#endif

#include "monitorplayer.h"

namespace {
bool holdsLegacyValueArray(const GValue* value)
{
    Q_UNUSED(value);
    return false;
}

guint legacyValueArrayCount(const GValue* value)
{
    Q_UNUSED(value);
    return 0;
}

const GValue* legacyValueArrayAt(const GValue* value, guint index)
{
    Q_UNUSED(value);
    Q_UNUSED(index);
    return nullptr;
}

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
    if (G_VALUE_HOLDS_STRING(value)) {
        const gchar* str = g_value_get_string(value);
        if (str == nullptr)
            return false;

        if (g_ascii_strcasecmp(str, "-inf") == 0 || g_ascii_strcasecmp(str, "-infinity") == 0) {
            *out = -INFINITY;
            return true;
        }
        if (g_ascii_strcasecmp(str, "+inf") == 0 || g_ascii_strcasecmp(str, "inf") == 0 || g_ascii_strcasecmp(str, "infinity") == 0) {
            *out = INFINITY;
            return true;
        }

        gchar* end = nullptr;
        const gdouble v = g_ascii_strtod(str, &end);
        if (end != str) {
            *out = v;
            return true;
        }
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

double dbToMeter(gdouble db)
{
    if (!std::isfinite(db))
        return 0.0;

    // Clamp to a useful meter range and convert dBFS to linear amplitude.
    if (db < -60.0)
        db = -60.0;
    if (db > 0.0)
        db = 0.0;
    return std::pow(10.0, db / 20.0);
}
}

void MonitorPlayer::sync_set_state(GstElement* element, GstState state)
{
    GstStateChangeReturn res;
    res = gst_element_set_state(GST_ELEMENT(element), state);
    if (res == GST_STATE_CHANGE_FAILURE)
        return;
    if (res == GST_STATE_CHANGE_ASYNC) {
        GstState state;
        res = gst_element_get_state(GST_ELEMENT(element), &state, NULL, 1000000000 /*GST_CLOCK_TIME_NONE*/);
        if (res == GST_STATE_CHANGE_FAILURE || res == GST_STATE_CHANGE_ASYNC)
            return;
    }
}

void cb_newpad_mp(GstElement* src,
    GstPad* new_pad,
    gpointer data)
{
    MonitorPlayer* instance = (MonitorPlayer*)data;
    instance->newpad(src, new_pad, data);
}

void MonitorPlayer::newpad(GstElement* src,
    GstPad* new_pad,
    gpointer data)
{
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
}

struct MonitorPlayerPrivate {
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
    bool fallbackTried;
};

MonitorPlayer::MonitorPlayer(QWidget* parent)
    : QWidget(parent)
    , p(new MonitorPlayerPrivate)
    , pipeline(nullptr)
    , bus(nullptr)
    , Gstart(0)
    , Glength(0)
{
    p->isStarted = false;
    p->isLoaded = false;
    p->isDisabled = false;
    p->rms_l = 0;
    p->rms_r = 0;
    p->fallbackTried = false;
    readDevices();
    p->deviceID = defaultDeviceID();

    connect(&p->watcher, SIGNAL(finished()), this, SLOT(loadThreadFinished()));
}

MonitorPlayer::~MonitorPlayer()
{
    if (p->watcher.isRunning())
        p->watcher.waitForFinished();

    cleanup();
    delete p;
    p = nullptr;
}

GstBusSyncReply MonitorPlayer::bus_cb(GstBus* bus, GstMessage* msg, gpointer data)
{
    Q_UNUSED(bus);

    MonitorPlayer* instance = (MonitorPlayer*)data;
    instance->messageReceived(msg);
    return GST_BUS_PASS;
}

void MonitorPlayer::cleanup()
{
    if (pipeline)
        sync_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
    if (bus)
        gst_object_unref(bus);
    if (pipeline)
        gst_object_unref(G_OBJECT(pipeline));
}

bool MonitorPlayer::prepare()
{
    // Init Gst
    qDebug() << Q_FUNC_INFO << " "
             << "START";
    QString caps_value;

    gst_init(nullptr, nullptr);

    //prepare
    GstElement *src, *conv, *resample, *sink, *level, *vol;
    GstCaps* caps;
    pipeline = gst_pipeline_new("pipeline");
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    src = gst_element_factory_make("uridecodebin", "source");

    caps_value = "audio/x-raw";
    caps = gst_caps_new_simple(caps_value.toLatin1().data(),
        "channels", G_TYPE_INT, 2, NULL);
    g_signal_connect(src, "pad-added", G_CALLBACK(cb_newpad_mp), this);

    conv = gst_element_factory_make("audioconvert", "convert");
    vol = gst_element_factory_make("volume", "volume");
    resample = gst_element_factory_make("audioresample", "resample");
    level = gst_element_factory_make("level", "level");

#if defined(Q_OS_DARWIN)
    sink = gst_element_factory_make("osxaudiosink", "sink");
    g_object_set(sink, "device", 0, NULL);
    qInfo() << Q_FUNC_INFO << "macOS: osxaudiosink created, initial device=0 (system default)";
    //g_object_set (sink, "volume", 9.5, NULL);
#elif defined(Q_OS_WIN32)
    sink = gst_element_factory_make("directsoundsink", "sink");
    g_object_set(sink, "device", NULL, NULL);
    //g_object_set (sink, "volume", 99.5, NULL);
#elif defined(Q_OS_UNIX)
    sink = gst_element_factory_make("alsasink", "sink");
    g_object_set(sink, "device", "default", NULL);
    //g_object_set (sink, "volume", 0.5, NULL);
#else
    sink = gst_element_factory_make("fakesink", "sink");
    g_object_set(sink, "device", NULL, NULL);
#endif

    gst_bin_add_many(GST_BIN(pipeline), src, conv, resample, level, vol, sink, NULL);
    configureLevelMessaging(level);
    gst_element_link(conv, resample);
    gst_element_link_filtered(resample, level, caps);
    gst_element_link(level, vol);
    gst_element_link(vol, sink);

    gst_element_set_state(src, GST_STATE_NULL);

    gst_bus_set_sync_handler(bus, bus_cb, this, nullptr);

    qDebug() << Q_FUNC_INFO << " "
             << "END";

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
    QFuture<void> future = QtConcurrent::run([this, url]() { asyncOpen(url); });
    p->watcher.setFuture(future);
}

void MonitorPlayer::asyncOpen(QUrl url)
{
    QMutexLocker locker(&p->mutex);
    p->length = 0;
    p->isLoaded = false;
    p->fallbackTried = false;
    p->error = "";

    sync_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);

    GstElement* src = gst_bin_get_by_name(GST_BIN(pipeline), "source");
    g_object_set(G_OBJECT(src), "uri", (const char*)url.toString().toUtf8(), NULL);

#if defined(Q_OS_DARWIN)
    // osxaudiosink routing is most reliable when device is applied in NULL state,
    // before prerolling to PAUSED.
    if (!p->deviceID.isEmpty()) {
        GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
        if (sink != nullptr) {
            bool ok = false;
            const gint devInt = p->deviceID.toInt(&ok);
            if (ok) {
                g_object_set(sink, "device", devInt, NULL);
                gint readback = 0;
                g_object_get(sink, "device", &readback, NULL);
                qInfo() << Q_FUNC_INFO << "apply device before preroll:" << devInt << "readback:" << readback;
            } else {
                qWarning() << Q_FUNC_INFO << "invalid deviceID, cannot convert to int:" << p->deviceID;
            }
            gst_object_unref(sink);
        }
    }
#endif

    sync_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
    setPosition(QTime(0, 0));

    gst_object_unref(src);
}

void MonitorPlayer::loadThreadFinished()
{
    // async load in MonitorPlayerGst done
    qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName();
    p->isLoaded = true;
    if (p->isStarted) {
        play();
    }
    emit loadFinished();
}

void MonitorPlayer::play()
{
    p->isStarted = true;
    GstState cur_state = GST_STATE_NULL;
    gst_element_get_state(GST_ELEMENT(pipeline), &cur_state, nullptr, 0);
    qInfo() << Q_FUNC_INFO << ":" << parentWidget()->objectName()
             << "deviceName=" << (p->deviceName.isEmpty() ? "(none)" : p->deviceName)
             << "isLoaded=" << p->isLoaded
             << "pipelineState=" << gst_state_get_name(cur_state);
    if (!p->deviceName.isEmpty())
        setOutputDevice(p->deviceName);
    if (p->isLoaded) {
        gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
    }
}

void MonitorPlayer::stop()
{
    p->isStarted = false;
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_READY);
}

void MonitorPlayer::pause()
{
    if (isPlaying()) {
        p->isStarted = false;
        gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
    }
}

bool MonitorPlayer::close()
{
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
    return true;
}

void MonitorPlayer::setPosition(QTime position)
{
    int time_milliseconds = QTime(0, 0).msecsTo(position);
    gint64 time_nanoseconds = (time_milliseconds * GST_MSECOND);
    gst_element_seek(pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
        GST_SEEK_TYPE_SET, time_nanoseconds,
        GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
    p->position = time_milliseconds;
    emit positionChanged();
}

QTime MonitorPlayer::position()
{
    if (pipeline) {

        gint64 value = 0;

        if (gst_element_query_position(pipeline, GST_FORMAT_TIME, &value)) {
            p->position = static_cast<uint>((value / GST_MSECOND));
            return QTime(0, 0).addMSecs(p->position); // nanosec -> msec
        }
        return QTime(0, 0).addMSecs(p->position); // nanosec -> msec
    }
    return QTime(0, 0);
}

QTime MonitorPlayer::length()
{
    gint64 value = 0;

    if (p->length == 0 && pipeline) {

        if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &value)) {
            p->length = static_cast<uint>((value / GST_MSECOND));
        }
    }
    return QTime(0, 0).addMSecs(p->length); // nanosec -> msec
}

double MonitorPlayer::volume()
{
    gdouble vol = 0;

    GstElement* volume = gst_bin_get_by_name(GST_BIN(pipeline), "volume");
    g_object_get(G_OBJECT(volume), "volume", &vol, NULL);
    gst_object_unref(volume);

    return vol;
}

void MonitorPlayer::setVolume(double v)
{
    gdouble vol = 1.00 * v;
    GstElement* volume = gst_bin_get_by_name(GST_BIN(pipeline), "volume");
    g_object_set(G_OBJECT(volume), "volume", vol, NULL);
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
    gst_element_get_state(GST_ELEMENT(pipeline), &st, 0, 0);
    //qDebug()<<gst_state_get_name(st);
    return (st != GST_STATE_NULL);
}

bool MonitorPlayer::isPlaying()
{
    GstState st;
    gst_element_get_state(GST_ELEMENT(pipeline), &st, 0, 0);
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
    if (pipeline == nullptr)
        return;

    // for Mac device is DeviceID
    bool found = false;
    QMapIterator<QString, QString> i(p->devices);
    while (i.hasNext()) {
        i.next();
        if (i.value() == deviceName) {
            p->deviceID = i.key();
            p->deviceName = i.value();
            found = true;
            break;
        }
    }

    if (!found) {
        qWarning() << Q_FUNC_INFO << "setDevice failed, unknown device name:" << deviceName
                   << "- available:" << p->devices.values();
        return;
    }

    qInfo() << Q_FUNC_INFO << "deviceName=" << p->deviceName << "deviceID=" << p->deviceID;

    GstState cur_state = GST_STATE_NULL;
    gst_element_get_state(GST_ELEMENT(pipeline), &cur_state, nullptr, 0);
    qInfo() << Q_FUNC_INFO << "pipelineState before set:" << gst_state_get_name(cur_state);

    GstState restore_state = cur_state;

#if defined(Q_OS_DARWIN)
    // For macOS, force NULL before setting "device" to ensure CoreAudio unit is
    // recreated on the selected endpoint and not silently kept on default output.
    if (cur_state != GST_STATE_NULL) {
        qInfo() << Q_FUNC_INFO << "transitioning pipeline to NULL to apply macOS device change";
        gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
        GstStateChangeReturn ret = gst_element_get_state(GST_ELEMENT(pipeline), nullptr, nullptr, 2 * GST_SECOND);
        qInfo() << Q_FUNC_INFO << "state change to NULL result:" << ret;
    }
#else
    // For other sinks, READY is enough for changing the output device.
    if (cur_state > GST_STATE_READY) {
        qInfo() << Q_FUNC_INFO << "transitioning pipeline to READY to apply device change";
        gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_READY);
        GstStateChangeReturn ret = gst_element_get_state(GST_ELEMENT(pipeline), nullptr, nullptr, 2 * GST_SECOND);
        qInfo() << Q_FUNC_INFO << "state change to READY result:" << ret;
    }
#endif

    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
#if defined(Q_OS_WIN32)
    g_object_set(sink, "device", p->deviceID.toLatin1().data(), NULL);
#elif defined(Q_OS_DARWIN)
    {
        gint devInt = p->deviceID.toInt();
        g_object_set(sink, "device", devInt, NULL);
        gint readback = 0;
        g_object_get(sink, "device", &readback, NULL);
        qInfo() << Q_FUNC_INFO << "osxaudiosink: set device" << devInt << "-> readback:" << readback;
    }
#elif defined(Q_OS_UNIX)
    {
        const QString alsaDev = QString("hw:%1").arg(p->deviceID);
        g_object_set(sink, "device", alsaDev.toLatin1().data(), NULL);
        qInfo() << Q_FUNC_INFO << "alsasink: set device" << alsaDev;
    }
#endif
    gst_object_unref(sink);

    // Restore the original state after device update.
    if (restore_state != GST_STATE_NULL) {
        qInfo() << Q_FUNC_INFO << "restoring pipeline to" << gst_state_get_name(restore_state);
        gst_element_set_state(GST_ELEMENT(pipeline), restore_state);
    }
}

#if defined(Q_OS_WIN32)
BOOL CALLBACK DSEnumProc(LPGUID lpGUID, const WCHAR* lpszDesc,
    const WCHAR* lpszDrvName, void* ctx)
{
    if (lpGUID) {
        QList<dsDevice>* l = reinterpret_cast<QList<dsDevice>*>(ctx);
        *l << dsDevice(QString::fromWCharArray(lpszDesc), QUuid(*lpGUID));
    }

    return (true);
}
#endif

QString MonitorPlayer::defaultDeviceID()
{
#if defined(Q_OS_WIN32)
    GUID guid;
    //DSDEVID_DefaultVoicePlayback="{DEF00000-9C6D-47ED-AAF1-4DDA8F2B5C03}"
    const GUID defaultguid = QUuid("{DEF00000-9C6D-47ED-AAF1-4DDA8F2B5C03}");
    if (GetDeviceID(&defaultguid, &guid) == DS_OK) {
        return QUuid(guid).toString();
    } else
        return QString();
#elif defined(Q_OS_DARWIN)
    AudioDeviceID defaultDevice = 0;
    UInt32 dataSize = sizeof(defaultDevice);
    AudioObjectPropertyAddress propertyAddress;
    propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    propertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
    propertyAddress.mElement = kAudioObjectPropertyElementMain;

    const OSStatus status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &propertyAddress,
        0,
        NULL,
        &dataSize,
        &defaultDevice);

    if (status == noErr && defaultDevice != 0)
        return QString::number(defaultDevice);

    qWarning() << Q_FUNC_INFO << "failed to query default output device, fallback to 0, status=" << status;
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

    DirectSoundEnumerate(DSEnumProc, reinterpret_cast<void*>(&qlOutput));

    for (const auto& dev : qlOutput) {
        p->devices.insert(dev.second.toString(), dev.first);
    }

#elif defined(Q_OS_DARWIN)
    auto hasOutputChannels = [](AudioDeviceID devId) -> bool {
        AudioObjectPropertyAddress channelsAddress;
        channelsAddress.mSelector = kAudioDevicePropertyStreamConfiguration;
        channelsAddress.mScope = kAudioDevicePropertyScopeOutput;
        channelsAddress.mElement = kAudioObjectPropertyElementMain;

        UInt32 listSize = 0;
        OSStatus s = AudioObjectGetPropertyDataSize(devId, &channelsAddress, 0, NULL, &listSize);
        if (s != noErr || listSize == 0)
            return false;

        AudioBufferList* bufferList = (AudioBufferList*)malloc(listSize);
        if (bufferList == NULL)
            return false;

        s = AudioObjectGetPropertyData(devId, &channelsAddress, 0, NULL, &listSize, bufferList);
        if (s != noErr) {
            free(bufferList);
            return false;
        }

        UInt32 totalChannels = 0;
        for (UInt32 bi = 0; bi < bufferList->mNumberBuffers; ++bi)
            totalChannels += bufferList->mBuffers[bi].mNumberChannels;

        free(bufferList);
        return totalChannels > 0;
    };

    UInt32 dataSize = 0;
    AudioObjectPropertyAddress propertyAddress;
    propertyAddress.mSelector = kAudioHardwarePropertyDevices;
    propertyAddress.mScope = kAudioDevicePropertyScopeOutput;
    propertyAddress.mElement = kAudioObjectPropertyElementMain;

    OSStatus status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &dataSize);
    if (kAudioHardwareNoError != status) {
        qDebug() << "Unable to get number of audio devices. Error: " << status;
    }

    UInt32 deviceCount = dataSize / sizeof(AudioDeviceID);

    AudioDeviceID* audioDevices = (AudioDeviceID*)malloc(dataSize);

    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &dataSize, audioDevices);
    if (kAudioHardwareNoError != status) {
        qDebug() << "AudioObjectGetPropertyData failed when getting device IDs. Error:" << status;
        free(audioDevices), audioDevices = NULL;
        return;
    }

    p->devices.clear();

    for (UInt32 i = 0; i < deviceCount; i++) {
        if (!hasOutputChannels(audioDevices[i])) {
            qInfo() << Q_FUNC_INFO << "skip non-output CoreAudio device id" << audioDevices[i];
            continue;
        }

        CFStringRef deviceNameRef = NULL;
        UInt32 nameSize = sizeof(deviceNameRef);

        propertyAddress.mSelector = kAudioDevicePropertyDeviceNameCFString;
        propertyAddress.mScope = kAudioDevicePropertyScopeOutput;
        status = AudioObjectGetPropertyData(audioDevices[i], &propertyAddress, 0, NULL, &nameSize, &deviceNameRef);
        if (status != noErr || deviceNameRef == NULL)
            continue;

        char deviceNameCstr[512];
        memset(deviceNameCstr, 0, sizeof(deviceNameCstr));
        if (CFStringGetCString(deviceNameRef, deviceNameCstr, sizeof(deviceNameCstr), kCFStringEncodingUTF8)) {
            const QString devName = QString::fromUtf8(deviceNameCstr).trimmed();
            if (!devName.isEmpty()) {
                p->devices.insert(QString::number(audioDevices[i]), devName);
                qInfo() << Q_FUNC_INFO << "found output device" << devName << "id" << audioDevices[i];
            }
        }

        CFRelease(deviceNameRef);
    }

    free(audioDevices);
#elif defined(Q_OS_UNIX)
    int idx = 0;
    char* name;

    while (snd_card_get_name(idx, &name) == 0) {
        p->devices.insert(QString::number(idx), QString(name));
        idx++;
    }
#endif
}

void MonitorPlayer::messageReceived(GstMessage* message)
{

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &err, &debug);

        const QString errMsg = err != nullptr ? QString::fromUtf8(err->message) : QString();
        const QString debugMsg = debug != nullptr ? QString::fromUtf8(debug) : QString();
        const bool coreAudioOpenFailed = errMsg.contains("CoreAudio device could not be opened", Qt::CaseInsensitive);

#if defined(Q_OS_DARWIN)
        if (coreAudioOpenFailed && !p->fallbackTried) {
            const QString fallbackId = defaultDeviceID();
            if (!fallbackId.isEmpty() && fallbackId != p->deviceID) {
                p->fallbackTried = true;
                qWarning() << Q_FUNC_INFO
                           << "CoreAudio open failed for device" << p->deviceID << p->deviceName
                           << "- retry with default output device id" << fallbackId;

                GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
                if (sink != nullptr) {
                    bool ok = false;
                    const gint devInt = fallbackId.toInt(&ok);
                    if (ok) {
                        g_object_set(sink, "device", devInt, NULL);
                        gint readback = 0;
                        g_object_get(sink, "device", &readback, NULL);
                        qWarning() << Q_FUNC_INFO << "fallback set device" << devInt << "readback" << readback;
                        p->deviceID = fallbackId;
                        p->deviceName = p->devices.value(fallbackId, QStringLiteral("System Default"));

                        // Re-preroll pipeline on the fallback device.
                        sync_set_state(GST_ELEMENT(pipeline), GST_STATE_READY);
                        sync_set_state(GST_ELEMENT(pipeline), GST_STATE_PAUSED);
                        if (p->isStarted && p->isLoaded)
                            sync_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
                    }
                    gst_object_unref(sink);
                }

                if (err != nullptr)
                    g_error_free(err);
                if (debug != nullptr)
                    g_free(debug);
                return;
            }
        }
#endif

        if (p->error == "") {
            p->error = "Error #" + QString::number(err != nullptr ? err->code : 0)
                + " in module " + QString::number(err != nullptr ? err->domain : 0)
                + "\n" + errMsg;
            if (err != nullptr && err->code == 6 && err->domain == 851) {
                p->error += "\nMay be you should to install gstreamerX.XX-plugins-ugly or gstreamerX.XX-plugins-bad";
            }
            qWarning() << "Gstreamer error:" << p->error;
            if (!debugMsg.isEmpty())
                qWarning() << "Gstreamer debug:" << debugMsg;
            Q_EMIT error();
        }

        if (err != nullptr)
            g_error_free(err);
        if (debug != nullptr)
            g_free(debug);
        break;
    }
    case GST_MESSAGE_EOS: {
        qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName() << " End of track reached";
        Q_EMIT finish();
        break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
        GstState old_state, new_state;
    gst_message_parse_state_changed(message, &old_state, &new_state, NULL);
        switch (new_state) {
        case GST_STATE_PAUSED:
        case GST_STATE_NULL:
            p->rms_l = p->rms_r = 0;
        default:
            break;
        }
        break;
    }

    case GST_MESSAGE_ELEMENT: {
        const GstStructure* s = gst_message_get_structure(message);
        if (s == nullptr)
            break;

        const gchar* structName = gst_structure_get_name(s);
        if (g_strcmp0(structName, "level") != 0)
            break;

        const char* dbFieldName = nullptr;
        const GValue* dbValues = levelDbValues(s, &dbFieldName);
        if (dbValues == nullptr) {
            qInfo() << Q_FUNC_INFO << "level message without rms/peak values";
            break;
        }

        const guint channels = peakValueCount(dbValues);
        if (channels == 0)
            break;

        gdouble db_l = -INFINITY;
        gdouble db_r = -INFINITY;
        bool updated = false;

        for (guint i = 0; i < channels; ++i) {
            const GValue* dbValue = peakValueAt(dbValues, i);
            gdouble levelDb = 0.0;
            if (!gValueToDouble(dbValue, &levelDb)) {
                qInfo() << Q_FUNC_INFO << "unable to parse" << dbFieldName << "value for channel" << i
                        << "gtype=" << (dbValue != nullptr ? G_VALUE_TYPE_NAME(dbValue) : "(null)");
                continue;
            }

            const gdouble meter = dbToMeter(levelDb);
            updated = true;

            if (i == 0) {
                db_l = levelDb;
                p->rms_l = meter;
            } else if (i == 1) {
                db_r = levelDb;
                p->rms_r = meter;
            }
        }

        if (!updated)
            break;

        if (channels == 1)
            p->rms_r = p->rms_l;

        if (!std::isfinite(db_r) && std::isfinite(db_l))
            db_r = db_l;

        qInfo() << "MonitorPlayer level:" << dbFieldName
                << "dB(L/R)=" << db_l << db_r
                << "meter(L/R)=" << p->rms_l << p->rms_r;
        Q_EMIT levelChanged();

    } break;
    default:
        break;
    }
}
