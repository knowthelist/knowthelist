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
#include "vumeter.h"

#include <QDragEnterEvent>

struct PlayerWidgetPrivate
{
    bool isEndAnnounced;
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
    p(new PlayerWidgetPrivate)
{
    ui->setupUi(this);

    p->isEndAnnounced = false;

    //create the player
    player = new Player(this);
    player->prepare();

    ui->butFwd->setIcon(QIcon(":forward.png"));
    ui->butRew->setIcon(QIcon(":backward.png"));
    ui->butPlay->setIcon(QIcon(":play.png"));
    ui->butPlay->setChecked(false);
    ui->butFwd->setIconSize(QSize(26, 26));
    ui->butRew->setIconSize(QSize(26, 26));
    ui->butPlay->setIconSize(QSize(26, 26));
    ui->butCue->setChecked(false);

   vuMeter = ui->vuMeter;
   vuMeter->setOrientation( Qt::Horizontal );
   vuMeter->LevelColorNormal.setRgb( 112,146,190 );
   vuMeter->LevelColorHigh.setRgb( 218,59,9 );
   vuMeter->LevelColorOff.setRgb( 31,45,65 );
   vuMeter->setSpacesBetweenSegments( 1 );
   vuMeter->setLinesPerSegment( 2 );
   vuMeter->setLinesPerPeak( 2 );
   vuMeter->setSpacesInSegments( 1 );
   vuMeter->setSpacesInPeak( 1 );

   timerLevel = new QTimer(this);
   connect( timerLevel, SIGNAL(timeout()), SLOT(timerLevel_timeOut()) );

   timerPosition = new QTimer(this);
   connect( timerPosition, SIGNAL(timeout()), SLOT(timerPosition_timeOut()) );

   connect(player, SIGNAL(finish()), this, SLOT(playerFinished()));
   connect(player, SIGNAL(error()), this, SLOT(playerError()));
   connect(player, SIGNAL(loadFinished()), this, SLOT(playerLoaded()));

   ui->lblTitle->setText( "" );
   ui->lblInfo->setText( "" );

   QFont font = ui->lblInfo->font();
   QFont fonttime = ui->lblTime->font();
#if defined(Q_OS_DARWIN)
   int newSize = font.pointSize()-4;
   fonttime.setPointSize(fonttime.pointSize()+2);
#else
   int newSize = font.pointSize()-1;
#endif
   font.setPointSize(newSize);
   ui->lblInfo->setFont(font);
   ui->lblTime->setFont(fonttime);
   ui->lblTimeRemain->setFont(fonttime);

   m_isStarted = false;
   setAcceptDrops( true );
   this->stop();

   trackanalyser = new TrackAnalyser(this);
   connect(trackanalyser, SIGNAL(finishGain()),this,SLOT(analyseGainFinished()));
}

PlayerWidget::~PlayerWidget()
{
    delete player;
    delete timerPosition;
    delete timerLevel;
    delete trackanalyser;
    trackanalyser = 0;
    delete p;
}


void PlayerWidget::setVolume(double volume)
{
  player->setVolume(volume);
}

void PlayerWidget::setGain(double gain)
{
  player->setGain(gain);
}

void PlayerWidget::setInfo(QPair<int,int> info)
{
    QString strTrack = (info.first > 1) ? tr("Tracks") : tr("Track");
    ui->lblInfo->setText(QString("%1 %2       %3 %4")
                         .arg(info.first)
                         .arg(strTrack)
                         .arg(Track::prettyTime( info.second))
                         .arg(tr("Hours")));
}

void PlayerWidget::setEqualizer(EqBand band, int value)
{
    //ranging from -24.0 to +12.0.
    player->setEqualizer("band"+QString::number(band), (value - 240) / 10.0);
}

void PlayerWidget::setPositionMarkers()
{
    if ( trackanalyser->finished()) {
        if (m_skipSilentEnd){
            qDebug() << Q_FUNC_INFO <<"endPosition:"<<trackanalyser->endPosition();
            qDebug() << Q_FUNC_INFO <<"length:"<<trackanalyser->length();
            remainCueTime=trackanalyser->endPosition().msecsTo(trackanalyser->length());
        }
        else
            remainCueTime = 0;

        ui->txtCue->setText("-" + QString::number(remainCueTime/1000));
    }

    if ( !m_isStarted && m_skipSilentBegin && trackanalyser->finished()) {
        player->setPosition(trackanalyser->startPosition());
        ui->butCue->setChecked(true);
    }
}

void PlayerWidget::play()
{
  m_isStarted = true;
  if (m_CurrentTrack){
    ui->butPlay->setIcon(QIcon(":pause.png"));
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
    ui->butPlay->setIcon(QIcon(":play.png"));
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
    ui->butPlay->setIcon(QIcon(":play.png"));
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

void PlayerWidget::analyseGainFinished()
{
    qDebug() << Q_FUNC_INFO <<":"<<objectName();
    // got gain factor -> emit
    if (trackanalyser->gainDB()!=TrackAnalyser::GAIN_INVALID){
        Q_EMIT gainChanged(trackanalyser->gainFactor());

    }
    if ( m_CurrentTrack ){
        setPositionMarkers();
        updateTimeAndPositionDisplay();
    }
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
     if ( sourceSite.left(4) == dropSite.left(4) || sourceSite.left(9) == "tracklist" )
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
        event->setDropAction(Qt::MoveAction);
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
    qDebug() << Q_FUNC_INFO << "url=" << file;
    loadTrack( new Track(file));
}

void PlayerWidget::loadTrack( Track *track)
{
    if ( track )
        qDebug() << Q_FUNC_INFO <<":"<<objectName()<< " track="<<track->url();

    m_CurrentTrack = track;

    if ( track != NULL ) {

      drawTitle();

      bool doPlay = m_isStarted;
      player->stop();

      QUrl url = track->url();
      player->open(url);

      trackanalyser->setMode(TrackAnalyser::STANDARD);
      trackanalyser->open(url);

      if ( doPlay )
          player->play();

    }
    else {
      if (player->lastError!="")
          ui->lblTitle->setText( player->lastError );
      else
          ui->lblTitle->setText( "no track" );

      ui->lblTime->setText("-:-");
      ui->lblTimeMs->setText(".-");
      ui->lblTimeRemain->setText("-:-");
      ui->lblTimeRemainMs->setText(".-");
      stop();
    }

    remainCueTime=0;
    ui->sliPosition->setValue( 0 );
    ui->txtCue->setText("-");
    ui->butCue->setChecked(false);
}

void PlayerWidget::resizeEvent( QResizeEvent* e )
{
    QWidget::resizeEvent(e);

    drawTitle();
}

void PlayerWidget::drawTitle()
{
    int width = ui->lblTitle->width() - 2;
    if ( width < 300)
        ui->lblTitle->setStyleSheet("* { font-size: 13pt; }");
    else if ( width < 400)
        ui->lblTitle->setStyleSheet("* { font-size: 14pt; }");
    else
        ui->lblTitle->setStyleSheet("* { font-size: 16pt; }");

    QFontMetrics metrix(ui->lblTitle->font());

    QString clippedText = tr("No track");
    if ( m_CurrentTrack )
        clippedText = metrix.elidedText(m_CurrentTrack->prettyTitle(), Qt::ElideRight, width);

    ui->lblTitle->setText( clippedText );
}

float PlayerWidget::currentLevelLeft()
{
     return player->levelOutLeft();
}

float PlayerWidget::currentLevelRight()
{
     return player->levelOutRight();
}

void PlayerWidget::updateTimeAndPositionDisplay(bool isPassive)
{

    QTime length = player->length();
    QTime curpos = player->position();
    QTime remain(0,0,0);
    long remainMs;

    //Some tracks deliver no length in state pause
    if ( length == QTime(0,0) && m_CurrentTrack)
       length = QTime(0,0,0).addSecs(m_CurrentTrack->length());

    remainMs=curpos.msecsTo(length);
    remain = QTime(0,0,0).addMSecs(remainMs);

    //qDebug()<<remainMs << " :" <<remain;

    ui->lblTime->setText(curpos.toString("mm:ss"));
    ui->lblTimeMs->setText("." + curpos.toString("zzz").left(1));
    ui->lblTimeRemain->setText("-" + remain.toString("mm:ss"));
    ui->lblTimeRemainMs->setText("." + remain.toString("zzz").left(1));

    //Signal end of track or media error
    //ToDo: better recognition of media error (play pressed but player is not running)

    if ( (remainMs-remainCueTime-mTrackFinishEmitTime <=0
                && 0 < remainMs )
                || m_isHanging )    {
        if (!p->isEndAnnounced ) {
            qDebug() << Q_FUNC_INFO <<":"<<objectName()<<" EMIT aboutFinished";
            qDebug() << Q_FUNC_INFO <<": curpos:"<< curpos;
            qDebug() << Q_FUNC_INFO <<": remainMs:"<< remainMs;
            qDebug() << Q_FUNC_INFO <<": remainCueTime:"<< remainCueTime;
            qDebug() << Q_FUNC_INFO <<": mTrackFinishEmitTime:"<< mTrackFinishEmitTime;
            qDebug() << Q_FUNC_INFO <<": m_isHanging:"<< m_isHanging;

            //send signals only once
            p->isEndAnnounced = true;
            Q_EMIT aboutFinished();
            Q_EMIT trackPlayed(m_CurrentTrack);
        }
    }
    else
        p->isEndAnnounced = false;

    //update position slider only if triggerd by timer
    if (isPassive) {
        if (length != QTime(0,0,0))
            ui->sliPosition->setValue(curpos.msecsTo(QTime(0,0,0)) * 1000 / length.msecsTo(QTime(0,0,0)));
        else
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
    uint length = -player->length().msecsTo(QTime(0,0,0));
    if (length != 0 && value > 0) {
        QTime pos = QTime(0, 0, 0);
        pos = pos.addMSecs(length * (value / 1000.0));
                qDebug()<<"pos:"<<pos;
        player->setPosition(pos);
    }
    updateTimeAndPositionDisplay(false);
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



