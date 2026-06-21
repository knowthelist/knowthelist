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

#include "juce_audio_backend.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <QString>
#include <cmath>

#if defined(Q_OS_DARWIN)
#include <CoreAudio/CoreAudio.h>
static QString coreAudioDeviceIdToName(const QString& deviceId)
{
    bool ok = false;
    const AudioDeviceID devId = static_cast<AudioDeviceID>(deviceId.toUInt(&ok));
    if (!ok || devId == 0)
        return {};

    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyDeviceNameCFString,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    CFStringRef nameRef = nullptr;
    UInt32 dataSize = sizeof(nameRef);
    if (AudioObjectGetPropertyData(devId, &addr, 0, nullptr, &dataSize, &nameRef) != noErr || !nameRef)
        return {};
    char buf[512] = {};
    CFStringGetCString(nameRef, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(nameRef);
    return QString::fromUtf8(buf).trimmed();
}
#else
static QString coreAudioDeviceIdToName(const QString&) { return {}; }
#endif

// ---------------------------------------------------------------------------
// Lock-free FIFO callback that plays deck audio through the monitor device.
// The main output callback pushes processed samples; this callback pulls them.
// ---------------------------------------------------------------------------
class JuceAudioBackend::MonitorTeeCallback : public juce::AudioIODeviceCallback {
public:
    static constexpr int kCapacity = 32768; // samples per channel

    juce::AbstractFifo fifo{kCapacity};
    juce::AudioBuffer<float> buffer{2, kCapacity};
    std::atomic<float> volume{1.0f};

    void push(const juce::AudioBuffer<float>& src, int numSamples)
    {
        if (numSamples <= 0)
            return;
        int s1, n1, s2, n2;
        fifo.prepareToWrite(numSamples, s1, n1, s2, n2);
        const int channels = juce::jmin(src.getNumChannels(), 2);
        if (n1 > 0)
            for (int ch = 0; ch < channels; ++ch)
                buffer.copyFrom(ch, s1, src, ch, 0, n1);
        if (n2 > 0)
            for (int ch = 0; ch < channels; ++ch)
                buffer.copyFrom(ch, s2, src, ch, n1, n2);
        fifo.finishedWrite(n1 + n2);
    }

    void audioDeviceIOCallbackWithContext(const float* const*, int,
                                          float* const* out, int numOut,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead(numSamples, s1, n1, s2, n2);

        const int channels = juce::jmin(numOut, 2);
        const int total = n1 + n2;
        for (int ch = 0; ch < channels; ++ch) {
            if (n1 > 0)
                std::copy_n(buffer.getReadPointer(ch, s1), n1, out[ch]);
            if (n2 > 0)
                std::copy_n(buffer.getReadPointer(ch, s2), n2, out[ch] + n1);
            if (total < numSamples)
                std::fill_n(out[ch] + total, numSamples - total, 0.0f);
        }
        for (int ch = channels; ch < numOut; ++ch)
            std::fill_n(out[ch], numSamples, 0.0f);
        fifo.finishedRead(n1 + n2);

        const float vol = volume.load();
        if (vol != 1.0f)
            for (int ch = 0; ch < channels; ++ch)
                juce::FloatVectorOperations::multiply(out[ch], vol, numSamples);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
};

class JuceAudioBackend::TempoSource : public juce::PositionableAudioSource {
public:
    explicit TempoSource(juce::PositionableAudioSource* source)
        : inputSource(source), resampler(source, false, 2)
    {
    }

    void setResamplingRatio(double ratio)
    {
        resampler.setResamplingRatio(ratio);
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        inputSource->prepareToPlay(samplesPerBlockExpected, sampleRate);
        resampler.prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    void releaseResources() override
    {
        resampler.releaseResources();
        inputSource->releaseResources();
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        resampler.getNextAudioBlock(info);
    }

    void setNextReadPosition(juce::int64 newPosition) override
    {
        inputSource->setNextReadPosition(newPosition);
        resampler.flushBuffers();
    }

    juce::int64 getNextReadPosition() const override
    {
        return inputSource->getNextReadPosition();
    }

    juce::int64 getTotalLength() const override
    {
        return inputSource->getTotalLength();
    }

    bool isLooping() const override
    {
        return inputSource->isLooping();
    }

private:
    juce::PositionableAudioSource* inputSource;
    juce::ResamplingAudioSource resampler;
};

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

float dbToGain(double db)
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}
} // anonymous namespace

JuceAudioBackend::JuceAudioBackend()
    : deviceManager(std::make_unique<juce::AudioDeviceManager>()),
      formatManager(std::make_unique<juce::AudioFormatManager>()),
      transportSource(std::make_unique<juce::AudioTransportSource>())
{
}

JuceAudioBackend::~JuceAudioBackend()
{
    // Remove the audio callback FIRST to stop the audio thread from accessing
    // any member of this object before we begin tearing them down.
    if (deviceManager) {
        deviceManager->removeAudioCallback(this);
        deviceManager->closeAudioDevice();
    }
    closeMonitorDevice();
    // Safe to release audio-source chain now that no callback can fire.
    if (transportSource)
        transportSource->setSource(nullptr);
    tempoSource.reset();
    currentSource.reset();
}

bool JuceAudioBackend::initialize()
{
    try {
        // Register standard audio formats
        formatManager->registerBasicFormats();

        // Initialize audio device manager
        auto error = deviceManager->initialiseWithDefaultDevices(0, 2);
        if (!error.isEmpty()) {
            lastError = QString::fromStdString(error.toStdString());
            return false;
        }

        // Set up audio callback
        deviceManager->addAudioCallback(this);

        return true;
    } catch (const std::exception& e) {
        lastError = QString("JUCE initialization failed: %1").arg(e.what());
        return false;
    }
}

void JuceAudioBackend::load(const QUrl& url)
{
    try {
        juce::File audioFile(url.toLocalFile().toStdString());
        if (!audioFile.existsAsFile()) {
            lastError = QString("File not found: %1").arg(url.toString());
            return;
        }

        auto reader = formatManager->createReaderFor(audioFile);
        if (!reader) {
            lastError = QString("Cannot open audio file: %1").arg(url.toString());
            return;
        }

        transportSource->setSource(nullptr);
        tempoSource.reset();
        currentSource.reset();
        sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 0.0;
        currentSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
        tempoSource = std::make_unique<TempoSource>(currentSource.get());
        tempoSource->setResamplingRatio(currentRate);
        // Tell transport the source timeline sample rate so setPosition(seconds)
        // and getCurrentPosition() map correctly to file time (not device rate).
        transportSource->setSource(tempoSource.get(), 0, nullptr,
                       sourceSampleRate > 0.0 ? sourceSampleRate : 0.0, 2);
        lastError = QString();
    } catch (const std::exception& e) {
        lastError = QString("Load failed: %1").arg(e.what());
    }
}

void JuceAudioBackend::play()
{
    transportSource->start();
}

void JuceAudioBackend::pause()
{
    transportSource->stop();
}

void JuceAudioBackend::stop()
{
    transportSource->stop();
    transportSource->setPosition(0.0);
}

void JuceAudioBackend::seek(const QTime& position)
{
    int ms = QTime(0, 0).msecsTo(position);
    double seconds = ms / 1000.0;
    transportSource->setPosition(seconds);
}

QTime JuceAudioBackend::getPosition()
{
    double seconds = 0.0;
    if (tempoSource && sourceSampleRate > 0.0) {
        // Use source read position so visuals map to the track timeline even
        // when playback speed is changed via resampling.
        const juce::int64 srcSamples = tempoSource->getNextReadPosition();
        seconds = static_cast<double>(srcSamples) / sourceSampleRate;
    } else {
        seconds = transportSource->getCurrentPosition();
    }
    int ms = static_cast<int>(seconds * 1000);
    return QTime(0, 0).addMSecs(ms);
}

QTime JuceAudioBackend::getDuration()
{
    double seconds = 0.0;
    if (currentSource && sourceSampleRate > 0.0) {
        // Keep duration in the same source timeline domain as getPosition().
        const juce::int64 totalSamples = currentSource->getTotalLength();
        seconds = static_cast<double>(totalSamples) / sourceSampleRate;
    } else {
        seconds = transportSource->getLengthInSeconds();
    }
    int ms = static_cast<int>(seconds * 1000);
    return QTime(0, 0).addMSecs(ms);
}

bool JuceAudioBackend::isPlaying()
{
    return transportSource->isPlaying();
}

bool JuceAudioBackend::isLoaded()
{
    return currentSource != nullptr;
}

void JuceAudioBackend::setRate(double rate)
{
    currentRate = juce::jlimit(0.5, 2.0, rate);
    if (tempoSource)
        tempoSource->setResamplingRatio(currentRate);
}

double JuceAudioBackend::getRate()
{
    return currentRate;
}

void JuceAudioBackend::setVolume(double volume)
{
    QMutexLocker locker(&stateGuard);
    masterVolume = juce::jlimit(0.0, 2.0, volume);
}

double JuceAudioBackend::getVolume()
{
    QMutexLocker locker(&stateGuard);
    return masterVolume;
}

void JuceAudioBackend::setGain(double gain)
{
    QMutexLocker locker(&stateGuard);
    // Legacy UI gain pot delivers a linear gain factor (1.0 == unity).
    gainDb = juce::jlimit(0.0, 4.0, gain);
}

void JuceAudioBackend::setEqualizer(const QString& band, double gain)
{
    if (band == "band0")
        eqBand0Gain.store(gain);
    else if (band == "band1")
        eqBand1Gain.store(gain);
    else if (band == "band2")
        eqBand2Gain.store(gain);
}

double JuceAudioBackend::getLevelLeft()
{
    return levelLeft;
}

double JuceAudioBackend::getLevelRight()
{
    return levelRight;
}

double JuceAudioBackend::getOutputLevelLeft()
{
    return outputLevelLeft;
}

double JuceAudioBackend::getOutputLevelRight()
{
    return outputLevelRight;
}

bool JuceAudioBackend::supportsSmoothTempo()
{
    return true; // JUCE supports smooth tempo via setPlaybackSpeedAndPitch
}

void JuceAudioBackend::setMonitorDeviceId(const QString& deviceId)
{
    monitorDeviceId = deviceId;
    const QString name = coreAudioDeviceIdToName(deviceId);
    if (!name.isEmpty())
        openMonitorDevice(name);
}

void JuceAudioBackend::openMonitorDevice(const QString& deviceName)
{
    closeMonitorDevice();

    if (!monitorDeviceManager)
        monitorDeviceManager = std::make_unique<juce::AudioDeviceManager>();

    monitorTeeCallback = std::make_unique<MonitorTeeCallback>();
    monitorTeeCallback->volume.store(static_cast<float>(monitorVolume));

    const auto error = monitorDeviceManager->initialise(
        0, 2, nullptr, false,
        juce::String(deviceName.toStdString()), nullptr);
    if (!error.isEmpty()) {
        qWarning() << "Failed to open monitor TEE device:" << deviceName
                   << "error:" << QString::fromStdString(error.toStdString());
        monitorTeeCallback.reset();
        return;
    }

    // Force the monitor device to run at the same sample rate as the main deck
    // device.  If the two devices run at different rates the FIFO will drift
    // continuously, causing stutter or silence on the monitor output.
    if (currentSampleRate > 0.0) {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        monitorDeviceManager->getAudioDeviceSetup(setup);
        if (setup.sampleRate != currentSampleRate) {
            setup.sampleRate = currentSampleRate;
            const auto setupError = monitorDeviceManager->setAudioDeviceSetup(setup, true);
            if (!setupError.isEmpty()) {
                qWarning() << "Could not match monitor TEE sample rate to main device:"
                           << QString::fromStdString(setupError.toStdString());
            }
        }
    }

    monitorDeviceManager->addAudioCallback(monitorTeeCallback.get());
}

void JuceAudioBackend::closeMonitorDevice()
{
    if (monitorDeviceManager && monitorTeeCallback) {
        monitorDeviceManager->removeAudioCallback(monitorTeeCallback.get());
        monitorDeviceManager->closeAudioDevice();
    }
    monitorTeeCallback.reset();
}

void JuceAudioBackend::setUseMonitorOutput(bool enabled)
{
    useMonitor = enabled;
}

bool JuceAudioBackend::useMonitorOutput()
{
    return useMonitor;
}

void JuceAudioBackend::setMonitorVolume(double volume)
{
    QMutexLocker locker(&stateGuard);
    monitorVolume = juce::jlimit(0.0, 2.0, volume);
    if (monitorTeeCallback)
        monitorTeeCallback->volume.store(static_cast<float>(monitorVolume));
}

int JuceAudioBackend::outputLatencyMs() const
{
    if (!deviceManager)
        return 0;

    auto* device = deviceManager->getCurrentAudioDevice();
    if (!device)
        return 0;

    const double sr = device->getCurrentSampleRate();
    if (sr <= 0.0)
        return 0;

    // Include device-reported output latency and one callback buffer of scheduling lead time.
    const int latencySamples = qMax(0, device->getOutputLatencyInSamples())
                             + qMax(0, device->getCurrentBufferSizeSamples());
    return qMax(0, qRound(1000.0 * static_cast<double>(latencySamples) / sr));
}

QString JuceAudioBackend::getLastError()
{
    return lastError;
}

void JuceAudioBackend::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                                        float* const* outputChannelData, int numOutputChannels,
                                                        int numSamples,
                                                        const juce::AudioIODeviceCallbackContext& context)
{
    (void) inputChannelData;
    (void) numInputChannels;
    (void) context;

    // Process audio through transport source
    juce::AudioBuffer<float> buffer(outputChannelData, numOutputChannels, numSamples);
    juce::AudioSourceChannelInfo info(&buffer, 0, numSamples);
    transportSource->getNextAudioBlock(info);

    // TEE: duplicate raw deck signal before deck fader/gain/EQ processing.
    if (useMonitor && monitorTeeCallback)
        monitorTeeCallback->push(buffer, numSamples);

    double volumeSnapshot = 1.0;
    double gainLinearSnapshot = 1.0;
    {
        QMutexLocker locker(&stateGuard);
        volumeSnapshot = masterVolume;
        gainLinearSnapshot = gainDb;
    }

    // Apply EQ + gain + volume in one processing pass.
    const double totalLinearGain = volumeSnapshot * gainLinearSnapshot;
    applyGainAndEQ(buffer, numSamples, totalLinearGain);

    // Metering on post-processing signal.
    meterAudio(buffer, numSamples, false);
    outputLevelLeft = levelLeft.load();
    outputLevelRight = levelRight.load();

}

void JuceAudioBackend::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    currentSampleRate = device->getCurrentSampleRate();
    transportSource->prepareToPlay(device->getCurrentBufferSizeSamples(), 
                                    device->getCurrentSampleRate());

    for (auto* filterGroup : { &lowEqFilters, &midEqFilters, &highEqFilters }) {
        for (auto& filter : *filterGroup)
            filter.reset();
    }
}

void JuceAudioBackend::audioDeviceStopped()
{
    transportSource->releaseResources();
}

double JuceAudioBackend::getMonitorLevelLeft() const
{
    return monitorLevelLeft;
}

double JuceAudioBackend::getMonitorLevelRight() const
{
    return monitorLevelRight;
}

void JuceAudioBackend::processAudioBlock(juce::AudioBuffer<float>& buffer, int numSamples, bool isMonitor)
{
    // Apply volume appropriate to the output path
    float volumeLinear = static_cast<float>(isMonitor ? monitorVolume : masterVolume);
    float gainLinear = static_cast<float>(gainDb);
    
    if (isMonitor)
        volumeLinear *= gainLinear;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        juce::FloatVectorOperations::multiply(buffer.getWritePointer(ch), volumeLinear, numSamples);
    }
}

void JuceAudioBackend::applyGainAndEQ(juce::AudioBuffer<float>& buffer, int numSamples, double volume)
{
    const float lowGain = dbToGain(eqBand0Gain.load());
    const float midGain = dbToGain(eqBand1Gain.load());
    const float highGain = dbToGain(eqBand2Gain.load());

    if (currentSampleRate > 0.0) {
        auto lowCoefficients = juce::IIRCoefficients::makeLowShelf(currentSampleRate, 180.0, 0.707, lowGain);
        auto midCoefficients = juce::IIRCoefficients::makePeakFilter(currentSampleRate, 1000.0, 0.707, midGain);
        auto highCoefficients = juce::IIRCoefficients::makeHighShelf(currentSampleRate, 5000.0, 0.707, highGain);

        const int channelCount = juce::jmin(buffer.getNumChannels(), 2);
        for (int channel = 0; channel < channelCount; ++channel) {
            lowEqFilters[static_cast<size_t>(channel)].setCoefficients(lowCoefficients);
            midEqFilters[static_cast<size_t>(channel)].setCoefficients(midCoefficients);
            highEqFilters[static_cast<size_t>(channel)].setCoefficients(highCoefficients);

            auto* samples = buffer.getWritePointer(channel);
            lowEqFilters[static_cast<size_t>(channel)].processSamples(samples, numSamples);
            midEqFilters[static_cast<size_t>(channel)].processSamples(samples, numSamples);
            highEqFilters[static_cast<size_t>(channel)].processSamples(samples, numSamples);
        }
    }

    float volumeLinear = static_cast<float>(volume);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        juce::FloatVectorOperations::multiply(buffer.getWritePointer(ch), volumeLinear, numSamples);
    }
}

void JuceAudioBackend::meterAudio(const juce::AudioBuffer<float>& buffer, int numSamples, bool isMonitor)
{
    if (buffer.getNumChannels() <= 0 || numSamples == 0)
        return;

    const float rmsL = computeRMS(buffer.getReadPointer(0), numSamples);
    const float rmsR = (buffer.getNumChannels() > 1)
        ? computeRMS(buffer.getReadPointer(1), numSamples)
        : rmsL;

    if (isMonitor) {
        monitorLevelLeft = rmsL;
        monitorLevelRight = rmsR;
    } else {
        levelLeft = rmsL;
        levelRight = rmsR;
    }
}
