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

#ifndef JUCE_AUDIO_BACKEND_H
#define JUCE_AUDIO_BACKEND_H

#include "audio_backend.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <memory>
#include <atomic>
#include <QMutex>

/**
 * JUCE-based audio backend implementation.
 * Pure JUCE-based audio backend for playback, routing, and level metering.
 * 
 * Supports:
 * - Main deck playback with seek, tempo, EQ, gain, volume
 * - Monitor output on separate device (pre-fader listen / headphone mix)
 * - Independent monitor volume
 * - Level metering for both main and monitor outputs
 * - Smooth tempo changes (if supported by tempo effect element)
 */
class JuceAudioBackend : public AudioBackend,
                         private juce::AudioIODeviceCallback {
public:
    JuceAudioBackend();
    ~JuceAudioBackend() override;

    bool initialize() override;
    void load(const QUrl& url) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(const QTime& position) override;
    QTime getPosition() override;
    QTime getDuration() override;
    bool isPlaying() override;
    bool isLoaded() override;
    void setRate(double rate) override;
    double getRate() override;
    void setVolume(double volume) override;
    double getVolume() override;
    void setGain(double gain) override;
    void setEqualizer(const QString& band, double gain) override;
    double getLevelLeft() override;
    double getLevelRight() override;
    double getOutputLevelLeft() override;
    double getOutputLevelRight() override;
    bool supportsSmoothTempo() override;
    void setMonitorDeviceId(const QString& deviceId) override;
    void setUseMonitorOutput(bool enabled) override;
    bool useMonitorOutput() override;
    void setMonitorVolume(double volume) override;
    int outputLatencyMs() const override;
    QString getLastError() override;

    // Monitor output level metering
    double getMonitorLevelLeft() const;
    double getMonitorLevelRight() const;
    
    // Added method for inter-player delay compensation
    void setInterPlayerDelayCompensation(int delayMs) { m_interPlayerDelayCompensation = delayMs; }
    int getInterPlayerDelayCompensation() const { return m_interPlayerDelayCompensation; }

    // Inter-player delay compensation
    int m_interPlayerDelayCompensation{0};

private:
    // JUCE audio callback (main output)
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // Audio processing helpers
    void processAudioBlock(juce::AudioBuffer<float>& buffer, int numSamples, bool isMonitor);
    void applyGainAndEQ(juce::AudioBuffer<float>& buffer, int numSamples, double volume);
    void meterAudio(const juce::AudioBuffer<float>& buffer, int numSamples, bool isMonitor);

    // Main audio device management
    std::unique_ptr<juce::AudioDeviceManager> deviceManager;
    std::unique_ptr<juce::AudioFormatManager> formatManager;
    std::unique_ptr<juce::AudioTransportSource> transportSource;
    std::unique_ptr<juce::AudioFormatReaderSource> currentSource;
    class TempoSource;
    std::unique_ptr<TempoSource> tempoSource;

    // Monitor output device (independent)
    std::unique_ptr<juce::AudioDeviceManager> monitorDeviceManager;
    std::unique_ptr<juce::AudioIODevice> monitorDevice;

    // EQ - simple 3-band equalizer via peaking filters
    std::atomic<double> eqBand0Gain{0.0};  // Low
    std::atomic<double> eqBand1Gain{0.0};  // Mid
    std::atomic<double> eqBand2Gain{0.0};  // High
    std::array<juce::IIRFilter, 2> lowEqFilters;
    std::array<juce::IIRFilter, 2> midEqFilters;
    std::array<juce::IIRFilter, 2> highEqFilters;
    double currentSampleRate = 44100.0;

    // Playback state
    double currentRate = 1.0;
    double sourceSampleRate = 0.0;
    double masterVolume = 1.0;
    double gainDb = 0.0;
    
    // Metering (main output)
    std::atomic<double> levelLeft{0.0};
    std::atomic<double> levelRight{0.0};
    std::atomic<double> outputLevelLeft{0.0};
    std::atomic<double> outputLevelRight{0.0};
    std::atomic<int> seekMuteSamples{0};

    // Metering (monitor output)
    std::atomic<double> monitorLevelLeft{0.0};
    std::atomic<double> monitorLevelRight{0.0};

    // Monitor configuration
    QString lastError;
    bool useMonitor = false;
    QString monitorDeviceId;
    double monitorVolume = 1.0;

    // Thread safety
    mutable QMutex stateGuard;

    // Monitor TEE: routes a copy of the deck audio to a second output device
    // when the MON button is enabled. The MonitorTeeCallback owns the lock-free
    // FIFO; the main callback pushes into it and the monitor device pulls from it.
    class MonitorTeeCallback;
    std::unique_ptr<MonitorTeeCallback> monitorTeeCallback;
    void openMonitorDevice(const QString& deviceName);
    void closeMonitorDevice();
};

#endif // JUCE_AUDIO_BACKEND_H
