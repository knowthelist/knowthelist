/*
   Copyright (C) 2011 Mario Stephan <mstephan@shared-files.de>

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

#include "playerwidget.h"
#include "ui_playerwidget.h"
#include "player.h"
#include "trackanalyser.h"

#include <QtGui/QBoxLayout>
#include <QtGui/QFileDialog>
#include <QtGui/QToolButton>
#include <QtGui/QLabel>
#include <QtGui/QSlider>
#include <QtGui/QMouseEvent>

#include <qdebug.h>


#include <qlabel.h>
#include "vumeter.h"
#include <qpushbutton.h>
#include <qevent.h>
//#include <qurldrag.h>
//#include <qiconloader.h>
#include <qled.h>
#include <qwaitcondition.h>
#include <qmutex.h>

struct PlayerWidget::Private
{

};

PlayerWidget::PlayerWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PlayerWidget),
    songTime(0),
    mTrackFinishEmitTime(12000),
    m_CurrentTrack(0),
    remainCueTime(0),
    m_isStarted(false),
    m_isHanging(false),
    p(new Private)
{
    ui->setupUi(this);

    qDebug() << __FUNCTION__ << "(BEGIN) constr "<< objectName();

    //create the player
    player = new Player(this);
    player->prepare();

    ui->butFwd->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
    ui->butRew->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    ui->butPlay->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    ui->butPlay->setChecked(false);
    ui->butFwd->setIconSize(QSize(26, 26));
    ui->butRew->setIconSize(QSize(26, 26));
    ui->butPlay->setIconSize(QSize(26, 26));
    ui->butCue->setChecked(false);

   vuMeter = new VUMeter(this);
   vuMeter->setGeometry(ui->txtVUMeter->geometry());
   vuMeter->setOrientation( Qt::Horizontal );
   vuMeter->LevelColorNormal.setRgb( 112,146,190 );
   vuMeter->LevelColorHigh.setRgb( 218,59,9 );
   vuMeter->LevelColorOff.setRgb( 31,45,65 );
   //vuMeter->setBackgroundColor( QColor(33,24,41) );
   vuMeter->setSpacesBetweenSecments( 1 );
   vuMeter->setLinesPerSecment( 2 );
   vuMeter->setLinesPerPeak( 2 );
   vuMeter->setSpacesInSecments( 1 );
   vuMeter->setSpacesInPeak( 1 );

   timerLevel = new QTimer(this);
   connect( timerLevel, SIGNAL(timeout()), SLOT(timerLevel_timeOut()) );

   timerPosition = new QTimer(this);
   connect( timerPosition, SIGNAL(timeout()), SLOT(timerPosition_timeOut()) );

   connect(player, SIGNAL(finish()), this, SLOT(playerFinished()));
   connect(player, SIGNAL(error()), this, SLOT(playerError()));
   connect(player, SIGNAL(loadFinished()), this, SLOT(playerLoaded()));

   ui->lblTitle->setText( "" );

   m_isStarted = false;
   setAcceptDrops( true );
   this->stop();
   //ui->txtCue->hide();

   trackanalyser = new TrackAnalyser(this);
   connect(trackanalyser, SIGNAL(finish()),this,SLOT(analyseFinished()));

}

PlayerWidget::~PlayerWidget()
{
    delete player;
    delete timerPosition;
    delete timerLevel;
    delete trackanalyser;
}


void PlayerWidget::setVolume(double volume)
{
  player->setVolume(volume);
}

void PlayerWidget::setGain(double gain)
{
  player->setGain(gain);
}

void PlayerWidget::setEqualizer(EqBand band, int value)
{
    //ranging from -24.0 to +12.0.
    player->setEqualizer("band"+QString::number(band), value / 10.0);
}

void PlayerWidget::setPositionMarkers()
{
    if (m_skipSilentEnd)
        remainCueTime=trackanalyser->endPosition().msecsTo(trackanalyser->length());
    else
        remainCueTime = 0;

    ui->txtCue->setText("-" + QString::number(remainCueTime/1000));

    if ( !m_isStarted && m_skipSilentBegin && trackanalyser->finished()) {
        player->setPosition(trackanalyser->startPosition());
        ui->butCue->setChecked(true);
    }
}

void PlayerWidget::play()
{
  m_isStarted = true;
  if (m_CurrentTrack){
    ui->butPlay->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    ui->butPlay->setChecked(true);
    player->play();
    ui->butCue->setChecked(false);
    timerLevel->start( 50 );
    timerPosition->start( 100 );
    Q_EMIT statusChanged(m_isStarted);
  }
  else
      m_isHanging=true;
}

void PlayerWidget::pause()
{
    ui->butPlay->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    ui->butPlay->setChecked(false);
    m_isStarted = false;
    player->pause();
    timerLevel->stop();
    timerPosition->stop();
    vuMeter->reset();
    Q_EMIT statusChanged(m_isStarted);
    Q_EMIT levelChanged(0,0);
}

void PlayerWidget::stop()
{
    ui->butPlay->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    ui->butPlay->setChecked(false);
    m_isStarted = false;
    m_isHanging = false;
    player->stop();
    timerLevel->stop();
    timerPosition->stop();
    vuMeter->reset();
    Q_EMIT statusChanged(m_isStarted);
    Q_EMIT levelChanged(0,0);
}

void PlayerWidget::on_butPlay_clicked()
{
    if ( m_isStarted ) {
      this->pause();
    }
    else {
      this->play();
    }
}

void PlayerWidget::analyseFinished()
{
    qDebug() << __PRETTY_FUNCTION__ <<":"<<objectName();
    // got gain factor -> emit
    if (trackanalyser->gainDB()!=TrackAnalyser::GAIN_INVALID){
        Q_EMIT gainChanged(trackanalyser->gainFactor());
    }

    setPositionMarkers();
    updateTimeAndPositionDisplay();

}

void PlayerWidget::timerLevel_timeOut()
{
      vuMeter->setValueLeft(player->levelLeft());
      vuMeter->setValueRight(player->levelRight());
      Q_EMIT levelChanged(player->levelOutLeft(),player->levelOutRight());
}

void PlayerWidget::timerPosition_timeOut()
{
      updateTimeAndPositionDisplay();
}

void PlayerWidget::dragEnterEvent(QDragEnterEvent* event)
{
    //ToDo: remove forein classname tracklist"
    //qDebug() << "PlayerWidget: dragEnterEvent: "<<event->source()->objectName()  ;
    if ( !event->source() ) return;
    event->setDropAction(Qt::CopyAction);
     QString sourceSite = event->source()->objectName();
     QString dropSite = this->objectName();
     if ( sourceSite.left(4) == dropSite.left(4) || sourceSite == "tracklist" )
        event->accept();
}


void PlayerWidget::dropEvent( QDropEvent *event )
{
    if (event->mimeData()->hasUrls()) {
            QList<QUrl> urlList = event->mimeData()->urls(); // returns list of QUrls
            event->ignore();

            if ( urlList.size() > 0) // if at least one QUrl is present in list
            {
                //load first
                loadFile(urlList.at(1));
            }
     }
    else if (event->mimeData()->hasFormat("text/playlistitem")) {

        //decode playlistitem
        QByteArray itemData = event->mimeData()->data("text/playlistitem");
        QDataStream stream(&itemData, QIODevice::ReadOnly);
        QVector<QStringList> tags;

        stream >> tags;
        event->setDropAction(Qt::CopyAction);
        event->accept();

        //publish dropped Tracks to connected playlist
        foreach ( QStringList tag, tags) {
            Track *track = new Track(tag);
            Q_EMIT trackDropped( track );
        }

    }
    else
       event->ignore();
}

void PlayerWidget::loadFile( QUrl file)
{
    qDebug() << __FUNCTION__ << "url=" << file;
    loadTrack( new Track(file));
}

void PlayerWidget::loadTrack( Track *track)

{

    if ( track )
        qDebug() << __PRETTY_FUNCTION__ <<":"<<objectName()<< " track="<<track->url();

    m_CurrentTrack = track;

    if ( track != NULL ) {

      ui->lblTitle->setText( track->artist() + " - " + track->title() );

      bool doPlay = m_isStarted;
      player->stop();

      QUrl url = track->url();
      player->open(url);

      trackanalyser->open(url);

      if ( doPlay )
          player->play();

    }
    else {

      ui->lblTitle->setText( "no track" );
      //ToDo: doesn't work - avoid new override afterwards
      ui->lblTime->setText("-:-");
      ui->lblTimeMs->setText(".-");
      ui->lblTimeRemain->setText("-:-");
      ui->lblTimeRemainMs->setText(".-");
      stop();
    }

    remainCueTime=0;
    ui->sliPosition->setValue( 0 );
    ui->txtCue->setText("?");

}

float PlayerWidget::currentLevelLeft()
{
     return player->levelOutLeft();
}

float PlayerWidget::currentLevelRight()
{
     return player->levelOutRight();
}

void PlayerWidget::updateTimeAndPositionDisplay()
{

    QTime length = player->length();
    QTime curpos = player->position();
    QTime remain(0,0);
    long remainMs;

    remainMs=curpos.msecsTo(length);
    remain = QTime(0,0).addMSecs(remainMs);

    //qDebug()<<remainMs << " :" <<remain;

    ui->lblTime->setText(curpos.toString("mm:ss"));
    ui->lblTimeMs->setText("." + curpos.toString("zzz").left(1));
    ui->lblTimeRemain->setText("-" + remain.toString("mm:ss"));
    ui->lblTimeRemainMs->setText("." + remain.toString("zzz").left(1));

    //Signal end of track or media error
    //ToDo: better recocnition of media error (play pressed but player is not running)

    if ( (remainMs-remainCueTime-mTrackFinishEmitTime <=0
                && 0 < remainMs)
                || m_isHanging )    {
            qDebug() << __PRETTY_FUNCTION__ <<":"<<objectName()<<" EMIT aboutFinished";
            Q_EMIT aboutFinished();
    }

    //update position slider
    if (length != QTime(0,0)) {
        ui->sliPosition->setValue(curpos.msecsTo(QTime()) * 1000 / length.msecsTo(QTime()));
    } else {
        ui->sliPosition->setValue(0);
    }

}

void PlayerWidget::playerError()
{
    Q_EMIT trackFinished();
}

void PlayerWidget::playerFinished()
{
    Q_EMIT trackFinished();
}

void PlayerWidget::playerLoaded()
{
    updateTimeAndPositionDisplay();
}

void PlayerWidget::on_butRew_clicked()
{
    if (player->position()<QTime(0,0,3))
        Q_EMIT rewindPressed();
    else
        player->setPosition(QTime(0,0,0));
}

void PlayerWidget::on_butFwd_clicked()
{
    Q_EMIT forwardPressed();
}

void PlayerWidget::setTrackFinishEmitTime( const int sec )
{
    if ( sec >= 0 && sec < 60 )
        mTrackFinishEmitTime = sec*1000;
}


void PlayerWidget::on_sliPosition_sliderMoved(int value)
{
    uint length = -player->length().msecsTo(QTime());
    if (length != 0 && value > 0) {
        QTime pos;
        pos = pos.addMSecs(length * (value / 1000.0));
                qDebug()<<"pos:"<<pos;
        player->setPosition(pos);
    }
    updateTimeAndPositionDisplay();
}

void PlayerWidget::on_sliPosition_actionTriggered(int action)
{
    //a workaround for page moving
    int posi;
    switch (action)
    {
        case 3:
            posi=ui->sliPosition->value()+100;
            break;
        case 4:
            posi=ui->sliPosition->value()-100;
            if (posi<100)
                posi=1;
            break;
        case 1:
            posi=ui->sliPosition->value()+10;
            break;
        case 2:
            posi=ui->sliPosition->value()-10;
            break;
        default:
            return;
            break;
    }

    this->on_sliPosition_sliderMoved(posi);
    ui->butCue->setChecked(false);
}

void PlayerWidget::on_butCue_clicked()
{
    //ToDo: Visualize skipped silent at start and at the end (color bar)
    this->pause();
    player->setPosition(trackanalyser->startPosition());
    updateTimeAndPositionDisplay();
}



