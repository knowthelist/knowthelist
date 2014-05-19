/*
    Copyright (C) 2005-2014 Mario Stephan <mstephan@shared-files.de>

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

#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QtGui/QWidget>
#include <QtGui/QStyle>
#include <QtCore/QTimer>


#include "player.h"
#include "vumeter.h"

#include "playlistitem.h"
#include "trackanalyser.h"

namespace Ui {
    class PlayerWidget;
}

class PlayerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlayerWidget(QWidget *parent = 0);
    ~PlayerWidget();

    enum EqBand  { EQ_Low = 0,
                   EQ_Mid = 1,
                   EQ_High = 2
                   };

    float currentLevelLeft();
    float currentLevelRight();
    void loadFile(QUrl);

    void play();
    void stop();
    void pause();
    bool isStarted(){ return m_isStarted;}

    void setTrackFinishEmitTime( const int sec );
    int TrackFinishEmitTime() { return mTrackFinishEmitTime;}
    void setVolume(double volume);
    void setGain(double gain);
    void setSkipSilentEnd(bool checked) {m_skipSilentEnd=checked; setPositionMarkers();}
    void setSkipSilentBegin(bool checked) {m_skipSilentBegin=checked; setPositionMarkers();}

 public Q_SLOTS:
    void loadTrack( Track* );
    void analyseFinished();
    void setEqualizer(EqBand, int);

 Q_SIGNALS:
    void trackFinished();
    void aboutFinished();
    void trackDropped( Track* );
    void forwardPressed();
    void rewindPressed();
    void statusChanged(bool);
    void gainChanged(double);
    void levelChanged(double, double);

private Q_SLOTS:

    void on_butCue_clicked();
    void on_sliPosition_actionTriggered(int action);
    void updateTimeAndPositionDisplay();
    void playerFinished();
    void playerError();
    void playerLoaded();
    void timerLevel_timeOut();
    void timerPosition_timeOut();
    void on_sliPosition_sliderMoved(int);

    void on_butPlay_clicked();

    void on_butRew_clicked();

    void on_butFwd_clicked();


protected:
    //void mouseMoveEvent(QMouseEvent *event);
    VUMeter* vuMeter;

    long songTime;

private:
    //ToDo: move privates to struct Private
    Ui::PlayerWidget *ui;
    QToolButton *initButton(QStyle::StandardPixmap icon, const QString & tip,
                            QObject *dstobj, const char *slot_method, QLayout *layout);

    void createUI(QBoxLayout *appLayout);


    Player *player;
    TrackAnalyser *trackanalyser;
    float m_level;
    QLabel *m_positionLabel;
    QLabel *m_volumeLabel;

    QTimer* timerLevel;
    QTimer* timerPosition;
    void dropEvent( QDropEvent* );
    void dragEnterEvent(QDragEnterEvent*);
    void setPositionMarkers();
    int mTrackFinishEmitTime;
    Track* m_CurrentTrack;
    long remainCueTime;
    bool m_isStarted;
    bool m_isHanging;
    bool m_skipSilentEnd;
    bool m_skipSilentBegin;



    struct Private;
    Private * p;

};

#endif // PLAYERWIDGET_H




