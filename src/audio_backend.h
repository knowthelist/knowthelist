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

#ifndef AUDIO_BACKEND_H
#define AUDIO_BACKEND_H

#include <QString>
#include <QUrl>
#include <QTime>
#include <memory>

/**
 * Abstract audio backend interface.
 * Allows swapping between GStreamer (legacy) and JUCE implementations.
 */
class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    /**
     * Initialize the backend.
     * @return true if successful
     */
    virtual bool initialize() = 0;

    /**
     * Load a track from URL.
     * @param url File URL to load
     */
    virtual void load(const QUrl& url) = 0;

    /**
     * Play the loaded track.
     */
    virtual void play() = 0;

    /**
     * Pause playback.
     */
    virtual void pause() = 0;

    /**
     * Stop playback and reset.
     */
    virtual void stop() = 0;

    /**
     * Seek to a position.
     * @param position Target position (QTime(0,0) + milliseconds)
     */
    virtual void seek(const QTime& position) = 0;

    /**
     * Get current playback position.
     * @return Position as QTime
     */
    virtual QTime getPosition() = 0;

    /**
     * Get track duration.
     * @return Duration as QTime
     */
    virtual QTime getDuration() = 0;

    /**
     * Check if track is playing.
     * @return true if playing
     */
    virtual bool isPlaying() = 0;

    /**
     * Check if track is loaded.
     * @return true if loaded
     */
    virtual bool isLoaded() = 0;

    /**
     * Set playback rate (tempo).
     * @param rate Rate multiplier (1.0 = normal, 0.5 = half, 2.0 = double)
     */
    virtual void setRate(double rate) = 0;

    /**
     * Get playback rate.
     * @return Current rate
     */
    virtual double getRate() = 0;

    /**
     * Set master volume.
     * @param volume Volume level (0.0 to 1.0+)
     */
    virtual void setVolume(double volume) = 0;

    /**
     * Get master volume.
     * @return Current volume
     */
    virtual double getVolume() = 0;

    /**
     * Set gain/amplify.
     * @param gain Gain in dB or linear multiplier
     */
    virtual void setGain(double gain) = 0;

    /**
     * Set equalizer band.
     * @param band Band name (e.g., "band0", "band1", "band2")
     * @param gain Gain value
     */
    virtual void setEqualizer(const QString& band, double gain) = 0;

    /**
     * Get left channel RMS level.
     * @return Level (0.0 to 1.0+)
     */
    virtual double getLevelLeft() = 0;

    /**
     * Get right channel RMS level.
     * @return Level (0.0 to 1.0+)
     */
    virtual double getLevelRight() = 0;

    /**
     * Get output left channel level (post-fader).
     * @return Level (0.0 to 1.0+)
     */
    virtual double getOutputLevelLeft() = 0;

    /**
     * Get output right channel level (post-fader).
     * @return Level (0.0 to 1.0+)
     */
    virtual double getOutputLevelRight() = 0;

    /**
     * Check if tempo can be adjusted smoothly without seeks.
     * @return true if smooth tempo supported
     */
    virtual bool supportsSmoothTempo() = 0;

    /**
     * Set monitor device ID.
     * @param deviceId Device identifier
     */
    virtual void setMonitorDeviceId(const QString& deviceId) = 0;

    /**
     * Enable/disable monitor output routing.
     * @param enabled true to route to monitor output
     */
    virtual void setUseMonitorOutput(bool enabled) = 0;

    /**
     * Check if monitor output is in use.
     * @return true if routing to monitor
     */
    virtual bool useMonitorOutput() = 0;

    /**
     * Set monitor output volume.
     * @param volume Volume level (0.0 to 1.0+)
     */
    virtual void setMonitorVolume(double volume) = 0;

    /**
     * Get last error message.
     * @return Error string, empty if no error
     */
    virtual QString getLastError() = 0;
};

#endif // AUDIO_BACKEND_H
