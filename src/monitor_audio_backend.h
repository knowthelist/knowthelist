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

#ifndef MONITOR_AUDIO_BACKEND_H
#define MONITOR_AUDIO_BACKEND_H

#include "audio_backend.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <atomic>
#include <QStringList>

/**
 * Monitor output audio backend using JUCE.
 * Simplified backend for headphone/monitor output with device enumeration.
 * 
 * Features:
 * - Playback to specific audio device
 * - Device enumeration (macOS CoreAudio, Windows DirectSound, Linux ALSA)
 * - Enable/disable for quick muting
 * - Level metering
 * - Can be toggled on/off without reloading file
 */
class MonitorAudioBackend : public AudioBackend,
                            private juce::AudioIODeviceCallback {
public:
    MonitorAudioBackend();
    ~MonitorAudioBackend() override;

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
    void setGain(double gain) override { Q_UNUSED(gain); /* Not used for monitor */ }
    void setEqualizer(const QString& band, double gain) override { Q_UNUSED(band); Q_UNUSED(gain); /* Not used */ }
    double getLevelLeft() override;
    double getLevelRight() override;
    double getOutputLevelLeft() override { return getLevelLeft(); }
    double getOutputLevelRight() override { return getLevelRight(); }
    bool supportsSmoothTempo() override { return true; }
    void setMonitorDeviceId(const QString& deviceId) override;
    void setUseMonitorOutput(bool enabled) override;
    bool useMonitorOutput() override;
    void setMonitorVolume(double volume) override;
    QString getLastError() override;

    // Monitor-specific API
    QStringList getOutputDevices();
    QString getOutputDeviceID();
    QString getOutputDeviceName();
    QString getDefaultDeviceID();
    void setOutputDevice(const QString& deviceNameOrId);
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void readDevices();

private:
    // JUCE audio callback
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // Device enumeration (platform-specific implementation)
    QString getDefaultDeviceIDPlatform();
    void readDevicesPlatform();

    // Audio device management
    std::unique_ptr<juce::AudioDeviceManager> deviceManager;
    std::unique_ptr<juce::AudioFormatManager> formatManager;
    std::unique_ptr<juce::AudioTransportSource> transportSource;
    std::unique_ptr<juce::AudioFormatReaderSource> currentSource;

    // Device info
    QMap<QString, QString> deviceMap;  // deviceId -> deviceName
    QString currentDeviceId;
    QString currentDeviceName;

    // Playback state
    double currentVolume = 1.0;
    double currentRate = 1.0;
    bool enabledFlag = true;
    bool wasPlayingBeforeDisable = false;

    // Metering
    std::atomic<double> levelLeft{0.0};
    std::atomic<double> levelRight{0.0};

    // Error tracking
    QString lastError;
};

#endif // MONITOR_AUDIO_BACKEND_H
