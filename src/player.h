/*
    Copyright (C) 2011-2026 Mario Stephan <mstephan@shared-files.de>

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
#ifndef PLAYER_H
#define PLAYER_H

#include <QWidget>
#include <QtCore>
#include <memory>

#include "juce_audio_backend.h"
class JuceAudioBackend;

class Player : public QWidget {
    Q_OBJECT
public:
    Player(QWidget* parent = nullptr);
    ~Player();

    bool prepare();
    bool ready();
    bool canOpen(QString mime);
    void open(QUrl url);
    void play();
    void stop();
    void pause();
    bool close();
    void setPosition(QTime);
    QTime position();
    double volume();
    void setVolume(double);
    void setGain(double);
    void setEqualizer(QString, double);
    void setRate(double rate);
    double rate() const;
    bool supportsSmoothTempo() const;
    void setMonitorDeviceId(const QString& deviceId);
    void setUseMonitorOutput(bool enabled);
    bool useMonitorOutput() const;
    void setMonitorVolume(double v);
    int outputLatencyMs() const;
    void setDelayCompensation(int milliseconds) {
        audioBackend->setInterPlayerDelayCompensation(milliseconds);
        qDebug() << Q_FUNC_INFO << "Set inter-player delay compensation to" << milliseconds << "ms, backend value now:" << audioBackend->getInterPlayerDelayCompensation()<< " outputLatencyMs: "  << audioBackend->outputLatencyMs(); 
    }

    QTime length();
    bool isPlaying();
    bool mediaPlayable();
    bool isLoaded() const;
    QString lastError;

    double levelLeft();
    double levelRight();
    double levelOutLeft();
    double levelOutRight();

Q_SIGNALS:
    void finish();
    void error();
    void levelChanged();
    void positionChanged();
    void loadFinished();

private slots:
    void loadThreadFinished();

private:
    struct PlayerPrivate* p;
    std::unique_ptr<JuceAudioBackend> audioBackend;

    void asyncOpen(QUrl url);
    void cleanup();
    void applyOutputRouting();
};

#endif
