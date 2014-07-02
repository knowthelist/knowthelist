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

#include "knowthelist.h"
#include "ui_knowthelist.h"
#include "playerwidget.h"
#include "qled.h"
#include "dj.h"
#include "djwidget.h"
#include "djfilterwidget.h"

#include <QtGui/QBoxLayout>
#include <QSettings>
#include <QtConcurrentRun>
#include <QMetaType>



Knowthelist::Knowthelist(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Knowthelist),
    monitorPlayer(0)
{
    ui->setupUi(this);

    //create the UI
    createUI();
}

Knowthelist::~Knowthelist()
{

        player1->stop();
        delete player1;
        player1 = 0;

        delete playList1;

        player2->stop();

        delete player2;
        player2 = 0;

        delete playList2;

        delete vuMeter1;
        delete vuMeter2;

        delete monitorPlayer;

        delete djSession;
        djSession=0;

        monitorPlayer=0;

        delete trackList;
        trackList=0;
        delete collectionBrowser;

        delete ui;
        qDebug() << "The end" << __FUNCTION__ ;
}

void Knowthelist::createUI()
{

    //hide place holders
    ui->phVU1->setVisible(false);
    ui->phVU2->setVisible(false);

    ui->slider1->setMinimum(0.0);
    ui->slider2->setMinimum(0.0);
    ui->slider1->setMaximum(100.0);
    ui->slider2->setMaximum(100.0);
    connect(ui->slider1,SIGNAL(valueChanged(int)),this,SLOT(slider1_valueChanged(int)));
    connect(ui->slider2,SIGNAL(valueChanged(int)),this,SLOT(slider2_valueChanged(int)));
    connect(ui->sliFader,SIGNAL(valueChanged(int)),this,SLOT(sliFader_valueChanged(int)));

    player1 = ui->player_L;
    player2 = ui->player_R;

    timerAutoFader = new QTimer(this);
    connect( timerAutoFader, SIGNAL(timeout()), SLOT(timerAutoFader_timerOut()) );

    vuMeter1 = new QVUMeter(ui->frameMixer);
    vuMeter2 = new QVUMeter(ui->frameMixer);

    vuMeter1->setGeometry(ui->phVU1->geometry());
    vuMeter2->setGeometry( ui->phVU2->geometry());

    timerMonitor = new QTimer(this);
    timerMonitor->setInterval(50);
    connect( timerMonitor, SIGNAL(timeout()), SLOT(timerMonitor_timeOut()) );

    timerGain1 = new QTimer(this);
    timerGain2 = new QTimer(this);
    timerGain1->setInterval(100);
    timerGain2->setInterval(100);
    connect( timerGain1, SIGNAL(timeout()), SLOT(timerGain1_timeOut()) );
    connect( timerGain2, SIGNAL(timeout()), SLOT(timerGain2_timeOut()) );

    qRegisterMetaType<QList<Track*> > ("QList<Track*>");


    //DJ
    djSession = new DjSession();

    playList1=ui->playlist_L;
    playList1->setIsCurrentList(true);


    playList2 = ui->playlist_R;
    playList2->setIsCurrentList(false);

    connect( playList1, SIGNAL(currentTrackChanged(Track*)),player1,SLOT(loadTrack(Track*)));
    connect( playList2, SIGNAL(currentTrackChanged(Track*)),player2,SLOT(loadTrack(Track*)));

    connect( player1, SIGNAL(forwardPressed()),playList1,SLOT( skipForward()));
    connect( player2, SIGNAL(forwardPressed()),playList2,SLOT( skipForward()));

    connect( player1, SIGNAL(rewindPressed()),playList1,SLOT(skipRewind()));
    connect( player2, SIGNAL(rewindPressed()),playList2,SLOT(skipRewind()));

    connect( player1, SIGNAL(aboutFinished()),SLOT( player_aboutTrackFinished()));
    connect( player2, SIGNAL(aboutFinished()),SLOT( player_aboutTrackFinished()));

    connect( player1, SIGNAL(gainChanged(double)),SLOT( player1_gainChanged(double)));
    connect( player2, SIGNAL(gainChanged(double)),SLOT( player2_gainChanged(double)));

    connect( player1, SIGNAL(levelChanged(double,double)),SLOT( player1_levelChanged(double,double)));
    connect( player2, SIGNAL(levelChanged(double,double)),SLOT( player2_levelChanged(double,double)));

    //ToDo: try to avoid this
    connect( player1, SIGNAL(statusChanged( bool )),playList1,SLOT(setPlaying( bool )));
    connect( player2, SIGNAL(statusChanged( bool )),playList2,SLOT(setPlaying( bool )));

    connect( player1, SIGNAL(trackFinished()),SLOT( player1_trackFinished()));
    connect( player2, SIGNAL(trackFinished()),SLOT( player2_trackFinished()));

    connect( player1, SIGNAL(trackPlayed(Track*)),djSession, SLOT(onTrackFinished(Track*)));
    connect( player2, SIGNAL(trackPlayed(Track*)),djSession, SLOT(onTrackFinished(Track*)));

    connect( player1, SIGNAL(trackDropped( Track* )),playList1,SLOT(addCurrentTrack(Track*)));
    connect( player2, SIGNAL(trackDropped( Track* )),playList2,SLOT(addCurrentTrack(Track*)));

    //alternateMax
    connect( playList1, SIGNAL(countChanged(int)),playList2, SLOT(setAlternateMax(int)));
    connect( playList2, SIGNAL(countChanged(int)),playList1, SLOT(setAlternateMax(int)));
    connect( playList1, SIGNAL(countChanged(QList<Track*>)),djSession, SLOT(onTracksChanged_Playlist1(QList<Track*>)));
    connect( playList2, SIGNAL(countChanged(QList<Track*>)),djSession, SLOT(onTracksChanged_Playlist2(QList<Track*>)));

    connect( djSession, SIGNAL(foundTracks_Playlist1(QList<Track*>)),playList1, SLOT(appendTracks(QList<Track*>)));
    connect( djSession, SIGNAL(foundTracks_Playlist2(QList<Track*>)),playList2, SLOT(appendTracks(QList<Track*>)));

    trackList = new Playlist();
    trackList->setObjectName("tracklist");
    trackList->setAcceptDrops( false );
    trackList->setPlaylistMode(Playlist::Tracklist);

    //ToDo: remove all of this
    //trackList->setMarkNextTrack( false );
    //trackList->setMarkCurrentTrack( false );

    collectionBrowser=new CollectionWidget(this);


    splitter= new QSplitter();
    splitter->addWidget(this->collectionBrowser);
    splitter->addWidget(trackList);
    QPixmap pixmap1(":database.png");
    ui->sideTab->AddTab(splitter,QIcon(pixmap1),tr("Collection"));

    connect( collectionBrowser, SIGNAL(selectionChanged(QList<Track*>)),trackList,SLOT(changeTracks(QList<Track*>)));
    connect( collectionBrowser,SIGNAL(setupDirs()),this,SLOT(showCollectionSetup()));
    connect( collectionBrowser,SIGNAL(wantLoad(QList<Track*>,QString)),this,SLOT(onWantLoad(QList<Track*>,QString)));

    connect( trackList, SIGNAL(wantSearch( QString )),collectionBrowser,SLOT(setFilterText(QString)));
    connect( playList1, SIGNAL(wantSearch( QString )),collectionBrowser,SLOT(setFilterText(QString)));
    connect( playList2, SIGNAL(wantSearch( QString )),collectionBrowser,SLOT(setFilterText(QString)));


   connect( trackList, SIGNAL(trackDoubleClicked(PlaylistItem*)),SLOT(Track_doubleClicked(PlaylistItem*)));
   connect( trackList, SIGNAL(wantLoad(PlaylistItem*,QString)),SLOT(trackList_wantLoad(PlaylistItem*, QString)));
   connect( trackList, SIGNAL(trackClicked(PlaylistItem*)),SLOT(Track_selectionChanged(PlaylistItem* )));
   connect( trackList, SIGNAL(trackChanged(PlaylistItem*)),SLOT(Track_selectionChanged(PlaylistItem* )));

   connect( playList1, SIGNAL(trackDoubleClicked(PlaylistItem*)),SLOT(Track_doubleClicked(PlaylistItem*)));
   connect( playList2, SIGNAL(trackDoubleClicked(PlaylistItem*)),SLOT(Track_doubleClicked(PlaylistItem*)));

   connect( playList1, SIGNAL(trackClicked(PlaylistItem*)),SLOT(Track_selectionChanged(PlaylistItem* )));
   connect( playList2, SIGNAL(trackClicked(PlaylistItem*)),SLOT(Track_selectionChanged(PlaylistItem* )));
   connect( playList1, SIGNAL(trackChanged(PlaylistItem*)),SLOT(Track_selectionChanged(PlaylistItem* )));
   connect( playList2, SIGNAL(trackChanged(PlaylistItem*)),SLOT(Track_selectionChanged(PlaylistItem* )));

    //AutoFade

    ui->ledFade->setLook(QLed::Flat);
    ui->ledFadeRight->setLook(QLed::Flat);
    ui->ledFadeLeft->setLook(QLed::Flat);
    ui->ledDJ->setLook(QLed::Flat);
    ui->ledAGC->setLook(QLed::Flat);
    ui->ledFadeRight->setShape(QLed::Rectangular);
    ui->ledFadeLeft->setShape(QLed::Rectangular);
    ui->ledFade->setShape(QLed::Rectangular);
    ui->ledDJ->setShape(QLed::Rectangular);
    ui->ledAGC->setShape(QLed::Rectangular);
    ui->ledFadeRight->setColor(QColor(35,119,246));
    ui->ledFadeLeft->setColor(QColor(35,119,246));
    ui->ledFade->setColor(QColor(35,119,246));
    ui->ledDJ->setColor(QColor(35,119,246));
    ui->ledAGC->setColor(QColor(35,119,246));
    ui->ledFade->off();
    ui->ledFadeRight->off();
    ui->ledFadeLeft->off();
    ui->ledAGC->off();
    ui->ledDJ->off();

    //MonitorPlayer
    initMonitorPlayer();

    //AutoDJ
    listDjFilters = new QListWidget();
    listDjs = new QListWidget();
    listDjFilters->setAttribute(Qt::WA_MacShowFocusRect, false);
    listDjs->setAttribute(Qt::WA_MacShowFocusRect, false);
    QWidget *djBox = new QWidget(this);
    QHBoxLayout *layout = new QHBoxLayout;
    layout->setMargin(0);
    layout->setSpacing(1);
    listDjs->setMaximumWidth(350);
    layout->addWidget(listDjs);
    layout->addWidget(listDjFilters);
    djBox->setLayout(layout);
    QPixmap pixmap2(":DJ.png");
    ui->sideTab->AddTab(djBox,QIcon(pixmap2),tr("AutoDJ"));
    ui->sideTab->setContextMenuPolicy(Qt::NoContextMenu);

    //Add the FileBrowser
    filetree = new FileBrowser(this);
    QPixmap pixmap3(":folder.png");
    ui->sideTab->AddTab(filetree,QIcon(pixmap3),tr("Folder"));

    //SettingsDialog
    preferences = new SettingsDialog(this);
    connect(preferences,SIGNAL(scanNowPressed()), collectionBrowser,SLOT(scan()));
    connect(preferences, SIGNAL(resetStatsPressed()), djSession, SLOT(onResetStats()));

    loadStartSettings();

    //ToDo: load from Settings
    ui->sideTab->SetCurrentIndex(0);
    ui->sideTab->SetMode(FancyTabWidget::Mode_LargeSidebar);

    //Collection ready?
    if ( !collectionBrowser->hasItems() )
    {
          this->show();
          showCollectionSetup();
    }


}

void Knowthelist::loadStartSettings()
{
    QSettings settings;

    ui->slider1->setValue( settings.value("Volume1","80").toDouble());
    ui->slider2->setValue( settings.value("Volume2","80").toDouble());
    ui->sliMonitorVolume->setValue( settings.value("VolumeMonitor").toDouble());
    ui->sliFader->setValue( 70 );
    changeVolumes();

    splitter->restoreState(settings.value("Splitter").toByteArray());

    restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
    restoreState(settings.value("mainWindowState").toByteArray());

    // Workaround to force correct geometry
    hide();
    show();

    if ( settings.value("loadPlaylists","true" )=="true")
    {
       playList1->loadXML( playList1->defaultPlaylistPath() );
       playList2->loadXML( playList2->defaultPlaylistPath() );
    }

    //AutoFade, AGC ...
    ui->toggleAutoFade->setChecked(settings.value("checkAutoFade",false).toBool());
    ui->toggleAGC->setChecked(settings.value("checkAGC",true).toBool());

    //EQ values
    ui->potHigh_1->setValue( settings.value("EQ_gains/High1").toInt() );
    ui->potMid_1->setValue( settings.value("EQ_gains/Mid1").toInt() );
    ui->potLow_1->setValue( settings.value("EQ_gains/Low1").toInt() );
    ui->potHigh_2->setValue( settings.value("EQ_gains/High2").toInt() );
    ui->potMid_2->setValue( settings.value("EQ_gains/Mid2").toInt() );
    ui->potLow_2->setValue( settings.value("EQ_gains/Low2").toInt() );

    loadCurrentSettings();

}

void Knowthelist::loadCurrentSettings()
{
    QSettings settings;

    if (monitorPlayer){
        on_cmdMonitorStop_clicked();
//ToDo: get the ID or Name of the default device
        monitorPlayer->setOutputDevice(settings.value("MonitorOutputDevice").toString());
        QString outDev = monitorPlayer->outputDeviceName();
        if ( monitorPlayer->outputDeviceID() == monitorPlayer->defaultDeviceID()
             || outDev.isEmpty()){
            ui->lblSoundcard->show();
            monitorPlayer->setVolume(0.0);
        }
        else {
            ui->lblSoundcard->hide();
            monitorPlayer->setVolume(1.0);
        }
    }

    //Auto DJ
    //m_AutoDJGenre=settings.value("editAutoDJGenre1","").toString();
    djSession->setMinCount(settings.value("minTracks","6").toInt());

    playList1->setAutoClearOn(settings.value("checkAutoRemove",true).toBool());
    playList2->setAutoClearOn(settings.value("checkAutoRemove",true).toBool());

    //Skip Silents Settings
    player1->setSkipSilentEnd(settings.value("checkSkipSilentEnd",true).toBool());
    player1->setSkipSilentBegin(settings.value("checkAutoCue",true).toBool());
    player2->setSkipSilentEnd(settings.value("checkSkipSilentEnd",true).toBool());
    player2->setSkipSilentBegin(settings.value("checkAutoCue",true).toBool());

    //Fader Settings
    mAutofadeLength=settings.value("faderTimeSlider","12").toInt();
    mAboutFinishTime=settings.value("faderEndSlider","12").toInt();
    setFaderModeToPlayer();
    isFading=false;

    //CollectionFolders
    collectionBrowser->loadSettings();

    //File Browser
    filetree->setRootPath(settings.value("editBrowerRoot","").toString());

    //Dj Widget List
    listDjs->clear();
    listDjs->setSelectionMode(QAbstractItemView::ExtendedSelection);


    DjWidget* djw;
    QListWidgetItem* itm;
    Dj* dj;
    int maxDj=settings.value("countDJ","3").toInt();

    settings.beginGroup("AutoDJ");

            // Filters
            for (int d=0;d<maxDj;d++)
            {
                settings.beginGroup(QString::number(d));
                    dj = new Dj();
                    dj->name = settings.value("Name","Dj%1").toString().arg(d+1);

                    djw  = new DjWidget(listDjs);

                    connect(djw,SIGNAL(activated()),this,SLOT(loadDj()));

                     itm = new QListWidgetItem(listDjs);
                     itm->setSizeHint(QSize(0,75));
                     listDjs->addItem(itm);
                     listDjs->setItemWidget(itm,djw);



                     // Filters
                     int countFilter = settings.value("FilterCount","2").toInt();
                     settings.beginGroup("Filter");
                     for (int i=0;i<countFilter;i++)
                     {
                         settings.beginGroup(QString::number(i));
                           Filter* f = new Filter();
                           dj->addFilter(f);

                           f->setPath(settings.value("Path","").toString());
                           f->setGenre(settings.value("Genre","").toString());
                           f->setArtist(settings.value("Artist","").toString());

                           // set to active for sum update
                           djSession->setCurrentDj(dj);
                           f->update();
                           f->setMaxUsage(settings.value("Value","2").toInt());
                         settings.endGroup();

                         f->setUsage(0);
                     }

                    djw->setDj(dj);
                    settings.endGroup();
                    dj->setActiveFilterIdx(settings.value("currentDjActiveFilter","0").toInt());
                    settings.endGroup();
            }
            settings.endGroup();
    listDjs->setCurrentRow(0);
    dj = ((DjWidget*)listDjs->itemWidget(listDjs->currentItem()))->dj();
    loadDj();

}

void Knowthelist::closeEvent(QCloseEvent* event)
{
    qDebug() << __FUNCTION__ << "for Knowthelist" ;

    QSettings settings;
    settings.setValue("Volume1", QString("%1").arg( ui->slider1->value() ) );
    settings.setValue("Volume2", QString("%1").arg( ui->slider2->value() ) );
    settings.setValue("VolumeMonitor", QString("%1").arg( ui->sliMonitorVolume->value() ) );

    //if (KnowthelistConfig::savePlaylist())
        savePlaylists();

    // save splitter
    settings.setValue("Splitter",splitter->saveState());

    //Save AutoDJ

    settings.beginGroup("AutoDJ");
    for (int d=0;d<listDjs->count();d++)
    {
        settings.beginGroup(QString::number(d));
        Dj* dj = ((DjWidget*)listDjs->itemWidget(listDjs->item(d)))->dj();
        QList<Filter*> f=dj->filters();

        settings.setValue("currentDjActiveFilter",QString("%1").arg(djSession->currentDj()->activeFilterIdx()));
        settings.beginGroup("Filter");
        for (int i=0; i<f.count();i++)
        {
             settings.beginGroup(QString::number(i));
             settings.setValue("Path",f.at(i)->path());
             settings.setValue("Genre",f.at(i)->genre());
             settings.setValue("Artist",f.at(i)->artist());
             settings.setValue("Value",QString::number(f.at(i)->maxUsage()));
             settings.endGroup();
        }
        settings.endGroup();
        settings.endGroup();
    }
    settings.endGroup();

    Dj* dj = djSession->currentDj();
    QList<Filter*> f = dj->filters();

    //Save AutoDJ

    settings.setValue("currentDjActiveFilter",QString("%1").arg(djSession->currentDj()->activeFilterIdx()));

     for (int i=0; i<f.count();i++)
    {
         settings.setValue(QString("editAutoDJPath%1").arg(i),f.at(i)->path());
         settings.setValue(QString("editAutoDJGenre%1").arg(i),f.at(i)->genre());
         settings.setValue(QString("editAutoDJArtist%1").arg(i),f.at(i)->artist());
         settings.setValue(QString("editAutoDJValue%1").arg(i),QString("%1").arg(f.at(i)->maxUsage()));
    }

    settings.setValue("checkAutoFade", ui->toggleAutoFade->isChecked());
    settings.setValue("checkAGC", ui->toggleAGC->isChecked());

    settings.setValue("EQ_gains/High1", ui->potHigh_1->value());
    settings.setValue("EQ_gains/Mid1", ui->potMid_1->value());
    settings.setValue("EQ_gains/Low1", ui->potLow_1->value());
    settings.setValue("EQ_gains/High2", ui->potHigh_2->value());
    settings.setValue("EQ_gains/Mid2", ui->potMid_2->value());
    settings.setValue("EQ_gains/Low2", ui->potLow_2->value());

    settings.setValue("mainWindowGeometry", saveGeometry());
    settings.setValue("mainWindowState", saveState());

    event->accept();
}

void Knowthelist::showCollectionSetup()
{
    preferences->setCurrentTab(SettingsDialog::TabCollection);
    if ( preferences->exec() != QDialog::Rejected )
             loadCurrentSettings();
}

void Knowthelist::loadDj()
{
    //Fill Filter Widget List
    qDebug() << __PRETTY_FUNCTION__ ;

    listDjFilters->clear();

    DjFilterWidget *djfw;
    QListWidgetItem * itm;

    for (int d=0;d<listDjs->count();d++)
        ((DjWidget*)listDjs->itemWidget(listDjs->item(d)))->deactivateDJ();

    Dj* dj = ((DjWidget*)listDjs->itemWidget(listDjs->currentItem()))->dj();
    ((DjWidget*)listDjs->itemWidget(listDjs->currentItem()))->activateDJ();

    // Filters
    if (dj)
    {
         // Filters
        qDebug() << __PRETTY_FUNCTION__<<dj->filters().count() ;
        for (int i=0;i<dj->filters().count();i++)
        {

                           djfw  = new DjFilterWidget(listDjFilters);
                           djfw->setFilter( dj->filters().at(i) );
                           djfw->setID(QString::number(i+1));
                           itm = new QListWidgetItem(listDjFilters);
                           itm->setSizeHint(QSize(0,75));
                           listDjFilters->addItem(itm);
                           listDjFilters->setItemWidget(itm,djfw);
        }

        dj->setActiveFilterIdx(0);
        djSession->setCurrentDj(dj);
    }

}

void Knowthelist::player1_levelChanged(double left, double right)
{
    vuMeter1->setLeftValue( left * 300.0 );
    vuMeter1->setRightValue( right * 300.0 );
}

void Knowthelist::player2_levelChanged(double left, double right)
{
    vuMeter2->setLeftValue( left * 300.0 );
    vuMeter2->setRightValue( right * 300.0 );
}


void Knowthelist::player_aboutTrackFinished( ) {
    if ( ui->toggleAutoFade->isChecked() )
       fadeNow();
}

void Knowthelist::player1_trackFinished( ) {
    if ( isFading )
        player1->stop();
    playList1->skipForward();
}

void Knowthelist::player2_trackFinished( ) {
    if ( isFading )
        player2->stop();
    playList2->skipForward();
}

void Knowthelist::player1_gainChanged(double gainValue)
{
    gain1Target = (int)(gainValue * 100.0);
    if ( ui->toggleAGC->isChecked())
         timerGain1->start();
}

void Knowthelist::player2_gainChanged(double gainValue)
{
   gain2Target = (int)(gainValue * 100.0);
   if ( ui->toggleAGC->isChecked())
        timerGain2->start();
}

// Move gain1 dial smoothly
void Knowthelist::timerGain1_timeOut()
{
   int gain1 = ui->potGain_1->value();
   if ( gain1Target > gain1 )
       ui->potGain_1->setValue(gain1+1);
   else if ( gain1Target < gain1 )
       ui->potGain_1->setValue(gain1-1);
   else
       timerGain1->stop();
}

// Move gain2 dial smoothly
void Knowthelist::timerGain2_timeOut()
{
   int gain2 = ui->potGain_2->value();
   if ( gain2Target > gain2 )
       ui->potGain_2->setValue(gain2+1);
   else if ( gain2Target < gain2 )
       ui->potGain_2->setValue(gain2-1);
   else
       timerGain2->stop();
}

void Knowthelist::fadeNow()
{
        //Fade now!
        if ( !isFading && (playList1->countTrack()>0 || playList2->countTrack()>0)) {
          if ( ui->sliFader->value() > 100 )
          {
            m_xfadeDir=-1;
            if ( ! player1->isStarted() ) player1->play();
            //Fader has 200 steps * 5 = 1000ms
            timerAutoFader->start( mAutofadeLength * 5 );
          }
          else
          {
            m_xfadeDir=1;
            if ( ! player2->isStarted() ) player2->play();
            timerAutoFader->start( mAutofadeLength * 5 );
          }

          isFading=true;

          //ToDo: search for a right time to save
          savePlaylists();

        }
}

void Knowthelist::changeVolumes()
{

    float v1 = ui->slider1->value()/100.0;
    float v2 = ui->slider2->value()/100.0;


    float f1 = 2 - ui->sliFader->value()/100.0;
    float f2 = ui->sliFader->value()/100.0;

    f1 = ( f1<1 ) ? f1 : 1;
    f2 = ( f2<1 ) ? f2 : 1;

    player1->setVolume( v1 * f1 );
    player2->setVolume( v2 * f2 );

}
void Knowthelist::slider1_valueChanged(int)
{
    changeVolumes();
}

void Knowthelist::slider2_valueChanged(int)
{
    changeVolumes();
}

void Knowthelist::sliFader_valueChanged(int)
{
    changeVolumes();
    if ( ui->sliFader->value() == ui->sliFader->minimum() )
    {
      playList1->setIsCurrentList(true);
      playList2->setIsCurrentList(false);
    }
    if ( ui->sliFader->value() == ui->sliFader->maximum() )
    {
      playList2->setIsCurrentList(true);
      playList1->setIsCurrentList(false);
    }
}


void Knowthelist::timerAutoFader_timerOut( )

{
        //Auto-Fader moves
        ui->sliFader->setValue( ui->sliFader->value() + m_xfadeDir );

        //Blinking
        if ( ui->sliFader->value()%3 == 0 ){
            if ( m_xfadeDir < 0 )
                ui->ledFadeLeft->toggle();
            else
                ui->ledFadeRight->toggle();
        }

        if ( ui->sliFader->value() <= ui->sliFader->minimum() ) {
                //Fade from 2 to 1 is done
                timerAutoFader->stop();
                ui->ledFadeLeft->off();
                isFading=false;

                //ToDo:handle AutoDJ from Playlist
                if ( player2->isStarted() ) {
                    player2->stop();
                    playList2->skipForward();
                    }
                  if ( ui->toggleAutoDJ->isChecked() )
                      djSession->fillPlaylists();

        }
        if ( ui->sliFader->value() >= ui->sliFader->maximum() ) {
                //Fade from 1 to 2 is done
                timerAutoFader->stop();
                isFading=false;
                ui->ledFadeRight->off();

                //ToDo:  handle AutoDJ from Playlist

                if ( player1->isStarted() ) {
                  player1->stop();
                  playList1->skipForward();
                  }
                  if ( ui->toggleAutoDJ->isChecked()  )
                      djSession->fillPlaylists();
        }
        changeVolumes();

}

void Knowthelist::savePlaylists()
{
    playList1->saveXML( playList1->defaultPlaylistPath() );
    playList2->saveXML( playList2->defaultPlaylistPath() );
}

void Knowthelist::Track_selectionChanged(  PlaylistItem *item )
{
    if ( item ){

        ui->lblMonitorTrack->setText( item->track()->prettyTitle(40) );
        wantSeek=false;
        //QPixmap pix = QPixmap::fromImage(item->track()->coverImage());
                //if (!pix.isNull())
        //        ui->pixmapLabel->setPixmap(pix);

        if ( monitorPlayer ) {
             on_cmdMonitorStop_clicked();
         //monitorPlayer->loadTrack( item->track()->url() );
         monitorPlayer->open(item->track()->url() );
         QPixmap pix = QPixmap::fromImage(item->track()->coverImage());
                 if (!pix.isNull())
                 ui->pixMonitorCover->setPixmap(pix);
                 timerMonitor_timeOut();
       }

    }
    else{
        ui->lblMonitorTrack->setText("");
        ui->pixMonitorCover->setPixmap(QPixmap());
        //monitorPlayer->loadTrack(QUrl());
    }

}

void Knowthelist::timerMonitor_loadFinished()
{
        timerMonitor_timeOut();
        if (wantSeek)
        {
            on_sliMonitor_sliderMoved(100);
        }
}

void Knowthelist::Track_doubleClicked(PlaylistItem* item)
{
    Track_selectionChanged(item );
    if ( monitorPlayer )
    {
        wantSeek=true;
        on_cmdMonitorPlay_clicked();
    }
}

void Knowthelist::trackList_wantLoad(PlaylistItem *pli, QString source)
{
    //ToDo: enable for multiple tracks like drag/drop
    qDebug() << __FUNCTION__ << "source=" << source;
    if ( source == "Right" )
      playList2->appendSong( pli->track() );
    else if ( source ==  "Left" )
      playList1->appendSong( pli->track() );
}

//ToDo: find a better name
void Knowthelist::onWantLoad(QList<Track*> trackList, QString target)
{
    if ( target == "Right" )
      playList2->appendTracks(trackList);
    else if ( target ==  "Left" )
      playList1->appendTracks(trackList);
}

void Knowthelist::setFaderModeToPlayer()
{
    if ( autoFadeOn ) {
        player1->setTrackFinishEmitTime( mAboutFinishTime );
        player2->setTrackFinishEmitTime( mAboutFinishTime );
        playList1->setPlaylistMode(Playlist::Playlist_Multi);
        playList2->setPlaylistMode(Playlist::Playlist_Multi);
    }
    else {
        player1->setTrackFinishEmitTime( 0 );
        player2->setTrackFinishEmitTime( 0 );
        playList1->setPlaylistMode(Playlist::Playlist_Single);
        playList2->setPlaylistMode(Playlist::Playlist_Single);
    }
}


void Knowthelist::editSettings()
{
    // update hardware infos
    monitorPlayer->readDevices();
    QSettings settings;
    settings.setValue("MonitorOutputDevices", monitorPlayer->outputDevices());

    if ( preferences->exec() != QDialog::Rejected )
         loadCurrentSettings();
}

void Knowthelist::on_cmdFade_clicked()
{
      fadeNow();
}

bool Knowthelist::initMonitorPlayer()
{
    qDebug() << __FUNCTION__ << "BEGIN ";

      monitorPlayer= new MonitorPlayer(this);
      monitorPlayer->prepare();
      monitorPlayer->setObjectName("monitorPlayer");

      ui->cmdMonitorStop->setIcon(QIcon(":stop.png"));
      ui->cmdMonitorPlay->setIcon(QIcon(":play.png"));
      //connect(monitorPlayer,SIGNAL(trackTimeChanged(qint64,qint64)),this,SLOT(monitorPlayer_trackTimeChanged(qint64,qint64)));
      //connect(monitorPlayer,SIGNAL(foundCover(QImage*)),this,SLOT(monitorPlayer_foundCover(QImage*)));
      connect(monitorPlayer,SIGNAL(loadFinished()),this,SLOT(timerMonitor_loadFinished()));

    qDebug()  << __FUNCTION__ << "END " ;
    return true;
}

void Knowthelist::on_cmdMonitorStop_clicked()
{
    monitorPlayer->stop();
    timerMonitor->stop();
    ui->cmdMonitorPlay->setIcon(QIcon(":play.png"));
    //ui->monitorMeter->reset();
    ui->monitorMeter->setRightValue(0);
    ui->monitorMeter->setLeftValue(0);
}

void Knowthelist::on_cmdMonitorPlay_clicked()
{
    if (monitorPlayer->isPlaying()) {
        ui->cmdMonitorPlay->setIcon(QIcon(":play.png"));
        monitorPlayer->pause();
        timerMonitor->stop();
        //ui->monitorMeter->reset();
        ui->monitorMeter->setRightValue(0);
        ui->monitorMeter->setLeftValue(0);
    }
    else {
        ui->cmdMonitorPlay->setIcon(QIcon(":pause.png"));
        monitorPlayer->play();
        timerMonitor->start();
    }
}

void Knowthelist::monitorPlayer_trackTimeChanged(qint64 time, qint64 totalTime)
{
    //ToDo: delete this function: Why?
    if ( ui->sliMonitor->maximum() !=totalTime )
    {
        if (totalTime==0)
            ui->sliMonitor->setMaximum(100);
        else
            ui->sliMonitor->setMaximum(totalTime);
    }

    ui->sliMonitor->setValue(time);

    QTime displayTime(0, (time / 60000) % 60, (time / 1000) % 60);
    QTime displayTotalTime(0, (totalTime / 60000) % 60, (totalTime / 1000) % 60);

    ui->lblMonitorPosition->setText(displayTime.toString("mm:ss"));
    ui->lblMonitorLength->setText(displayTotalTime.toString("mm:ss"));
}

void Knowthelist::timerMonitor_timeOut()
{

    QTime length = monitorPlayer->length();
    QTime curpos = monitorPlayer->position();
    QTime remain(0,0);
    long remainMs;

    remainMs=curpos.msecsTo(length);
    remain = QTime(0,0).addMSecs(remainMs);

    ui->lblMonitorPosition->setText(curpos.toString("mm:ss.zzz").left(7));
    ui->lblMonitorLength->setText(length.toString("mm:ss"));

    //update position slider
    if (length != QTime(0,0)) {
        ui->sliMonitor->setValue(curpos.msecsTo(QTime()) * 1000 / length.msecsTo(QTime()));
    } else {
        ui->sliMonitor->setValue(0);
    }

    ui->monitorMeter->setLeftValue( monitorPlayer->levelLeft() * 100.0 );
    ui->monitorMeter->setRightValue( monitorPlayer->levelRight() * 100.0);
}

void Knowthelist::on_sliMonitor_sliderMoved(int value)
{
        uint length = -monitorPlayer->length().msecsTo(QTime());
        if (length != 0 && value > 0) {
            QTime pos;
            pos = pos.addMSecs(length * (value / 1000.0));
                    qDebug()<<"pos:"<<pos;
    monitorPlayer->setPosition(pos);
        }

}

void Knowthelist::on_cmdOptions_clicked()
{
    preferences->setCurrentTab(SettingsDialog::TabFader);
    editSettings();
}

void Knowthelist::on_potGain_1_valueChanged(int value)
{
    player1->setGain(value / 100.0);
}

void Knowthelist::on_potGain_2_valueChanged(int value)
{
    player2->setGain(value / 100.0);
}

void Knowthelist::on_toggleAGC_toggled(bool checked)
{
    Q_UNUSED(checked);
    ui->ledAGC->toggle();
}

void Knowthelist::on_toggleAutoDJ_toggled(bool checked)
{
    if ( checked ) {
        //AutoDJ on

        //Fill both playlists
        djSession->fillPlaylists();

        //Start playing
        if ( !player1->isStarted() && !player2->isStarted() )
            fadeNow();
        //Activate Autofade
        ui->toggleAutoFade->setChecked( true );
    }
    else{
     m_AutoDJGenre=collectionBrowser->filterText();
    }
    ui->ledDJ->toggle();

}


void Knowthelist::on_toggleAutoFade_toggled(bool checked)
{
      ui->ledFade->setState(checked ? QLed::On : QLed::Off);
      autoFadeOn = checked;
      setFaderModeToPlayer();
}


void Knowthelist::on_potHigh_1_valueChanged(int value)
{
    player1->setEqualizer(PlayerWidget::EQ_High, value);
}
void Knowthelist::on_potMid_1_valueChanged(int value)
{
    player1->setEqualizer(PlayerWidget::EQ_Mid, value);
}
void Knowthelist::on_potLow_1_valueChanged(int value)
{
    player1->setEqualizer(PlayerWidget::EQ_Low, value);
}
void Knowthelist::on_potHigh_2_valueChanged(int value)
{
    player2->setEqualizer(PlayerWidget::EQ_High, value);
}
void Knowthelist::on_potMid_2_valueChanged(int value)
{
    player2->setEqualizer(PlayerWidget::EQ_Mid, value);
}
void Knowthelist::on_potLow_2_valueChanged(int value)
{
    player2->setEqualizer(PlayerWidget::EQ_Low, value);
}

void Knowthelist::on_lblSoundcard_linkActivated(const QString &link)
{
    Q_UNUSED(link);
    preferences->setCurrentTab(SettingsDialog::TabMonitor);
    editSettings();
}


void Knowthelist::on_sliMonitor_actionTriggered(int action)
{
    //a workaround for page moving
    int posi;
    switch (action)
    {
        case 3:
            posi=ui->sliMonitor->value()+100;
            break;
        case 4:
            posi=ui->sliMonitor->value()-100;
            if (posi<100)
                posi=1;
            break;
        case 1:
            posi=ui->sliMonitor->value()+10;
            break;
        case 2:
            posi=ui->sliMonitor->value()-10;
            break;
        default:
            return;
            break;
    }

    this->on_sliMonitor_sliderMoved(posi);

}

void Knowthelist::on_sliMonitorVolume_valueChanged(int value)
{
    monitorPlayer->setVolume( value/100.0 );
}
