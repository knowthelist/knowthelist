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
#include "juce_audio_backend.h"

#include <QWidget>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace {
    constexpr bool kLogDebug = true;
}

struct PlayerPrivate {
    QFutureWatcher<void> watcher;
    QMutex mutex;
    bool isStarted = false;
    bool isLoaded = false;
    QString error;
    int length = 0;
    int position = 0;
        int playBasePosition = 0;
    QElapsedTimer playTimer;
        QElapsedTimer seekTimer;
    double volume = 1.0;
    double rate = 1.0;
    double rms_l = 0.0;
    double rms_r = 0.0;
    double rmsout_l = 0.0;
    double rmsout_r = 0.0;
    QString monitorDeviceId;
    QString masterDeviceId;
    bool useMonitorOutput = false;
    double monitorVolume = 1.0;
    int lastQueriedPosition = -1;
};

Player::Player(QWidget* parent)
    : QWidget(parent)
    , p(new PlayerPrivate)
    , audioBackend(std::make_unique<JuceAudioBackend>())
{
    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << "Creating Player instance";

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

bool Player::prepare()
{
    qDebug() << Q_FUNC_INFO << "START";

    if (!audioBackend) {
        lastError = "Audio backend not initialized";
        return false;
    }

    if (!audioBackend->initialize()) {
        lastError = audioBackend->getLastError();
        qWarning() << "Failed to initialize audio backend:" << lastError;
        return false;
    }

    // Apply initial settings
    audioBackend->setVolume(p->volume);
    audioBackend->setGain(1.0);
    audioBackend->setRate(1.0);

    qDebug() << Q_FUNC_INFO << "END";
    return true;
}

bool Player::ready()
{
    return audioBackend != nullptr;
}

bool Player::canOpen(QString mime)
{
    // JUCE's format manager handles most common audio formats
    // Accept anything - let JUCE's reader determine support
    Q_UNUSED(mime);
    return true;
}

void Player::open(QUrl url)
{
    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName() << "url=" << url;

    QFuture<void> future = QtConcurrent::run([this, url]() { asyncOpen(url); });
    p->watcher.setFuture(future);
}

void Player::asyncOpen(QUrl url)
{
    QMutexLocker locker(&p->mutex);

    if (!audioBackend) {
        p->error = "Audio backend not initialized";
        lastError = p->error;
        emit loadFinished();
        return;
    }

    p->length = 0;
    p->position = 0;
    p->rate = 1.0;
    p->isLoaded = false;
    p->error = "";
    lastError = "";

    // Reset to start
    audioBackend->stop();
    audioBackend->setRate(1.0);

    // Load the file
    audioBackend->load(url);

    if (!audioBackend->isLoaded()) {
        p->error = audioBackend->getLastError();
        lastError = p->error;
        qWarning() << "Failed to load:" << lastError;
        emit loadFinished();
        return;
    }

    // Get duration
    QTime duration = audioBackend->getDuration();
    p->length = QTime(0, 0).msecsTo(duration);

    p->isLoaded = true;
    lastError = "";

    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << "Loaded successfully, duration:" << p->length << "ms";

    emit loadFinished();
}

void Player::play()
{
    p->isStarted = true;
    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName() << "isLoaded=" << p->isLoaded;

    if (p->isLoaded && audioBackend) {
        audioBackend->play();
        p->playBasePosition = p->position;
        p->playTimer.restart();
        p->lastQueriedPosition = -1;
    }
}

void Player::stop()
{
    p->isStarted = false;
    p->rate = 1.0;
    p->playBasePosition = 0;
    p->playTimer.invalidate();
    p->lastQueriedPosition = -1;

    if (audioBackend) {
        audioBackend->setRate(1.0);
        audioBackend->stop();
    }
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

        if (audioBackend) {
            audioBackend->pause();
        }
    }
}

bool Player::close()
{
    if (audioBackend) {
        audioBackend->stop();
    }
    return true;
}

void Player::setPosition(QTime position)
{
    if (!audioBackend)
        return;

    int timeMs = QTime(0, 0).msecsTo(position);
    timeMs = qBound(0, timeMs, p->length > 0 ? p->length : timeMs);
    position = QTime(0, 0).addMSecs(timeMs);

    audioBackend->seek(position);

    // Anchor position for timer-based fallback
    p->position = timeMs;
    p->playBasePosition = timeMs;
    p->seekTimer.restart();
    if (p->playTimer.isValid())
        p->playTimer.restart();
}

QTime Player::position()
{
    if (!audioBackend)
        return QTime(0, 0);

    // While paused, the backend may not have published a seek to its source
    // cursor yet. The Player state is authoritative until playback resumes.
    if (!p->isStarted || (p->seekTimer.isValid() && p->seekTimer.elapsed() < 250))
        return QTime(0, 0).addMSecs(p->position);

    // Always use backend clock so waveform cursor and heard audio stay aligned,
    // especially at non-1.0 tempo rates.
    QTime backendPos = audioBackend->getPosition();
    p->position = QTime(0, 0).msecsTo(backendPos);
    p->position = qBound(0, p->position, p->length > 0 ? p->length : p->position);

    return QTime(0, 0).addMSecs(p->position);
}

double Player::volume()
{
    return p->volume;
}

void Player::setVolume(double v)
{
    p->volume = v;
    if (audioBackend) {
        audioBackend->setVolume(v);
    }
}

void Player::setGain(double g)
{
    if (audioBackend) {
        audioBackend->setGain(g);
    }
}

void Player::setEqualizer(QString band, double gain)
{
    if (audioBackend) {
        audioBackend->setEqualizer(band, gain);
    }
}

void Player::setRate(double rate)
{
    p->rate = rate;
    if (audioBackend) {
        audioBackend->setRate(rate);
    }
}

double Player::rate() const
{
    return p->rate;
}

bool Player::supportsSmoothTempo() const
{
    if (audioBackend)
        return audioBackend->supportsSmoothTempo();
    return false;
}

void Player::setMonitorDeviceId(const QString& deviceId)
{
    p->monitorDeviceId = deviceId;
    if (audioBackend) {
        audioBackend->setMonitorDeviceId(deviceId);
    }
}

void Player::setUseMonitorOutput(bool enabled)
{
    p->useMonitorOutput = enabled;
    if (audioBackend) {
        audioBackend->setUseMonitorOutput(enabled);
    }
}

bool Player::useMonitorOutput() const
{
    return p->useMonitorOutput;
}

void Player::setMonitorVolume(double v)
{
    p->monitorVolume = v;
    if (audioBackend) {
        audioBackend->setMonitorVolume(v);
    }
}

int Player::outputLatencyMs() const
{
    if (audioBackend)
        return audioBackend->outputLatencyMs();
    return 0;
}

QTime Player::length()
{
    return QTime(0, 0).addMSecs(p->length);
}

bool Player::isPlaying()
{
    if (audioBackend)
        return audioBackend->isPlaying();
    return false;
}

bool Player::mediaPlayable()
{
    return p->isLoaded;
}

bool Player::isLoaded() const
{
    return p->isLoaded;
}

double Player::levelLeft()
{
    if (audioBackend)
        return audioBackend->getLevelLeft();
    return 0.0;
}

double Player::levelRight()
{
    if (audioBackend)
        return audioBackend->getLevelRight();
    return 0.0;
}

double Player::levelOutLeft()
{
    if (audioBackend)
        return audioBackend->getOutputLevelLeft();
    return 0.0;
}

double Player::levelOutRight()
{
    if (audioBackend)
        return audioBackend->getOutputLevelRight();
    return 0.0;
}

void Player::cleanup()
{
    if (audioBackend) {
        audioBackend->stop();
    }
}

void Player::loadThreadFinished()
{
    if (p->isLoaded && p->isStarted) {
        // Auto-play if requested during load
        play();
    }
}

void Player::applyOutputRouting()
{
    // Monitor routing is handled by JuceAudioBackend
    // This is kept for API compatibility but does nothing now
}
