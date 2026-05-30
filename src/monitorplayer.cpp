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

#include "monitorplayer.h"
#include "monitor_audio_backend.h"

#include <QWidget>
#include <QMutexLocker>
#include <QtConcurrent/QtConcurrent>

namespace {
    constexpr bool kLogDebug = true;
}

struct MonitorPlayerPrivate {
    QFutureWatcher<void> watcher;
    QMutex mutex;
    bool isStarted = false;
    bool isLoaded = false;
    bool isDisabled = false;
    QString error;
    QString deviceName;
    QString deviceID;
    uint length = 0;
    uint position = 0;
    double rms_l = 0.0;
    double rms_r = 0.0;
};

MonitorPlayer::MonitorPlayer(QWidget* parent)
    : QWidget(parent)
    , p(new MonitorPlayerPrivate)
    , audioBackend(std::make_unique<MonitorAudioBackend>())
{
    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << "Creating MonitorPlayer instance";

    connect(&p->watcher, SIGNAL(finished()), this, SLOT(loadThreadFinished()));

    // Initialize device enumeration
    if (audioBackend) {
        audioBackend->readDevices();
        p->deviceID = audioBackend->getDefaultDeviceID();
        p->deviceName = audioBackend->getOutputDeviceName();
    }
}

MonitorPlayer::~MonitorPlayer()
{
    if (p->watcher.isRunning())
        p->watcher.waitForFinished();

    cleanup();
    delete p;
    p = nullptr;
}

bool MonitorPlayer::prepare()
{
    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << "START";

    if (!audioBackend) {
        p->error = "Audio backend not initialized";
        return false;
    }

    if (!audioBackend->initialize()) {
        p->error = audioBackend->getLastError();
        qWarning() << "Failed to initialize monitor backend:" << p->error;
        return false;
    }

    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << "END";

    return true;
}

bool MonitorPlayer::ready()
{
    return audioBackend != nullptr;
}

bool MonitorPlayer::canOpen(QString mime)
{
    Q_UNUSED(mime);
    return true;
}

void MonitorPlayer::open(QUrl url)
{
    if (p->isDisabled) {
        if (kLogDebug)
            qDebug() << Q_FUNC_INFO << "Skipping open (monitor disabled)";
        return;
    }

    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << ":" << parentWidget()->objectName() << "url=" << url;

    QFuture<void> future = QtConcurrent::run([this, url]() { asyncOpen(url); });
    p->watcher.setFuture(future);
}

void MonitorPlayer::asyncOpen(QUrl url)
{
    QMutexLocker locker(&p->mutex);

    if (!audioBackend) {
        p->error = "Audio backend not initialized";
        emit loadFinished();
        return;
    }

    p->length = 0;
    p->position = 0;
    p->isLoaded = false;
    p->error = "";

    audioBackend->stop();
    audioBackend->load(url);

    if (!audioBackend->isLoaded()) {
        p->error = audioBackend->getLastError();
        qWarning() << "Failed to load monitor track:" << p->error;
        emit loadFinished();
        return;
    }

    // Get duration
    QTime duration = audioBackend->getDuration();
    p->length = QTime(0, 0).msecsTo(duration);
    p->isLoaded = true;

    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << "Monitor loaded, duration:" << p->length << "ms";

    emit loadFinished();
}

void MonitorPlayer::play()
{
    if (!p->isDisabled) {
        p->isStarted = true;
    }

    if (!p->isDisabled && p->isLoaded && audioBackend) {
        audioBackend->play();
        if (kLogDebug)
            qDebug() << Q_FUNC_INFO << "Monitor playback started";
    }
}

void MonitorPlayer::stop()
{
    p->isStarted = false;
    if (audioBackend) {
        audioBackend->stop();
    }
}

void MonitorPlayer::pause()
{
    if (isPlaying()) {
        p->isStarted = false;
        if (audioBackend) {
            audioBackend->pause();
        }
    }
}

bool MonitorPlayer::close()
{
    if (audioBackend) {
        audioBackend->stop();
    }
    return true;
}

void MonitorPlayer::setPosition(QTime position)
{
    if (audioBackend) {
        audioBackend->seek(position);
        p->position = QTime(0, 0).msecsTo(position);
    }
}

QTime MonitorPlayer::position()
{
    if (audioBackend) {
        return audioBackend->getPosition();
    }
    return QTime(0, 0).addMSecs(p->position);
}

double MonitorPlayer::volume()
{
    if (audioBackend)
        return audioBackend->getVolume();
    return 1.0;
}

void MonitorPlayer::setVolume(double v)
{
    if (audioBackend) {
        audioBackend->setVolume(v);
    }
}

void MonitorPlayer::disable()
{
    if (audioBackend) {
        audioBackend->setEnabled(false);
    }
    p->isDisabled = true;
    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << "Monitor disabled";
}

void MonitorPlayer::enable()
{
    p->isDisabled = false;
    if (audioBackend) {
        audioBackend->setEnabled(true);
    }
    if (kLogDebug)
        qDebug() << Q_FUNC_INFO << "Monitor enabled";
}

bool MonitorPlayer::isDisabled()
{
    return p->isDisabled;
}

QTime MonitorPlayer::length()
{
    return QTime(0, 0).addMSecs(p->length);
}

bool MonitorPlayer::isPlaying()
{
    if (audioBackend)
        return audioBackend->isPlaying();
    return false;
}

bool MonitorPlayer::mediaPlayable()
{
    return p->isLoaded;
}

QStringList MonitorPlayer::outputDevices()
{
    if (audioBackend)
        return audioBackend->getOutputDevices();
    return QStringList();
}

QString MonitorPlayer::outputDeviceName()
{
    if (audioBackend)
        return audioBackend->getOutputDeviceName();
    return QString();
}

QString MonitorPlayer::outputDeviceID()
{
    if (audioBackend)
        return audioBackend->getOutputDeviceID();
    return QString();
}

void MonitorPlayer::setOutputDevice(QString deviceName)
{
    if (audioBackend) {
        audioBackend->setOutputDevice(deviceName);
        p->deviceName = audioBackend->getOutputDeviceName();
        p->deviceID = audioBackend->getOutputDeviceID();
        if (kLogDebug)
            qDebug() << Q_FUNC_INFO << "Set monitor device to:" << p->deviceName;
    }
}

void MonitorPlayer::readDevices()
{
    if (audioBackend) {
        audioBackend->readDevices();
    }
}

QString MonitorPlayer::defaultDeviceID()
{
    if (audioBackend)
        return audioBackend->getDefaultDeviceID();
    return QString();
}

double MonitorPlayer::levelLeft()
{
    if (audioBackend)
        return audioBackend->getLevelLeft();
    return 0.0;
}

double MonitorPlayer::levelRight()
{
    if (audioBackend)
        return audioBackend->getLevelRight();
    return 0.0;
}

void MonitorPlayer::cleanup()
{
    if (audioBackend) {
        audioBackend->stop();
    }
}

void MonitorPlayer::loadThreadFinished()
{
    if (p->isLoaded && p->isStarted) {
        play();
    }
}
