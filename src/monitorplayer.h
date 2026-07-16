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

#ifndef MONITORPLAYER_H
#define MONITORPLAYER_H

#include <QtCore>
#include <QWidget>
#include <memory>

class MonitorAudioBackend;

class MonitorPlayer : public QWidget
{
    Q_OBJECT
public:
    MonitorPlayer(QWidget* parent = nullptr);
    ~MonitorPlayer();

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
    void disable();
    void enable();
    bool isDisabled();

    QTime length();
    bool isPlaying();
    bool mediaPlayable();
    QStringList outputDevices();
    QString outputDeviceName();
    QString outputDeviceID();
    void setOutputDevice(QString deviceName);
    void readDevices();
    QString defaultDeviceID();

    double levelLeft();
    double levelRight();

Q_SIGNALS:
    void finish();
    void error();
    void levelChanged();
    void positionChanged();
    void loadFinished();

private slots:
    void loadThreadFinished();

private:
    struct MonitorPlayerPrivate* p;
    std::unique_ptr<MonitorAudioBackend> audioBackend;

    void asyncOpen(QUrl url);
    void cleanup();
};

#endif // MONITORPLAYER_H




