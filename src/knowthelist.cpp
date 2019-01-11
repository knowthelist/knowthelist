/*
    Copyright (C) 2005-2019 Mario Stephan <mstephan@shared-files.de>

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
#include "dj.h"
#include "djfilterwidget.h"
#include "djwidget.h"
#include "playerwidget.h"
#include "playlistbrowser.h"
#include "qled.h"
#include "ui_knowthelist.h"

#include <QBoxLayout>
#include <QSettings>
#if QT_VERSION >= 0x050000
#include <QtConcurrent/QtConcurrent>
#else
#include <QtConcurrentRun>
#endif
#include <QMetaType>

Knowthelist::Knowthelist(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::Knowthelist)
{
    ui->setupUi(this);

    //create the UI
    createUI();
}

Knowthelist::~Knowthelist()
{
    qDebug() << Q_FUNC_INFO << "START closing application";
    player1->stop();
    delete player1;
    player1 = nullptr;
    delete playList1;
    player2->stop();
    delete player2;
    player2 = nullptr;
    delete playList2;
    delete vuMeter1;
    delete vuMeter2;
    delete monitorMeter;
    delete monitorPlayer;
    monitorPlayer = nullptr;
    delete djSession;
    djSession = nullptr;
    delete trackList;
    trackList = nullptr;
    delete collectionBrowser;
    delete djBrowser;
    delete filetree;
    delete playlistBrowser;
    delete trackList2;
    delete splitterPlaylist;
    delete ui;
    qDebug() << Q_FUNC_INFO << "END closing application";
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
    connect(ui->slider1, SIGNAL(valueChanged(int)), this, SLOT(slider1_valueChanged(int)));
    connect(ui->slider2, SIGNAL(valueChanged(int)), this, SLOT(slider2_valueChanged(int)));
    connect(ui->sliFader, SIGNAL(valueChanged(int)), this, SLOT(sliFader_valueChanged(int)));

    //Add player
    player1 = ui->player_L;
    player2 = ui->player_R;
    monitorPlayer = new MonitorPlayer(this);

    timerAutoFader = new QTimer(this);
    connect(timerAutoFader, SIGNAL(timeout()), SLOT(timerAutoFader_timerOut()));

    vuMeter2 = new VUMeter(ui->frameMixer);
    vuMeter2->setLinesPerSegment(2);
    vuMeter2->setSpacesBetweenSegments(1);
    vuMeter2->setSegmentsPerPeak(1);
    vuMeter2->setMargin(2);
    vuMeter2->LevelColorOff.setRgb(20, 20, 20);

    vuMeter1 = new VUMeter(ui->frameMixer);
    vuMeter1->setLinesPerSegment(2);
    vuMeter1->setSpacesBetweenSegments(1);
    vuMeter1->setSegmentsPerPeak(1);
    vuMeter1->setMargin(2);
    vuMeter1->LevelColorOff.setRgb(20, 20, 20);

    monitorMeter = new VUMeter(ui->fraMonitorTop);
    monitorMeter->setSpacesBetweenSegments(1);
    monitorMeter->setLinesPerSegment(2);
    monitorMeter->setSegmentsPerPeak(2);
    monitorMeter->setMargin(2);
    monitorMeter->LevelColorOff.setRgb(20, 20, 20);

    vuMeter1->setGeometry(ui->phVU1->geometry());
    vuMeter2->setGeometry(ui->phVU2->geometry());
    monitorMeter->setGeometry(ui->phVUMeter->geometry());

    ui->potGain_1->setRange(10, 180);
    ui->potGain_1->setValue(100);
    ui->potGain_2->setRange(10, 180);
    ui->potGain_2->setValue(100);

    timerMonitor = new QTimer(this);
    timerMonitor->setInterval(50);
    connect(timerMonitor, SIGNAL(timeout()), SLOT(timerMonitor_timeOut()));

    timerGain1 = new QTimer(this);
    timerGain2 = new QTimer(this);
    timerGain1->setInterval(100);
    timerGain2->setInterval(100);
    connect(timerGain1, SIGNAL(timeout()), SLOT(timerGain1_timeOut()));
    connect(timerGain2, SIGNAL(timeout()), SLOT(timerGain2_timeOut()));

    qRegisterMetaType<QList<Track*>>("QList<Track*>");

    //Add DJ
    djSession = new DjSession();

    playList1 = ui->playlist_L;
    playList1->setIsCurrentList(true);

    playList2 = ui->playlist_R;
    playList2->setIsCurrentList(false);

    connect(playList1, SIGNAL(currentTrackChanged(Track*)), player1, SLOT(loadTrack(Track*)));
    connect(playList2, SIGNAL(currentTrackChanged(Track*)), player2, SLOT(loadTrack(Track*)));

    connect(player1, SIGNAL(forwardPressed()), playList1, SLOT(skipForward()));
    connect(player2, SIGNAL(forwardPressed()), playList2, SLOT(skipForward()));

    connect(player1, SIGNAL(rewindPressed()), playList1, SLOT(skipRewind()));
    connect(player2, SIGNAL(rewindPressed()), playList2, SLOT(skipRewind()));

    connect(player1, SIGNAL(aboutFinished()), SLOT(player_aboutTrackFinished()));
    connect(player2, SIGNAL(aboutFinished()), SLOT(player_aboutTrackFinished()));

    connect(player1, SIGNAL(gainChanged(double)), SLOT(player1_gainChanged(double)));
    connect(player2, SIGNAL(gainChanged(double)), SLOT(player2_gainChanged(double)));

    connect(player1, SIGNAL(levelChanged(double, double)), SLOT(player1_levelChanged(double, double)));
    connect(player2, SIGNAL(levelChanged(double, double)), SLOT(player2_levelChanged(double, double)));

    connect(player1, SIGNAL(statusChanged(bool)), playList1, SLOT(setPlaying(bool)));
    connect(player2, SIGNAL(statusChanged(bool)), playList2, SLOT(setPlaying(bool)));

    connect(player1, SIGNAL(trackFinished()), SLOT(player1_trackFinished()));
    connect(player2, SIGNAL(trackFinished()), SLOT(player2_trackFinished()));

    connect(player1, SIGNAL(trackPlayed(Track*)), djSession, SLOT(onTrackFinished(Track*)));
    connect(player2, SIGNAL(trackPlayed(Track*)), djSession, SLOT(onTrackFinished(Track*)));

    connect(player1, SIGNAL(trackDropped(Track*)), playList1, SLOT(addCurrentTrack(Track*)));
    connect(player2, SIGNAL(trackDropped(Track*)), playList2, SLOT(addCurrentTrack(Track*)));

    //alternateMax
    connect(playList1, SIGNAL(countChanged(int)), playList2, SLOT(setAlternateMax(int)));
    connect(playList2, SIGNAL(countChanged(int)), playList1, SLOT(setAlternateMax(int)));
    connect(playList1, SIGNAL(countChanged(QList<Track*>)), djSession, SLOT(onTracksChanged_Playlist1(QList<Track*>)));
    connect(playList2, SIGNAL(countChanged(QList<Track*>)), djSession, SLOT(onTracksChanged_Playlist2(QList<Track*>)));

    connect(djSession, SIGNAL(foundTracks_Playlist1(QList<Track*>)), playList1, SLOT(appendTracks(QList<Track*>)));
    connect(djSession, SIGNAL(foundTracks_Playlist2(QList<Track*>)), playList2, SLOT(appendTracks(QList<Track*>)));

    connect(djSession, SIGNAL(changed_Playlist1(QPair<int, int>)), player1, SLOT(setInfo(QPair<int, int>)));
    connect(djSession, SIGNAL(changed_Playlist2(QPair<int, int>)), player2, SLOT(setInfo(QPair<int, int>)));

    //Add Tracklist for Collection
    trackList = new Playlist();
    trackList->setObjectName("tracklist");
    trackList->setAcceptDrops(false);
    trackList->setPlaylistMode(Playlist::Tracklist);

    collectionBrowser = new CollectionWidget(this);

    splitter = new QSplitter();
    splitter->addWidget(this->collectionBrowser);
    splitter->addWidget(trackList);
    QPixmap pixmap1(":database.png");
    ui->sideTab->AddTab(splitter, QIcon(pixmap1), tr("Collection"));

    connect(collectionBrowser, SIGNAL(selectionChanged(QList<Track*>)), trackList, SLOT(changeTracks(QList<Track*>)));
    connect(collectionBrowser, SIGNAL(setupDirs()), this, SLOT(showCollectionSetup()));
    connect(collectionBrowser, SIGNAL(wantLoad(QList<Track*>, QString)), this, SLOT(onWantLoad(QList<Track*>, QString)));

    connect(trackList, SIGNAL(wantSearch(QString)), collectionBrowser, SLOT(setFilterText(QString)));
    connect(playList1, SIGNAL(wantSearch(QString)), collectionBrowser, SLOT(setFilterText(QString)));
    connect(playList2, SIGNAL(wantSearch(QString)), collectionBrowser, SLOT(setFilterText(QString)));

    connect(trackList, SIGNAL(trackDoubleClicked(Track*)), SLOT(Track_doubleClicked(Track*)));
    connect(trackList, SIGNAL(wantLoad(Track*, QString)), SLOT(trackList_wantLoad(Track*, QString)));
    connect(trackList, SIGNAL(trackSelected(Track*)), SLOT(Track_selectionChanged(Track*)));
    connect(trackList, SIGNAL(trackPropertyChanged(Track*)), djSession, SLOT(onTrackPropertyChanged(Track*)));

    connect(playList1, SIGNAL(trackDoubleClicked(Track*)), SLOT(Track_doubleClicked(Track*)));
    connect(playList2, SIGNAL(trackDoubleClicked(Track*)), SLOT(Track_doubleClicked(Track*)));

    connect(playList1, SIGNAL(trackSelected(Track*)), SLOT(Track_selectionChanged(Track*)));
    connect(playList2, SIGNAL(trackSelected(Track*)), SLOT(Track_selectionChanged(Track*)));

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
    ui->ledFadeRight->setColor(QColor(35, 119, 246));
    ui->ledFadeLeft->setColor(QColor(35, 119, 246));
    ui->ledFade->setColor(QColor(35, 119, 246));
    ui->ledDJ->setColor(QColor(35, 119, 246));
    ui->ledAGC->setColor(QColor(35, 119, 246));
    ui->ledFade->off();
    ui->ledFadeRight->off();
    ui->ledFadeLeft->off();
    ui->ledAGC->off();
    ui->ledDJ->off();

    //MonitorPlayer
    initMonitorPlayer();

    //change slider style for linux
#if defined(Q_OS_LINUX)
    QString sliderStyle = QString(
        "QSlider::sub-page:vertical { background: qlineargradient(x1: 0, y1: 0, x2:1, y2: 0,"
        "   stop: 0.4 #666, stop: 0 #111111 ); border: 1px solid #444; border-radius: 2px;}"
        "QSlider::add-page:vertical {background: qlineargradient(x1: 0, y1: 0, x2:1, y2: 0,"
        "   stop: 0 #111,stop: 0.4 #666); border: 1px solid #333; border-radius: 2px;}"
        "QSlider::sub-page:horizontal,QSlider::add-page:horizontal  {"
        "   background: qlineargradient(x1: 0, y1: 0,    x2: 0, y2: 1,"
        "   stop: 0 #111, stop: 0.6 #666 ); border: 1px solid #222; border-radius: 2px;}");

    ui->frameMixer->setStyleSheet(sliderStyle);
    ui->MonitorPlayer->setStyleSheet(sliderStyle);
#endif

    //Add the AutoDJ Browser
    djBrowser = new DjBrowser();
    QPixmap pixmap2(":DJ.png");
    ui->sideTab->AddTab(djBrowser, QIcon(pixmap2), tr("AutoDJ"));
    ui->sideTab->setContextMenuPolicy(Qt::NoContextMenu);
    connect(djBrowser, SIGNAL(selectionChanged(Dj*)), djSession, SLOT(setCurrentDj(Dj*)));
    connect(djBrowser, SIGNAL(selectionStarted()), this, SLOT(startAutoDj()));

    //Add the FileBrowser
    filetree = new FileBrowser(this);
    QPixmap pixmap3(":folder.png");
    ui->sideTab->AddTab(filetree, QIcon(pixmap3), tr("Folder"));

    //Add PlaylistBrowser
    playlistBrowser = new PlaylistBrowser();

    splitterPlaylist = new QSplitter();
    splitterPlaylist->addWidget(playlistBrowser);

    trackList2 = new Playlist();
    trackList2->setObjectName("tracklist2");
    trackList2->setAcceptDrops(false);
    trackList2->setPlaylistMode(Playlist::Tracklist);

    connect(playlistBrowser, SIGNAL(selectionChanged(QList<Track*>)), trackList2, SLOT(changeTracks(QList<Track*>)));
    connect(playlistBrowser, SIGNAL(selectionStarted(QList<Track*>)), djSession, SLOT(forceTracks(QList<Track*>)));
    //connect(playlistBrowser,SIGNAL(savePlaylists(QString)),djSession, SLOT(savePlaylists(QString)));
    connect(playlistBrowser, SIGNAL(storePlaylists(QString)), djSession, SLOT(storePlaylists(QString)));
    connect(djSession, SIGNAL(savedPlaylists()), playlistBrowser, SLOT(updateLists()));
    connect(trackList2, SIGNAL(trackDoubleClicked(Track*)), SLOT(Track_doubleClicked(Track*)));
    connect(trackList2, SIGNAL(wantLoad(Track*, QString)), SLOT(trackList_wantLoad(Track*, QString)));
    connect(trackList2, SIGNAL(trackSelected(Track*)), SLOT(Track_selectionChanged(Track*)));
    connect(trackList2, SIGNAL(trackPropertyChanged(Track*)), djSession, SLOT(onTrackPropertyChanged(Track*)));

    splitterPlaylist->addWidget(trackList2);
    QPixmap pixmap4(":list.png");
    ui->sideTab->AddTab(splitterPlaylist, QIcon(pixmap4), tr("Lists"));

    //Add SettingsDialog
    preferences = new SettingsDialog(this);
    connect(preferences, SIGNAL(scanNowPressed()), collectionBrowser, SLOT(scan()));
    connect(preferences, SIGNAL(resetStatsPressed()), djSession, SLOT(onResetStats()));

    loadStartSettings();

    ui->sideTab->SetCurrentIndex(0);
    ui->sideTab->SetMode(FancyTabWidget::Mode_LargeSidebar);

    //Collection ready?
    if (!collectionBrowser->hasItems()) {
        this->show();
        showCollectionSetup();
    }
}

void Knowthelist::loadStartSettings()
{
    QSettings settings;

    ui->slider1->setValue(settings.value("Volume1", 80).toInt());
    ui->slider2->setValue(settings.value("Volume2", 80).toInt());

    ui->sliFader->setValue(70);
    changeVolumes();

    splitter->restoreState(settings.value("Splitter").toByteArray());
    splitterPlaylist->restoreState(settings.value("SplitterPlaylist").toByteArray());

    restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
    restoreState(settings.value("mainWindowState").toByteArray());

    // Workaround to force correct geometry
    hide();
    show();

    if (settings.value("loadPlaylists", "true") == "true") {
        djSession->playDefaultList();
    }

    //AutoFade, AGC ...
    ui->toggleAutoFade->setChecked(settings.value("checkAutoFade", true).toBool());
    ui->toggleAGC->setChecked(settings.value("checkAGC", true).toBool());

    //EQ values
    ui->potHigh_1->setValue(settings.value("EQ_gains/High1", 180).toInt());
    ui->potMid_1->setValue(settings.value("EQ_gains/Mid1", 180).toInt());
    ui->potLow_1->setValue(settings.value("EQ_gains/Low1", 180).toInt());
    ui->potHigh_2->setValue(settings.value("EQ_gains/High2", 180).toInt());
    ui->potMid_2->setValue(settings.value("EQ_gains/Mid2", 180).toInt());
    ui->potLow_2->setValue(settings.value("EQ_gains/Low2", 180).toInt());

    loadCurrentSettings();

    //now monitorplayer is initialized, restore monitor volume with effect
    ui->sliMonitorVolume->setValue(settings.value("VolumeMonitor").toDouble());
}

void Knowthelist::loadCurrentSettings()
{
    QSettings settings;

    if (monitorPlayer) {
        on_cmdMonitorStop_clicked();

        monitorPlayer->setOutputDevice(settings.value("MonitorOutputDevice").toString());
        QString outDev = monitorPlayer->outputDeviceName();
        if (monitorPlayer->outputDeviceID() == monitorPlayer->defaultDeviceID()
            || outDev.isEmpty()) {
            ui->lblSoundcard->show();
            monitorPlayer->disable();
        } else {
            ui->lblSoundcard->hide();
            monitorPlayer->enable();
        }
    }

    //Auto DJ Settings
    djSession->setMinCount(settings.value("minTracks", "6").toInt());
    djSession->setIsEnabledAutoDJCount(settings.value("isEnabledAutoDJCount", false).toBool());
    djBrowser->updateList();

    playList1->setAutoClearOn(settings.value("checkAutoRemove", true).toBool());
    playList2->setAutoClearOn(settings.value("checkAutoRemove", true).toBool());
    playlistBrowser->updateLists();

    //Skip Silents Settings
    player1->setSkipSilentEnd(settings.value("checkSkipSilentEnd", true).toBool());
    player1->setSkipSilentBegin(settings.value("checkAutoCue", true).toBool());
    player2->setSkipSilentEnd(settings.value("checkSkipSilentEnd", true).toBool());
    player2->setSkipSilentBegin(settings.value("checkAutoCue", true).toBool());

    //Fader Settings
    mAutofadeLength = settings.value("faderTimeSlider", "12").toInt();
    mAboutFinishTime = settings.value("faderEndSlider", "12").toInt();
    setFaderModeToPlayer();
    isFading = false;

    //CollectionFolders Settings
    collectionBrowser->loadSettings();

    //File Browser Settings
    filetree->setRootPath(settings.value("editBrowerRoot", "").toString());
}

void Knowthelist::closeEvent(QCloseEvent* event)
{
    qDebug() << Q_FUNC_INFO << "for Knowthelist";

    QSettings settings;
    settings.setValue("Volume1", QString("%1").arg(ui->slider1->value()));
    settings.setValue("Volume2", QString("%1").arg(ui->slider2->value()));
    settings.setValue("VolumeMonitor", QString("%1").arg(ui->sliMonitorVolume->value()));

    savePlaylists();

    //Save splitter
    settings.setValue("Splitter", splitter->saveState());
    settings.setValue("SplitterPlaylist", splitterPlaylist->saveState());

    //Save AutoDJ
    settings.setValue("isEnabledAutoDJCount", djSession->isEnabledAutoDJCount());

    Dj* dj = djSession->currentDj();
    if (dj != nullptr) {
        QList<Filter*> f = dj->filters();

        settings.setValue("currentDjActiveFilter", QString("%1").arg(djSession->currentDj()->activeFilterIdx()));

        for (int i = 0; i < f.count(); i++) {
            settings.setValue(QString("editAutoDJPath%1").arg(i), f.at(i)->path());
            settings.setValue(QString("editAutoDJGenre%1").arg(i), f.at(i)->genre());
            settings.setValue(QString("editAutoDJArtist%1").arg(i), f.at(i)->artist());
            settings.setValue(QString("editAutoDJValue%1").arg(i), QString("%1").arg(f.at(i)->maxUsage()));
        }
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

    qDebug() << Q_FUNC_INFO << "settings saved";
    event->accept();
}

void Knowthelist::showCollectionSetup()
{
    preferences->setCurrentTab(SettingsDialog::TabCollection);
    if (preferences->exec() != QDialog::Rejected)
        loadCurrentSettings();
}

void Knowthelist::player1_levelChanged(double left, double right)
{
    vuMeter1->setValueLeft(left * 3.0);
    vuMeter1->setValueRight(right * 3.0);
}

void Knowthelist::player2_levelChanged(double left, double right)
{
    vuMeter2->setValueLeft(left * 3.0);
    vuMeter2->setValueRight(right * 3.0);
}

void Knowthelist::player_aboutTrackFinished()
{
    if (ui->toggleAutoFade->isChecked())
        fadeNow();
}

void Knowthelist::player1_trackFinished()
{
    if (isFading)
        player1->stop();
    playList1->skipForward();
}

void Knowthelist::player2_trackFinished()
{
    if (isFading)
        player2->stop();
    playList2->skipForward();
}

void Knowthelist::player1_gainChanged(double gainValue)
{
    gain1Target = (int)(gainValue * 100.0);
    if (ui->toggleAGC->isChecked())
        timerGain1->start();
}

void Knowthelist::player2_gainChanged(double gainValue)
{
    gain2Target = (int)(gainValue * 100.0);
    if (ui->toggleAGC->isChecked())
        timerGain2->start();
}

// Move gain1 dial smoothly
void Knowthelist::timerGain1_timeOut()
{
    int gain1 = ui->potGain_1->value();
    if (gain1Target > gain1)
        ui->potGain_1->setValue(gain1 + 1);
    else if (gain1Target < gain1)
        ui->potGain_1->setValue(gain1 - 1);
    else
        timerGain1->stop();
}

// Move gain2 dial smoothly
void Knowthelist::timerGain2_timeOut()
{
    int gain2 = ui->potGain_2->value();
    if (gain2Target > gain2)
        ui->potGain_2->setValue(gain2 + 1);
    else if (gain2Target < gain2)
        ui->potGain_2->setValue(gain2 - 1);
    else
        timerGain2->stop();
}

void Knowthelist::fadeNow()
{
    //Fade now!
    if (!isFading && (playList1->countTrack() > 0 || playList2->countTrack() > 0)) {
        if (ui->sliFader->value() > 100) {
            m_xfadeDir = -1;
            if (!player1->isStarted())
                player1->play();
            //Fader has 200 steps * 5 = 1000ms
            timerAutoFader->start(mAutofadeLength * 5);
        } else {
            m_xfadeDir = 1;
            if (!player2->isStarted())
                player2->play();
            timerAutoFader->start(mAutofadeLength * 5);
        }

        isFading = true;

        //ToDo: search for a right time to save
        savePlaylists();
    }
}

void Knowthelist::changeVolumes()
{

    float v1 = ui->slider1->value() / 100.0;
    float v2 = ui->slider2->value() / 100.0;

    float f1 = 2 - ui->sliFader->value() / 100.0;
    float f2 = ui->sliFader->value() / 100.0;

    f1 = (f1 < 1) ? f1 : 1;
    f2 = (f2 < 1) ? f2 : 1;

    player1->setVolume(v1 * f1);
    player2->setVolume(v2 * f2);
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
    if (ui->sliFader->value() == ui->sliFader->minimum()) {
        playList1->setIsCurrentList(true);
        playList2->setIsCurrentList(false);
    }
    if (ui->sliFader->value() == ui->sliFader->maximum()) {
        playList2->setIsCurrentList(true);
        playList1->setIsCurrentList(false);
    }
}

void Knowthelist::timerAutoFader_timerOut()

{
    //Auto-Fader moves
    ui->sliFader->setValue(ui->sliFader->value() + m_xfadeDir);

    //Blinking
    if (ui->sliFader->value() % 3 == 0) {
        if (m_xfadeDir < 0)
            ui->ledFadeLeft->toggle();
        else
            ui->ledFadeRight->toggle();
    }

    if (ui->sliFader->value() <= ui->sliFader->minimum()) {
        //Fade from 2 to 1 is done
        timerAutoFader->stop();
        ui->ledFadeLeft->off();
        isFading = false;

        //ToDo:handle AutoDJ from Playlist
        if (player2->isStarted()) {
            player2->stop();
            playList2->skipForward();
        }
        if (ui->toggleAutoDJ->isChecked())
            djSession->updatePlaylists();
    }
    if (ui->sliFader->value() >= ui->sliFader->maximum()) {
        //Fade from 1 to 2 is done
        timerAutoFader->stop();
        isFading = false;
        ui->ledFadeRight->off();

        //ToDo:  handle AutoDJ from Playlist

        if (player1->isStarted()) {
            player1->stop();
            playList1->skipForward();
        }
        if (ui->toggleAutoDJ->isChecked())
            djSession->updatePlaylists();
    }
    changeVolumes();
}

void Knowthelist::savePlaylists()
{
    djSession->storePlaylists("defaultKnowthelist", true);
    //    playList1->saveXML( playList1->defaultPlaylistPath() );
    //    playList2->saveXML( playList2->defaultPlaylistPath() );
}

void Knowthelist::Track_selectionChanged(Track* track)
{
    if (track) {

        m_MonitorTrack = track;
        ui->lblMonitorArtist->setText(track->prettyArtist(20));
        ui->lblMonitorTrack->setText(track->prettyTitle(60));
        wantSeek = false;

        if (monitorPlayer) {
            on_cmdMonitorStop_clicked();
            monitorPlayer->open(track->url());
            QPixmap pix = QPixmap::fromImage(track->coverImage());
            if (!pix.isNull())
                ui->pixMonitorCover->setPixmap(pix);
            timerMonitor_timeOut();
        }
    } else {
        ui->lblMonitorTrack->setText("");
        ui->pixMonitorCover->setPixmap(QPixmap());
    }
}

void Knowthelist::timerMonitor_loadFinished()
{
    timerMonitor_timeOut();
    if (wantSeek) {
        on_sliMonitor_sliderMoved(100);
    }
}

void Knowthelist::Track_doubleClicked(Track* track)
{
    Track_selectionChanged(track);
    if (monitorPlayer) {
        wantSeek = true;
        on_cmdMonitorPlay_clicked();
    }
}

void Knowthelist::trackList_wantLoad(Track* track, QString target)
{
    //ToDo: enable for multiple tracks like drag/drop
    qDebug() << Q_FUNC_INFO << "target=" << target;
    if (target == "Right")
        playList2->appendSong(new Track(track->tagList()));
    else if (target == "Left")
        playList1->appendSong(new Track(track->tagList()));
}

//ToDo: find a better name
void Knowthelist::onWantLoad(QList<Track*> trackList, QString target)
{
    if (target == "Right")
        playList2->appendTracks(trackList);
    else if (target == "Left")
        playList1->appendTracks(trackList);
}

void Knowthelist::setFaderModeToPlayer()
{
    if (autoFadeOn) {
        player1->setTrackFinishEmitTime(mAboutFinishTime);
        player2->setTrackFinishEmitTime(mAboutFinishTime);
        playList1->setPlaylistMode(Playlist::Playlist_Multi);
        playList2->setPlaylistMode(Playlist::Playlist_Multi);
    } else {
        player1->setTrackFinishEmitTime(0);
        player2->setTrackFinishEmitTime(0);
        playList1->setPlaylistMode(Playlist::Playlist_Single);
        playList2->setPlaylistMode(Playlist::Playlist_Single);
    }
}

void Knowthelist::editSettings()
{
    // save DJ settings before change anything
    djBrowser->saveSettings();

    // update hardware infos
    monitorPlayer->readDevices();
    QSettings settings;
    settings.setValue("MonitorOutputDevices", monitorPlayer->outputDevices());

    if (preferences->exec() != QDialog::Rejected)
        loadCurrentSettings();
}

void Knowthelist::on_cmdFade_clicked()
{
    fadeNow();
}

bool Knowthelist::initMonitorPlayer()
{
    //ToDo: spend a separate widget for Monitor player
    qDebug() << Q_FUNC_INFO << "BEGIN ";

    monitorPlayer = new MonitorPlayer(this);
    monitorPlayer->prepare();
    monitorPlayer->setObjectName("monitorPlayer");

    ui->cmdMonitorStop->setIcon(QIcon(":stop.png"));
    ui->cmdMonitorPlay->setIcon(QIcon(":play.png"));
    connect(monitorPlayer, SIGNAL(loadFinished()), this, SLOT(timerMonitor_loadFinished()));

    qDebug() << Q_FUNC_INFO << "END ";
    return true;
}

void Knowthelist::startAutoDj()
{
    if (ui->toggleAutoDJ->isChecked())
        ui->toggleAutoDJ->setChecked(false);
    ui->toggleAutoDJ->setChecked(true);
}

void Knowthelist::on_cmdMonitorStop_clicked()
{
    monitorPlayer->stop();
    timerMonitor->stop();
    ui->cmdMonitorPlay->setIcon(QIcon(":play.png"));
    monitorMeter->setValueRight(0);
    monitorMeter->setValueLeft(0);
}

void Knowthelist::on_cmdMonitorPlay_clicked()
{
    if (monitorPlayer->isDisabled()) {
        return;
    }

    if (monitorPlayer->isPlaying()) {
        ui->cmdMonitorPlay->setIcon(QIcon(":play.png"));
        monitorPlayer->pause();
        timerMonitor->stop();
        monitorMeter->setValueRight(0);
        monitorMeter->setValueLeft(0);
    } else {
        ui->cmdMonitorPlay->setIcon(QIcon(":pause.png"));
        monitorPlayer->play();
        timerMonitor->start();
    }
}

void Knowthelist::monitorPlayer_trackTimeChanged(qint64 time, qint64 totalTime)
{
    //ToDo: delete this function: Why?
    if (ui->sliMonitor->maximum() != totalTime) {
        if (totalTime == 0)
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
    QTime remain(0, 0);
    long remainMs;

    //Some tracks deliver no length in state pause
    if (length == QTime(0, 0))
        length = QTime(0, 0, 0).addSecs(m_MonitorTrack->length());

    remainMs = curpos.msecsTo(length);
    remain = QTime(0, 0).addMSecs(remainMs);

    ui->lblMonitorPosition->setText(curpos.toString("mm:ss.zzz").left(7));
    ui->lblMonitorLength->setText(length.toString("mm:ss"));

    //update position slider
    if (length != QTime(0, 0))
        ui->sliMonitor->setValue(curpos.msecsTo(QTime(0, 0, 0)) * 1000 / length.msecsTo(QTime(0, 0, 0)));
    else
        ui->sliMonitor->setValue(0);

    monitorMeter->setValueLeft(monitorPlayer->levelLeft() * 1.0);
    monitorMeter->setValueRight(monitorPlayer->levelRight() * 1.0);
}

void Knowthelist::on_sliMonitor_sliderMoved(int value)
{
    uint length = -monitorPlayer->length().msecsTo(QTime(0, 0, 0));

    //Some tracks deliver no length in state pause
    if (length == 0)
        length = m_MonitorTrack->length() * 1000;

    if (length != 0 && value > 0) {
        QTime pos = QTime(0, 0, 0);
        pos = pos.addMSecs(length * (value / 1000.0));
        qDebug() << "pos:" << pos;
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
    if (checked) {
        // AutoDJ on

        // For an empty list
        if (playList1->isEmpty())
            playList1->addCurrentTrack(djSession->getRandomTrack());

        if (playList2->isEmpty())
            playList2->addCurrentTrack(djSession->getRandomTrack());

        // Fill both playlists
        djSession->updatePlaylists();

        // Start playing
        if (!player1->isStarted() && !player2->isStarted())
            fadeNow();

        // Activate Autofade
        ui->toggleAutoFade->setChecked(true);

        //ui->fr
    } else {
        m_AutoDJGenre = collectionBrowser->filterText();
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

void Knowthelist::on_lblSoundcard_linkActivated(const QString& link)
{
    Q_UNUSED(link);
    preferences->setCurrentTab(SettingsDialog::TabMonitor);
    editSettings();
}

void Knowthelist::on_sliMonitor_actionTriggered(int action)
{
    //a workaround for page moving
    int posi;
    switch (action) {
    case 3:
        posi = ui->sliMonitor->value() + 100;
        break;
    case 4:
        posi = ui->sliMonitor->value() - 100;
        if (posi < 100)
            posi = 1;
        break;
    case 1:
        posi = ui->sliMonitor->value() + 10;
        break;
    case 2:
        posi = ui->sliMonitor->value() - 10;
        break;
    default:
        return;
        break;
    }

    this->on_sliMonitor_sliderMoved(posi);
}

void Knowthelist::on_sliMonitorVolume_valueChanged(int value)
{
    monitorPlayer->setVolume(value / 100.0);
}
