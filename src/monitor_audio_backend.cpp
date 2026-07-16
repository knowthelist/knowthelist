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

#include "monitor_audio_backend.h"
#include <QString>
#include <QDebug>

namespace {
float computeRMS(const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return 0.0f;

    double sumSquares = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sumSquares += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);

    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(numSamples)));
}
}

#if defined(Q_OS_DARWIN)
#include <CoreAudio/AudioHardware.h>
#include <CoreAudio/CoreAudio.h>
#elif defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
#include <alsa/asoundlib.h>
#endif

MonitorAudioBackend::MonitorAudioBackend()
    : deviceManager(std::make_unique<juce::AudioDeviceManager>()),
      formatManager(std::make_unique<juce::AudioFormatManager>()),
      transportSource(std::make_unique<juce::AudioTransportSource>())
{
    qDebug() << Q_FUNC_INFO << "Creating MonitorAudioBackend";
}

MonitorAudioBackend::~MonitorAudioBackend()
{
    // Remove the audio callback FIRST so no audio thread can access members
    // that are about to be destroyed.
    if (deviceManager) {
        deviceManager->removeAudioCallback(this);
        deviceManager->closeAudioDevice();
    }
    // Detach the reader source before it is freed by the unique_ptr destructor.
    if (transportSource)
        transportSource->setSource(nullptr);
    currentSource.reset();
}

bool MonitorAudioBackend::initialize()
{
    try {
        qDebug() << Q_FUNC_INFO << "Initializing MonitorAudioBackend";
        
        // Register standard audio formats
        formatManager->registerBasicFormats();

        // Read available devices
        readDevices();

        // Initialize with default device
        QString defaultId = getDefaultDeviceIDPlatform();
        if (defaultId.isEmpty() && !deviceMap.isEmpty()) {
            defaultId = deviceMap.keys().first();
        }
        currentDeviceId = defaultId;

        // Initialize audio device manager with default devices
        auto error = deviceManager->initialiseWithDefaultDevices(0, 2);
        if (!error.isEmpty()) {
            lastError = QString::fromStdString(error.toStdString());
            qWarning() << "Failed to initialize device manager:" << lastError;
            return false;
        }

        // Set up audio callback
        deviceManager->addAudioCallback(this);

        return true;
    } catch (const std::exception& e) {
        lastError = QString("Monitor backend initialization failed: %1").arg(e.what());
        qWarning() << lastError;
        return false;
    }
}

void MonitorAudioBackend::load(const QUrl& url)
{
    try {
        if (url.isEmpty()) {
            lastError = "Empty URL";
            return;
        }

        juce::File audioFile(url.toLocalFile().toStdString());
        if (!audioFile.existsAsFile()) {
            lastError = QString("File not found: %1").arg(url.toString());
            qWarning() << lastError;
            return;
        }

        auto reader = formatManager->createReaderFor(audioFile);
        if (!reader) {
            lastError = QString("Cannot open audio file: %1").arg(url.toString());
            qWarning() << lastError;
            return;
        }

        transportSource->setSource(nullptr);
        currentSource.reset();
        currentSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
        transportSource->setSource(currentSource.get());
        lastError = QString();
        qDebug() << Q_FUNC_INFO << "Loaded file:" << url.toString();
    } catch (const std::exception& e) {
        lastError = QString("Load failed: %1").arg(e.what());
        qWarning() << lastError;
    }
}

void MonitorAudioBackend::play()
{
    if (enabledFlag && transportSource) {
        transportSource->start();
        qDebug() << Q_FUNC_INFO << "Monitor playback started";
    }
}

void MonitorAudioBackend::pause()
{
    if (transportSource) {
        transportSource->stop();
        qDebug() << Q_FUNC_INFO << "Monitor playback paused";
    }
}

void MonitorAudioBackend::stop()
{
    if (transportSource) {
        transportSource->stop();
        transportSource->setPosition(0.0);
        qDebug() << Q_FUNC_INFO << "Monitor playback stopped";
    }
}

void MonitorAudioBackend::seek(const QTime& position)
{
    if (transportSource) {
        int ms = QTime(0, 0).msecsTo(position);
        double seconds = ms / 1000.0;
        transportSource->setPosition(seconds);
    }
}

QTime MonitorAudioBackend::getPosition()
{
    if (transportSource) {
        double seconds = transportSource->getCurrentPosition();
        int ms = static_cast<int>(seconds * 1000);
        return QTime(0, 0).addMSecs(ms);
    }
    return QTime(0, 0);
}

QTime MonitorAudioBackend::getDuration()
{
    if (transportSource) {
        double seconds = transportSource->getLengthInSeconds();
        int ms = static_cast<int>(seconds * 1000);
        return QTime(0, 0).addMSecs(ms);
    }
    return QTime(0, 0);
}

bool MonitorAudioBackend::isPlaying()
{
    if (transportSource)
        return transportSource->isPlaying();
    return false;
}

bool MonitorAudioBackend::isLoaded()
{
    if (transportSource)
        return currentSource != nullptr;
    return false;
}

void MonitorAudioBackend::setRate(double rate)
{
    currentRate = juce::jlimit(0.5, 2.0, rate);
}

double MonitorAudioBackend::getRate()
{
    return currentRate;
}

void MonitorAudioBackend::setVolume(double volume)
{
    currentVolume = juce::jlimit(0.0, 2.0, volume);
}

double MonitorAudioBackend::getVolume()
{
    return currentVolume;
}

double MonitorAudioBackend::getLevelLeft()
{
    return levelLeft;
}

double MonitorAudioBackend::getLevelRight()
{
    return levelRight;
}

QString MonitorAudioBackend::getLastError()
{
    return lastError;
}

void MonitorAudioBackend::setMonitorDeviceId(const QString& deviceId)
{
    setOutputDevice(deviceId);
}

void MonitorAudioBackend::setUseMonitorOutput(bool enabled)
{
    // No-op for monitor backend (it's always monitor)
    Q_UNUSED(enabled);
}

bool MonitorAudioBackend::useMonitorOutput()
{
    return true;
}

void MonitorAudioBackend::setMonitorVolume(double volume)
{
    setVolume(volume);
}

QStringList MonitorAudioBackend::getOutputDevices()
{
    return deviceMap.values();
}

QString MonitorAudioBackend::getOutputDeviceID()
{
    return currentDeviceId;
}

QString MonitorAudioBackend::getOutputDeviceName()
{
    return currentDeviceName;
}

QString MonitorAudioBackend::getDefaultDeviceID()
{
    return getDefaultDeviceIDPlatform();
}

void MonitorAudioBackend::setOutputDevice(const QString& deviceNameOrId)
{
    // Try to find device by ID first, then by name
    if (deviceMap.contains(deviceNameOrId)) {
        currentDeviceId = deviceNameOrId;
        currentDeviceName = deviceMap[deviceNameOrId];
    } else {
        // Search by name
        for (auto it = deviceMap.begin(); it != deviceMap.end(); ++it) {
            if (it.value() == deviceNameOrId) {
                currentDeviceId = it.key();
                currentDeviceName = it.value();
                break;
            }
        }
    }
    qDebug() << Q_FUNC_INFO << "Set output device to:" << currentDeviceName << "(" << currentDeviceId << ")";

    // Reconfigure JUCE AudioDeviceManager to use the selected device
    if (!currentDeviceName.isEmpty() && deviceManager) {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager->getAudioDeviceSetup(setup);
        setup.outputDeviceName = juce::String(currentDeviceName.toStdString());
        setup.useDefaultOutputChannels = false;
        const auto error = deviceManager->setAudioDeviceSetup(setup, true);
        if (!error.isEmpty()) {
            qWarning() << Q_FUNC_INFO << "Failed to switch monitor device to:"
                       << currentDeviceName
                       << "error:" << QString::fromStdString(error.toStdString());
        }
    }
}

void MonitorAudioBackend::setEnabled(bool enabled)
{
    if (enabled == enabledFlag)
        return;

    if (enabled && wasPlayingBeforeDisable) {
        // Resume playback
        play();
        wasPlayingBeforeDisable = false;
    } else if (!enabled && isPlaying()) {
        // Pause playback
        wasPlayingBeforeDisable = true;
        pause();
    }
    
    enabledFlag = enabled;
    qDebug() << Q_FUNC_INFO << "Monitor output" << (enabled ? "enabled" : "disabled");
}

bool MonitorAudioBackend::isEnabled() const
{
    return enabledFlag;
}

void MonitorAudioBackend::readDevices()
{
    readDevicesPlatform();
}

#if defined(Q_OS_DARWIN)
QString MonitorAudioBackend::getDefaultDeviceIDPlatform()
{
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

    qWarning() << Q_FUNC_INFO << "Failed to query default output device, status=" << status;
    return QString();
}

void MonitorAudioBackend::readDevicesPlatform()
{
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
        qWarning() << "Unable to get number of audio devices. Error:" << status;
        return;
    }

    UInt32 deviceCount = dataSize / sizeof(AudioDeviceID);
    AudioDeviceID* audioDevices = (AudioDeviceID*)malloc(dataSize);

    status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &dataSize, audioDevices);
    if (kAudioHardwareNoError != status) {
        qWarning() << "AudioObjectGetPropertyData failed when getting device IDs. Error:" << status;
        free(audioDevices);
        return;
    }

    deviceMap.clear();

    for (UInt32 i = 0; i < deviceCount; i++) {
        if (!hasOutputChannels(audioDevices[i])) {
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
                deviceMap.insert(QString::number(audioDevices[i]), devName);
                qInfo() << Q_FUNC_INFO << "Found output device:" << devName << "id:" << audioDevices[i];
            }
        }

        CFRelease(deviceNameRef);
    }

    free(audioDevices);
}

#elif defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
QString MonitorAudioBackend::getDefaultDeviceIDPlatform()
{
    return QString("default");
}

void MonitorAudioBackend::readDevicesPlatform()
{
    deviceMap.clear();
    int idx = 0;
    char* name;

    while (snd_card_get_name(idx, &name) == 0) {
        deviceMap.insert(QString::number(idx), QString(name));
        qInfo() << Q_FUNC_INFO << "Found ALSA device:" << name << "id:" << idx;
        idx++;
    }

    // Add default entry
    if (!deviceMap.isEmpty()) {
        deviceMap.insert(QString("default"), QString("Default Output"));
    }
}

#else  // Windows or other
QString MonitorAudioBackend::getDefaultDeviceIDPlatform()
{
    return QString();
}

void MonitorAudioBackend::readDevicesPlatform()
{
    deviceMap.clear();
    // On Windows, JUCE can enumerate devices via its device manager
    // For now, just add a placeholder
    deviceMap.insert(QString("default"), QString("Default Output"));
}
#endif

void MonitorAudioBackend::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                                           float* const* outputChannelData, int numOutputChannels,
                                                           int numSamples,
                                                           const juce::AudioIODeviceCallbackContext& context)
{
    Q_UNUSED(inputChannelData);
    Q_UNUSED(numInputChannels);
    Q_UNUSED(context);
    
    // Process audio through transport source
    juce::AudioBuffer<float> buffer(outputChannelData, numOutputChannels, numSamples);
    juce::AudioSourceChannelInfo info(&buffer, 0, numSamples);
    transportSource->getNextAudioBlock(info);

    // Apply volume
    float volumeLinear = static_cast<float>(currentVolume);
    for (int ch = 0; ch < numOutputChannels; ++ch) {
        juce::FloatVectorOperations::multiply(outputChannelData[ch], volumeLinear, numSamples);
    }

    // Metering
    if (numOutputChannels >= 2) {
        float rmsL = computeRMS(outputChannelData[0], numSamples);
        float rmsR = computeRMS(outputChannelData[1], numSamples);
        levelLeft = rmsL;
        levelRight = rmsR;
    }
}

void MonitorAudioBackend::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (transportSource) {
        transportSource->prepareToPlay(device->getCurrentBufferSizeSamples(), 
                                        device->getCurrentSampleRate());
    }
}

void MonitorAudioBackend::audioDeviceStopped()
{
    if (transportSource) {
        transportSource->releaseResources();
    }
}
