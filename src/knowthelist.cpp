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
#include <QResizeEvent>
#include <QToolButton>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include <QMetaType>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <cmath>

namespace {
bool nearBeatBoundary(const QTime& position, const QTime& beatReference, int bpm, double toleranceMs)
{
    if (bpm <= 0)
        return true;

    const double beatMs = 60000.0 / static_cast<double>(bpm);
    if (beatMs <= 0.0)
        return true;

    const qint64 posMs = QTime(0, 0).msecsTo(position);
    const qint64 beatRefMs = beatReference.isValid() ? QTime(0, 0).msecsTo(beatReference) : 0;
    double phaseMs = std::fmod(static_cast<double>(posMs - beatRefMs), beatMs);
    if (phaseMs < 0.0)
        phaseMs += beatMs;
    const double distanceToBeat = qMin(phaseMs, beatMs - phaseMs);
    return distanceToBeat <= toleranceMs;
}

double beatPhaseDistanceMs(const QTime& lhsPos, const QTime& lhsBeatRef,
                           const QTime& rhsPos, const QTime& rhsBeatRef,
                           int bpm)
{
    if (bpm <= 0)
        return 0.0;

    const double beatMs = 60000.0 / static_cast<double>(bpm);
    if (beatMs <= 0.0)
        return 0.0;

    auto phaseFor = [beatMs](const QTime& pos, const QTime& beatRef) {
        const qint64 posMs = QTime(0, 0).msecsTo(pos);
        const qint64 beatRefMs = beatRef.isValid() ? QTime(0, 0).msecsTo(beatRef) : 0;
        double phaseMs = std::fmod(static_cast<double>(posMs - beatRefMs), beatMs);
        if (phaseMs < 0.0)
            phaseMs += beatMs;
        return phaseMs;
    };

    const double lhsPhase = phaseFor(lhsPos, lhsBeatRef);
    const double rhsPhase = phaseFor(rhsPos, rhsBeatRef);
    const double rawDelta = qAbs(lhsPhase - rhsPhase);
    return qMin(rawDelta, beatMs - rawDelta);
}
}

Knowthelist::Knowthelist(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::Knowthelist)
    , gain1Target(100)
    , gain2Target(100)
    , beatSyncWidget(nullptr)
    , timerBeatSyncVisual(nullptr)
    , m_Player1Bpm(0)
    , m_Player2Bpm(0)
    , m_rateRestoreTimer(nullptr)
    , m_rateRestorePlayer(nullptr)
    , m_toggleAutoSyncButton(nullptr)
    , m_toggleBeatVisualButton(nullptr)
    , m_autoSyncLed(nullptr)
    , m_monitorSettingsButton(nullptr)
    , m_autoSyncEnabled(true)
    , m_fadeSyncPhase(FadeSyncIdle)
    , m_fadeSyncOutgoingPlayer(nullptr)
    , m_fadeSyncIncomingPlayer(nullptr)
    , m_fadeSyncOutgoingBpm(0)
    , m_fadeSyncIncomingBpm(0)
    , m_fadeSyncStartTempoBpm(0.0)
    , m_fadeSyncTargetTempoBpm(0.0)
    , m_fadeSyncStep(0)
    , m_fadeSyncPreRollSteps(0)
    , m_fadeSyncCrossfadeSteps(0)
    , m_fadeSyncRestoreSteps(0)
    , m_fadeSyncTotalSteps(0)
    , m_fadeSyncWaitingBeatStart(false)
    , m_fadeSyncBeatWaitSteps(0)
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
    delete beatSyncWidget;
    beatSyncWidget = nullptr;
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

    // Allow top deck layouts to expand freely (ui file uses SetMaximumSize).
    if (QLayout* l = findChild<QLayout*>("horizontalLayout_2"))
        l->setSizeConstraint(QLayout::SetDefaultConstraint);
    if (QLayout* l = findChild<QLayout*>("verticalLayout"))
        l->setSizeConstraint(QLayout::SetDefaultConstraint);
    if (QLayout* l = findChild<QLayout*>("verticalLayout_3"))
        l->setSizeConstraint(QLayout::SetDefaultConstraint);

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
    // monitorPlayer is created later in initMonitorPlayer()

    timerAutoFader = new QTimer(this);
    connect(timerAutoFader, SIGNAL(timeout()), SLOT(timerAutoFader_timerOut()));

    m_rateRestoreTimer = new QTimer(this);
    m_rateRestoreTimer->setInterval(250);
    connect(m_rateRestoreTimer, &QTimer::timeout, this, &Knowthelist::timerRateRestore_timeOut);

    m_toggleAutoSyncButton = new QPushButton(ui->frameMixer);
    m_toggleAutoSyncButton->setObjectName("toggleAutoSync");
    m_toggleAutoSyncButton->setGeometry(QRect(123, 314, 40, 18));
    m_toggleAutoSyncButton->setMinimumSize(QSize(16, 16));
    m_toggleAutoSyncButton->setPalette(ui->toggleAutoFade->palette());
    m_toggleAutoSyncButton->setFont(ui->toggleAutoFade->font());
    m_toggleAutoSyncButton->setCheckable(true);
    m_toggleAutoSyncButton->setText(tr("Sync"));
    m_toggleAutoSyncButton->setToolTip(tr("Enable BPM sync for the next auto fade"));
    connect(m_toggleAutoSyncButton, &QPushButton::toggled, this, &Knowthelist::on_toggleAutoSync_toggled);

    m_autoSyncLed = new QLed(ui->frameMixer);
    m_autoSyncLed->setObjectName("ledSync");
    m_autoSyncLed->setGeometry(QRect(138, 307, 12, 7));
    m_autoSyncLed->setLook(QLed::Flat);
    m_autoSyncLed->setShape(QLed::Rectangular);
    m_autoSyncLed->setColor(QColor(35, 119, 246));
    m_autoSyncLed->setToolTip(tr("Sync LED: ON = next fade will be BPM synced"));
    m_autoSyncLed->off();

    m_toggleBeatVisualButton = new QPushButton(ui->frameMixer);
    m_toggleBeatVisualButton->setObjectName("toggleBeatVisual");
    m_toggleBeatVisualButton->setGeometry(QRect(125, 335, 40, 18));
    m_toggleBeatVisualButton->setMinimumSize(QSize(16, 16));
    m_toggleBeatVisualButton->setPalette(ui->toggleAutoFade->palette());
    m_toggleBeatVisualButton->setFont(ui->toggleAutoFade->font());
    m_toggleBeatVisualButton->setCheckable(true);
    m_toggleBeatVisualButton->setText(tr("VU"));
    connect(m_toggleBeatVisualButton, &QPushButton::toggled, this, &Knowthelist::on_toggleBeatVisual_toggled);

    ui->cmdOptions->setGeometry(QRect(32, 335, 40, 18));
    ui->cmdOptions->setMinimumWidth(16);
    ui->cmdOptions->setMaximumWidth(48);
    ui->cmdOptions->setIcon(QIcon(":settings.png"));
    ui->cmdOptions->setIconSize(QSize(14, 14));

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

    m_monitorSettingsButton = new QToolButton(ui->fraMonitorTop);
    m_monitorSettingsButton->setObjectName("cmdMonitorSettings");
    m_monitorSettingsButton->setGeometry(QRect(150, 0, 23, 20)); // repositioned in showEvent
    m_monitorSettingsButton->setIcon(QIcon(":settings.png"));
    m_monitorSettingsButton->setToolTip(tr("Monitor output settings"));
    m_monitorSettingsButton->setAutoRaise(true);
    connect(m_monitorSettingsButton, &QToolButton::clicked, this, &Knowthelist::on_cmdMonitorSettings_clicked);

    // Dedicated mixer beat-sync visualizer (switchable from settings).
    beatSyncWidget = new BeatSyncWidget(ui->frameMixer);
    const QRect beatRect = ui->phVU1->geometry().united(ui->phVU2->geometry()).adjusted(-2, -2, 2, 2);
    beatSyncWidget->setGeometry(beatRect);
    beatSyncWidget->hide();

    ui->potGain_1->setRange(1, 180);
    ui->potGain_1->setValue(100);
    ui->potGain_2->setRange(1, 180);
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

    timerBeatSyncVisual = new QTimer(this);
    timerBeatSyncVisual->setInterval(33);
    connect(timerBeatSyncVisual, SIGNAL(timeout()), SLOT(timerBeatSyncVisual_timeOut()));

    qRegisterMetaType<QList<Track*>>("QList<Track*>");

    //Add DJ
    djSession = new DjSession();

    playList1 = ui->playlist_L;
    playList1->setIsCurrentList(true);

    playList2 = ui->playlist_R;
    playList2->setIsCurrentList(false);

    connect(playList1, SIGNAL(currentTrackChanged(Track*)), player1, SLOT(loadTrack(Track*)));
    connect(playList2, SIGNAL(currentTrackChanged(Track*)), player2, SLOT(loadTrack(Track*)));
    connect(playList1, SIGNAL(currentTrackChanged(Track*)), SLOT(playlist1_currentTrackChanged(Track*)));
    connect(playList2, SIGNAL(currentTrackChanged(Track*)), SLOT(playlist2_currentTrackChanged(Track*)));
    connect(playList1, SIGNAL(trackPropertyChanged(Track*)), player1, SLOT(onTrackPropertyChanged(Track*)));
    connect(playList2, SIGNAL(trackPropertyChanged(Track*)), player2, SLOT(onTrackPropertyChanged(Track*)));
    connect(playList1, SIGNAL(trackPropertyChanged(Track*)), djSession, SLOT(onTrackPropertyChanged(Track*)));
    connect(playList2, SIGNAL(trackPropertyChanged(Track*)), djSession, SLOT(onTrackPropertyChanged(Track*)));

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
    connect(player1, SIGNAL(tempoChanged(int, QTime)), SLOT(player1_tempoChanged(int, QTime)));
    connect(player2, SIGNAL(tempoChanged(int, QTime)), SLOT(player2_tempoChanged(int, QTime)));
    connect(player1, &PlayerWidget::syncRequested, this, &Knowthelist::player1_syncRequested);
    connect(player2, &PlayerWidget::syncRequested, this, &Knowthelist::player2_syncRequested);
    connect(player1, &PlayerWidget::monitorRouteToggled, this, &Knowthelist::player1_monitorRouteToggled);
    connect(player2, &PlayerWidget::monitorRouteToggled, this, &Knowthelist::player2_monitorRouteToggled);

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

    connect(collectionBrowser, SIGNAL(tracksSelected(QList<Track*>)), trackList, SLOT(changeTracks(QList<Track*>)));
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

    // Keep mixer/monitor sliders neutral and avoid platform accent color stripes.
#if defined(Q_OS_LINUX) || defined(Q_OS_DARWIN)
    QString sliderStyle = QString(
        "QSlider { background: transparent; }"
        "QSlider::handle:horizontal {"
        "   background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "      stop: 0 #000, stop: 0.1 #222, stop: 0.38 #444, stop:0.5 #ccc,"
        "      stop:0.6 #444, stop:0.9 #222, stop:1 #000 );"
        "   border: 1px solid #5c5c5c; width: 18px; margin: 1px 0; border-radius: 3px; }"
        "QSlider::handle:vertical {"
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "      stop: 0 #000, stop: 0.1 #222, stop: 0.38 #444, stop:0.5 #ccc,"
        "      stop:0.6 #444, stop:0.9 #222, stop:1 #000 );"
        "   border: 1px solid #5c5c5c; width: 12px; min-height: 15px; margin: 0 2px; border-radius: 3px; }"
        "QSlider::sub-page:vertical { background: qlineargradient(x1: 0, y1: 0, x2:1, y2: 0,"
        "   stop: 0.4 #666, stop: 0 #111111 ); border: 1px solid #444; border-radius: 2px;}"
        "QSlider::add-page:vertical {background: qlineargradient(x1: 0, y1: 0, x2:1, y2: 0,"
        "   stop: 0 #111,stop: 0.4 #666); border: 1px solid #333; border-radius: 2px;}"
        "QSlider::sub-page:horizontal,QSlider::add-page:horizontal  {"
        "   background: qlineargradient(x1: 0, y1: 0,    x2: 0, y2: 1,"
        "   stop: 0 #111, stop: 0.6 #666 ); border: 1px solid #222; border-radius: 2px;}");
        // Note: QSlider::groove must NOT be styled here. Styling the groove puts Qt into its
        // full CSS rendering path which disables native tick-mark drawing. The sliders have
        // tickPosition=TicksBothSides set in the .ui file; tick rendering requires the native
        // (non-groove-CSS) path. Handle + page colours are safe to style without breaking ticks.

    ui->frameMixer->setStyleSheet(sliderStyle);
    ui->MonitorPlayer->setStyleSheet(sliderStyle);
#endif

    //Add the AutoDJ Browser
    djBrowser = new DjBrowser();
    QPixmap pixmap2(":DJ.png");
    ui->sideTab->AddTab(djBrowser, QIcon(pixmap2), tr("AutoDJ"));
    ui->sideTab->setContextMenuPolicy(Qt::NoContextMenu);
    connect(djBrowser, SIGNAL(selectionChanged(Dj*)), djSession, SLOT(setCurrentDj(Dj*)));
    connect(djBrowser, SIGNAL(selectionChanged(Dj*)), this, SLOT(currentDjChanged(Dj*)));
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
    connect(preferences, SIGNAL(resetAnalysisCachePressed()), this, SLOT(on_resetAnalysisCachePressed()));

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

    //if (settings.value("loadPlaylists", "true") == "true") {
        djSession->playDefaultList();
    //}

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
    ui->sliMonitorVolume->setValue(settings.value("VolumeMonitor", 70).toInt());
}

void Knowthelist::setPlayer1BeatSyncEnabled(bool enabled)
{
    player1->setBeatSyncEnabled(enabled);
    if (enabled) {
        player2->setBeatSyncEnabled(false);
    }
}

void Knowthelist::setPlayer2BeatSyncEnabled(bool enabled)
{
    player2->setBeatSyncEnabled(enabled);
    if (enabled) {
        player1->setBeatSyncEnabled(false);
    }
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

    updatePlayerMonitorRouting();

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

    //Beat sync and BPM analysis settings
    const bool beatSyncEnabled = settings.value("beatSyncEnabled", true).toBool();
    const bool beatCueEnabled = settings.value("beatSyncCueEnabled", true).toBool();
    const bool beatVisualMode = settings.value("beatSyncVisualMode", false).toBool();
    player1->setBeatSyncEnabled(beatSyncEnabled);
    player2->setBeatSyncEnabled(beatSyncEnabled);
    player1->setBeatCueEnabled(beatCueEnabled);
    player2->setBeatCueEnabled(beatCueEnabled);
    m_autoSyncEnabled = settings.value("autoSyncEnabled", beatSyncEnabled).toBool();
    if (m_toggleAutoSyncButton)
        m_toggleAutoSyncButton->setChecked(m_autoSyncEnabled);
    else
        applyAutoSyncEnabled(m_autoSyncEnabled);
    if (m_toggleBeatVisualButton)
        m_toggleBeatVisualButton->setChecked(beatVisualMode);
    else
        applyBeatVisualMode(beatVisualMode);

    // Mixer VU is always visible. Beat visual mode now affects deck widgets only.
    timerBeatSyncVisual->stop();
    beatSyncWidget->hide();
    vuMeter1->show();
    vuMeter2->show();

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

void Knowthelist::on_resetAnalysisCachePressed()
{
    qDebug() << Q_FUNC_INFO << "Reset analysis cache requested";
    // Invalidate in-flight analyzers first so they cannot repopulate cache rows
    // while reset is in progress.
    TrackAnalyzer::clearRuntimeCaches();

    QSqlDatabase db = QSqlDatabase::database("CollectionDB");
    if (!db.isValid() || !db.isOpen()) {
        QMessageBox::warning(this, tr("Reset analysis cache"), tr("The collection database is not open."));
        return;
    }

    QSqlQuery query(db);
    if (!query.exec("DELETE FROM analysis_cache;")) {
        qDebug() << Q_FUNC_INFO << "Reset analysis cache failed:" << query.lastError().text();
        QMessageBox::warning(this,
                             tr("Reset analysis cache"),
                             tr("Failed to clear analysis cache: %1").arg(query.lastError().text()));
        return;
    }

    qDebug() << Q_FUNC_INFO << "Reset analysis cache completed";

    QMessageBox::information(this,
                             tr("Reset analysis cache"),
                             tr("All analysis cache data has been cleared."));
}

void Knowthelist::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // Keep the DJ name label pinned to the bottom of the mixer frame
    const int mixerH = ui->frameMixer->height();
    const int labelMargin = 4;
    ui->lblDjName->setGeometry(4, mixerH - ui->lblDjName->height() - labelMargin,
                               186, ui->lblDjName->height());
    // Reposition monitor settings button to bottom-right of fraMonitorTop
    if (m_monitorSettingsButton) {
        const QRect fra = ui->fraMonitorTop->rect();
        m_monitorSettingsButton->setGeometry(
            fra.width() - 27, fra.height() - 24, 23, 20);
    }
}

void Knowthelist::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    // Defer until the layout has settled so geometry() values are correct.
    QTimer::singleShot(0, this, [this]() {
        const int mixerH = ui->frameMixer->height();
        if (mixerH > 0) {
            constexpr int labelMargin = 4;
            ui->lblDjName->setGeometry(4, mixerH - ui->lblDjName->height() - labelMargin,
                                       186, ui->lblDjName->height());
        }
        if (m_monitorSettingsButton) {
            const QRect fra = ui->fraMonitorTop->rect();
            m_monitorSettingsButton->setGeometry(
                fra.width() - 27, fra.height() - 24, 23, 20);
        }
    });
}

void Knowthelist::closeEvent(QCloseEvent* event)
{
    qDebug() << Q_FUNC_INFO << "for Knowthelist";

    QSettings settings;
    settings.setValue("Volume1", QString("%1").arg(ui->slider1->value()));
    settings.setValue("Volume2", QString("%1").arg(ui->slider2->value()));
    settings.setValue("VolumeMonitor", QString("%1").arg(ui->sliMonitorVolume->value()));
    settings.setValue("autoSyncEnabled", m_autoSyncEnabled);
    if (m_toggleBeatVisualButton)
        settings.setValue("beatSyncVisualMode", m_toggleBeatVisualButton->isChecked());

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
    vuMeter1->setValueLeft(left);
    vuMeter1->setValueRight(right);
}

void Knowthelist::player2_levelChanged(double left, double right)
{
    vuMeter2->setValueLeft(left);
    vuMeter2->setValueRight(right);
}

void Knowthelist::timerBeatSyncVisual_timeOut()
{
    if (!beatSyncWidget)
        return;

    BeatSyncWidget::DeckState deck1;
    deck1.bpm = m_Player1Bpm;
    deck1.running = player1->isStarted();
    deck1.position = player1->currentPosition();
    deck1.beatReference = m_Player1BeatPosition;
    beatSyncWidget->setDeck1(deck1);

    BeatSyncWidget::DeckState deck2;
    deck2.bpm = m_Player2Bpm;
    deck2.running = player2->isStarted();
    deck2.position = player2->currentPosition();
    deck2.beatReference = m_Player2BeatPosition;
    beatSyncWidget->setDeck2(deck2);
}

void Knowthelist::player1_tempoChanged(int bpm, QTime beatPosition)
{
    m_Player1Bpm = bpm;
    m_Player1BeatPosition = beatPosition;

    if (!m_autoSyncEnabled || bpm <= 0)
        return;

    // Deck A analysis done: align whichever deck is waiting to the running deck.
    if (player1->isStarted() && !player2->isStarted()) {
        // A running, B waiting → pre-cue B to A
        player2->setTempoRate(1.0);
        player2->setSyncAdopting(false);
        player2->alignCueToReferenceBeat(bpm, player1->currentPosition(), beatPosition);
    }
    else if (!player1->isStarted() && player2->isStarted() && m_Player2Bpm > 0) {
        // A waiting, B running → pre-cue A to B
        player1->setTempoRate(1.0);
        player1->setSyncAdopting(false);
        player1->alignCueToReferenceBeat(m_Player2Bpm, player2->currentPosition(), m_Player2BeatPosition);
    }
}

void Knowthelist::player2_tempoChanged(int bpm, QTime beatPosition)
{
    m_Player2Bpm = bpm;
    m_Player2BeatPosition = beatPosition;

    if (!m_autoSyncEnabled || bpm <= 0)
        return;

    // Deck B analysis done: align whichever deck is waiting to the running deck.
    if (player2->isStarted() && !player1->isStarted()) {
        // B running, A waiting → pre-cue A to B
        player1->setTempoRate(1.0);
        player1->setSyncAdopting(false);
        player1->alignCueToReferenceBeat(bpm, player2->currentPosition(), beatPosition);
    }
    else if (!player2->isStarted() && player1->isStarted() && m_Player1Bpm > 0) {
        // B waiting, A running → pre-cue B to A
        player2->setTempoRate(1.0);
        player2->setSyncAdopting(false);
        player2->alignCueToReferenceBeat(m_Player1Bpm, player1->currentPosition(), m_Player1BeatPosition);
    }
}

void Knowthelist::player1_syncRequested()
{
    // If player2 is already sync active, turn it off
    if (player2->getSyncButton()->isChecked()) {
        // Turn off player2's sync by toggling the sync button
        player2->getSyncButton()->setChecked(false);
    }

    // SYNC pressed on deck A: snap deck A's beat phase to deck B (the master).
    if (m_Player2Bpm <= 0 || !player2->isStarted()) {
        player1->setSyncActive(false);
        return;
    }
    
    player1->syncNowToReferenceBeat(m_Player2Bpm, player2->currentPosition(), m_Player2BeatPosition);
}

void Knowthelist::player2_syncRequested()
{
    // If player1 is already sync active, turn it off
    if (player1->getSyncButton()->isChecked()) {
        // Turn off player1's sync by toggling the sync button
        player1->getSyncButton()->setChecked(false);
    }

    // SYNC pressed on deck B: snap deck B's beat phase to deck A (the master).
    if (m_Player1Bpm <= 0 || !player1->isStarted()) {
        player2->setSyncActive(false);
        return;
    }
    
    player2->syncNowToReferenceBeat(m_Player1Bpm, player1->currentPosition(), m_Player1BeatPosition);
}

void Knowthelist::player_aboutTrackFinished()
{
    if (ui->toggleAutoFade->isChecked())
        fadeNow();
}

void Knowthelist::on_playerSyncButtonToggled(bool checked)
{
    // Handle mutual exclusion between player sync buttons
    // Only one player should be in sync mode at a time
    QObject* sender = QObject::sender();
    if (sender == player1) {
        // If player1 sync is activated, deactivate player2 sync
        if (checked && player2->getSyncButton()->isChecked()) {
            player2->setSyncActive(false);
        }
    } else if (sender == player2) {
        // If player2 sync is activated, deactivate player1 sync
        if (checked && player1->getSyncButton()->isChecked()) {
            player1->setSyncActive(false);
        }
    }
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

void Knowthelist::beginPlainFade(PlayerWidget* incoming)
{
    clearAutoFadeSyncState();
    m_rateRestoreTimer->stop();
    m_rateRestorePlayer = nullptr;
    if (incoming && !incoming->isStarted())
        incoming->play();
    timerAutoFader->start(mAutofadeLength * 5);
}

void Knowthelist::beginAutoFadeSync(PlayerWidget* outgoing, PlayerWidget* incoming,
                                    int outgoingBpm, int incomingBpm,
                                    const QTime& outgoingBeatPosition)
{
    clearAutoFadeSyncState();
    m_rateRestoreTimer->stop();
    m_rateRestorePlayer = nullptr;

    m_fadeSyncPhase = FadeSyncPreRoll;
    m_fadeSyncOutgoingPlayer = outgoing;
    m_fadeSyncIncomingPlayer = incoming;
    m_fadeSyncOutgoingBpm = outgoingBpm;
    m_fadeSyncIncomingBpm = incomingBpm;
    m_fadeSyncOutgoingBeatPosition = outgoingBeatPosition;
    m_fadeSyncStartTempoBpm = qMax(1.0, static_cast<double>(outgoingBpm) * outgoing->tempoRate());
    m_fadeSyncTargetTempoBpm = selectAutoFadeTargetTempo(m_fadeSyncStartTempoBpm, outgoingBpm, incomingBpm);
    m_fadeSyncCrossfadeSteps = qMax(1, qAbs((m_xfadeDir < 0 ? ui->sliFader->minimum() : ui->sliFader->maximum()) - ui->sliFader->value()));
    m_fadeSyncPreRollSteps = qMax(12, m_fadeSyncCrossfadeSteps / 4);
    m_fadeSyncRestoreSteps = qMax(12, m_fadeSyncCrossfadeSteps / 4);
    m_fadeSyncTotalSteps = m_fadeSyncPreRollSteps + m_fadeSyncCrossfadeSteps + m_fadeSyncRestoreSteps;
    m_fadeSyncStep = 0;
    m_fadeSyncWaitingBeatStart = true;
    m_fadeSyncBeatWaitSteps = 0;

    outgoing->setSyncAdopting(true);
    incoming->setSyncAdopting(false);
    timerAutoFader->start(mAutofadeLength * 5);
}

double Knowthelist::selectAutoFadeTargetTempo(double startTempoBpm, int outgoingBpm, int incomingBpm) const
{
    const double baseTarget = qMax(1.0, static_cast<double>(incomingBpm));

    if (outgoingBpm <= 0 || incomingBpm <= 0)
        return baseTarget;

    // Evaluate three candidate meeting tempos:
    //   (1) direct – incoming at its native BPM
    //   (2) half   – incoming at 0.5× (2:1 half-tempo relationship)
    //   (3) double – incoming at 2×  (1:2 relationship)
    // For each candidate check that both decks stay within hardware rate limits [0.5, 2.0].
    // Among the feasible candidates pick the one with the smallest TOTAL log-rate deviation
    // across both decks. This avoids "fixing" the outgoing deck by forcing the incoming deck
    // to an extreme playback rate (which was the 104←160 → target=208 bug).
    const double candidates[3] = { baseTarget, baseTarget * 0.5, baseTarget * 2.0 };

    double bestTempo = -1.0;
    double bestDeviation = std::numeric_limits<double>::max();

    for (int i = 0; i < 3; ++i) {
        const double candidate = candidates[i];
        if (candidate < 10.0)
            continue;
        const double outRate = candidate / static_cast<double>(outgoingBpm);
        const double inRate  = candidate / static_cast<double>(incomingBpm);
        if (outRate < 0.5 || outRate > 2.0 || inRate < 0.5 || inRate > 2.0)
            continue;

        const double deviation = qAbs(std::log(outRate)) + qAbs(std::log(inRate));
        if (deviation < bestDeviation - 0.01) {
            bestDeviation = deviation;
            bestTempo = candidate;
        }
    }

    return qMax(1.0, bestTempo > 0.0 ? bestTempo : baseTarget);
}

double Knowthelist::autoFadeSharedTempoForStep(int step) const
{
    if (m_fadeSyncTotalSteps <= 0)
        return m_fadeSyncTargetTempoBpm;

    double progress = static_cast<double>(step) / static_cast<double>(m_fadeSyncTotalSteps);
    progress = qBound(0.0, progress, 1.0);

    const double safeStart = qMax(1.0, m_fadeSyncStartTempoBpm);
    const double safeTarget = qMax(1.0, m_fadeSyncTargetTempoBpm);
    const double ratioDistance = qAbs(std::log(safeTarget / safeStart));
    const double curvePower = 1.15 + qBound(0.0, (ratioDistance - 0.08) * 3.2, 1.35);
    const double easedProgress = std::pow(progress, curvePower);
    return std::exp(std::log(safeStart)
                    + (std::log(safeTarget) - std::log(safeStart)) * easedProgress);
}

void Knowthelist::applyAutoFadeSharedTempo(double sharedTempoBpm)
{
    if (m_fadeSyncOutgoingPlayer && m_fadeSyncOutgoingBpm > 0)
        m_fadeSyncOutgoingPlayer->setTempoRate(sharedTempoBpm / static_cast<double>(m_fadeSyncOutgoingBpm));
    if (m_fadeSyncIncomingPlayer && m_fadeSyncIncomingBpm > 0)
        m_fadeSyncIncomingPlayer->setTempoRate(sharedTempoBpm / static_cast<double>(m_fadeSyncIncomingBpm));

    if (m_fadeSyncOutgoingPlayer)
        m_fadeSyncOutgoingPlayer->setSyncAdopting(m_fadeSyncPhase == FadeSyncPreRoll || m_fadeSyncPhase == FadeSyncCrossfade);
    if (m_fadeSyncIncomingPlayer)
        m_fadeSyncIncomingPlayer->setSyncAdopting(m_fadeSyncPhase == FadeSyncCrossfade || m_fadeSyncPhase == FadeSyncRestore);
}

void Knowthelist::clearAutoFadeSyncState()
{
    if (m_fadeSyncOutgoingPlayer)
        m_fadeSyncOutgoingPlayer->setSyncAdopting(false);
    if (m_fadeSyncIncomingPlayer)
        m_fadeSyncIncomingPlayer->setSyncAdopting(false);

    m_fadeSyncPhase = FadeSyncIdle;
    m_fadeSyncOutgoingPlayer = nullptr;
    m_fadeSyncIncomingPlayer = nullptr;
    m_fadeSyncOutgoingBpm = 0;
    m_fadeSyncIncomingBpm = 0;
    m_fadeSyncOutgoingBeatPosition = QTime();
    m_fadeSyncStartTempoBpm = 0.0;
    m_fadeSyncTargetTempoBpm = 0.0;
    m_fadeSyncStep = 0;
    m_fadeSyncPreRollSteps = 0;
    m_fadeSyncCrossfadeSteps = 0;
    m_fadeSyncRestoreSteps = 0;
    m_fadeSyncTotalSteps = 0;
    m_fadeSyncWaitingBeatStart = false;
    m_fadeSyncBeatWaitSteps = 0;
}

void Knowthelist::resetWaitingDeckTempoPreviews()
{
    if (!player1->isStarted()) {
        player1->resetSyncState();
    }
    if (!player2->isStarted()) {
        player2->resetSyncState();
    }
}

void Knowthelist::resetAllDecksSyncState()
{
    // Use timer-based gradual tempo restoration instead of immediate tempo change
    // to provide smoother listening experience during sync resets
    
    // For player1 - set up for gradual tempo restoration if player is running
    if (player1 && player1->isStarted()) {
        m_rateRestorePlayer = player1;
        m_rateRestoreTimer->start();
    } else if (player1) {
        // Player not started, reset immediately 
        player1->resetSyncState();
    }
    
    // For player2 - set up for gradual tempo restoration if player is running
    if (player2 && player2->isStarted()) {
        m_rateRestorePlayer = player2;
        m_rateRestoreTimer->start();
    } else if (player2) {
        // Player not started, reset immediately
        player2->resetSyncState();
    }
}

void Knowthelist::applyBeatVisualMode(bool enabled)
{
    player1->setBeatVisualMode(enabled);
    player2->setBeatVisualMode(enabled);
    if (m_toggleBeatVisualButton)
        m_toggleBeatVisualButton->setText(enabled ? tr("BPM") : tr("VU"));
}

void Knowthelist::applyAutoSyncEnabled(bool enabled)
{
    m_autoSyncEnabled = enabled;
    if (m_autoSyncLed)
        m_autoSyncLed->setState(enabled ? QLed::On : QLed::Off);
    if (!enabled) {
        clearAutoFadeSyncState();
        resetWaitingDeckTempoPreviews();
    }
}

void Knowthelist::fadeNow()
{
    //Fade now!
    if (!isFading && (playList1->countTrack() > 0 || playList2->countTrack() > 0)) {
        PlayerWidget* incoming = nullptr;
        PlayerWidget* outgoing = nullptr;
        int incomingBpm = 0;
        int outgoingBpm = 0;
        QTime outgoingBeatPosition;

        if (ui->sliFader->value() > 100) {
            m_xfadeDir = -1;
            incoming = player1;
            outgoing = player2;
            incomingBpm = m_Player1Bpm;
            outgoingBpm = m_Player2Bpm;
            outgoingBeatPosition = m_Player2BeatPosition;
        } else {
            m_xfadeDir = 1;
            incoming = player2;
            outgoing = player1;
            incomingBpm = m_Player2Bpm;
            outgoingBpm = m_Player1Bpm;
            outgoingBeatPosition = m_Player1BeatPosition;
        }

        const bool useAutoSync = ui->toggleAutoFade->isChecked()
            && m_autoSyncEnabled
            && outgoing && incoming
            && outgoing->isStarted()
            && !incoming->isStarted()
            && outgoingBpm > 0 && incomingBpm > 0
            && outgoing->supportsSmoothTempo()
            && incoming->supportsSmoothTempo();

        if (useAutoSync)
            beginAutoFadeSync(outgoing, incoming, outgoingBpm, incomingBpm, outgoingBeatPosition);
        else
            beginPlainFade(incoming);

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
    if (m_fadeSyncPhase == FadeSyncPreRoll) {
        m_fadeSyncStep = qMin(m_fadeSyncTotalSteps, m_fadeSyncStep + 1);
        applyAutoFadeSharedTempo(autoFadeSharedTempoForStep(m_fadeSyncStep));

        if (m_fadeSyncStep >= m_fadeSyncPreRollSteps && m_fadeSyncIncomingPlayer) {
            const int sharedTempoBpm = qMax(1, qRound(autoFadeSharedTempoForStep(m_fadeSyncStep)));
            m_fadeSyncIncomingPlayer->alignCueToReferenceBeat(sharedTempoBpm,
                                                              m_fadeSyncOutgoingPlayer->currentPosition(),
                                                              m_fadeSyncOutgoingBeatPosition);
            m_fadeSyncIncomingPlayer->setTempoRate(sharedTempoBpm / static_cast<double>(m_fadeSyncIncomingBpm));

            const double beatMs = 60000.0 / static_cast<double>(sharedTempoBpm);
            const double toleranceMs = qMin(60.0, beatMs * 0.12);
            const bool onBeat = nearBeatBoundary(m_fadeSyncOutgoingPlayer->currentPosition(),
                                                 m_fadeSyncOutgoingBeatPosition,
                                                 sharedTempoBpm,
                                                 toleranceMs);

            if (onBeat || !m_fadeSyncWaitingBeatStart) {
                if (!m_fadeSyncIncomingPlayer->isStarted())
                    m_fadeSyncIncomingPlayer->play();
                m_fadeSyncIncomingPlayer->syncNowToReferenceBeat(sharedTempoBpm,
                                                                 m_fadeSyncOutgoingPlayer->currentPosition(),
                                                                 m_fadeSyncOutgoingBeatPosition);
                m_fadeSyncPhase = FadeSyncCrossfade;
                m_fadeSyncWaitingBeatStart = false;
                m_fadeSyncBeatWaitSteps = 0;
            }
        }
        return;
    }

    if (m_fadeSyncPhase == FadeSyncCrossfade) {
        {
            QSignalBlocker blocker(ui->sliFader);
            ui->sliFader->setValue(ui->sliFader->value() + m_xfadeDir);
        }
        m_fadeSyncStep = qMin(m_fadeSyncTotalSteps, m_fadeSyncStep + 1);
        applyAutoFadeSharedTempo(autoFadeSharedTempoForStep(m_fadeSyncStep));

        if (m_fadeSyncIncomingPlayer && m_fadeSyncOutgoingPlayer
            && m_fadeSyncIncomingPlayer->isStarted() && m_fadeSyncBeatWaitSteps < 4) {
            const int sharedTempoBpm = qMax(1, qRound(autoFadeSharedTempoForStep(m_fadeSyncStep)));
            const QTime incomingBeatPosition = (m_fadeSyncIncomingPlayer == player1)
                                                   ? m_Player1BeatPosition
                                                   : m_Player2BeatPosition;
            const double beatMs = 60000.0 / static_cast<double>(sharedTempoBpm);
            const double toleranceMs = qMin(18.0, beatMs * 0.04);
            const double phaseDeltaMs = beatPhaseDistanceMs(m_fadeSyncIncomingPlayer->currentPosition(),
                                                            incomingBeatPosition,
                                                            m_fadeSyncOutgoingPlayer->currentPosition(),
                                                            m_fadeSyncOutgoingBeatPosition,
                                                            sharedTempoBpm);
            if (phaseDeltaMs > toleranceMs) {
                m_fadeSyncIncomingPlayer->syncNowToReferenceBeat(sharedTempoBpm,
                                                                 m_fadeSyncOutgoingPlayer->currentPosition(),
                                                                 m_fadeSyncOutgoingBeatPosition);
                ++m_fadeSyncBeatWaitSteps;
            } else {
                m_fadeSyncBeatWaitSteps = 4;
            }
        }

        if (ui->sliFader->value() % 3 == 0) {
            if (m_xfadeDir < 0)
                ui->ledFadeLeft->toggle();
            else
                ui->ledFadeRight->toggle();
        }

        const bool crossedLeft = ui->sliFader->value() <= ui->sliFader->minimum();
        const bool crossedRight = ui->sliFader->value() >= ui->sliFader->maximum();
        if (crossedLeft || crossedRight) {
            if (crossedLeft)
                ui->ledFadeLeft->off();
            if (crossedRight)
                ui->ledFadeRight->off();

            PlayerWidget* finishedPlayer = m_fadeSyncOutgoingPlayer;
            if (finishedPlayer == player1) {
                if (player1->isStarted())
                    player1->stop();
                playList1->skipForward();
            } else if (finishedPlayer == player2) {
                if (player2->isStarted())
                    player2->stop();
                playList2->skipForward();
            }

            if (ui->toggleAutoDJ->isChecked())
                djSession->updatePlaylists();

            m_fadeSyncPhase = FadeSyncRestore;
            if (m_fadeSyncStep >= m_fadeSyncTotalSteps || !m_fadeSyncIncomingPlayer || !m_fadeSyncIncomingPlayer->isStarted()) {
                if (m_fadeSyncIncomingPlayer)
                    m_fadeSyncIncomingPlayer->setTempoRate(1.0);
                timerAutoFader->stop();
                resetWaitingDeckTempoPreviews();
                clearAutoFadeSyncState();
                isFading = false;
            }
        }
        changeVolumes();
        return;
    }

    if (m_fadeSyncPhase == FadeSyncRestore) {
        m_fadeSyncStep = qMin(m_fadeSyncTotalSteps, m_fadeSyncStep + 1);
        applyAutoFadeSharedTempo(autoFadeSharedTempoForStep(m_fadeSyncStep));

        if (m_fadeSyncStep >= m_fadeSyncTotalSteps || !m_fadeSyncIncomingPlayer || !m_fadeSyncIncomingPlayer->isStarted()) {
            if (m_fadeSyncIncomingPlayer)
                m_fadeSyncIncomingPlayer->setTempoRate(1.0);
            timerAutoFader->stop();
            resetWaitingDeckTempoPreviews();
            clearAutoFadeSyncState();
            isFading = false;
        }
        return;
    }

    // Plain crossfade without auto-sync.
    {
        QSignalBlocker blocker(ui->sliFader);
        ui->sliFader->setValue(ui->sliFader->value() + m_xfadeDir);
    }

    if (ui->sliFader->value() % 3 == 0) {
        if (m_xfadeDir < 0)
            ui->ledFadeLeft->toggle();
        else
            ui->ledFadeRight->toggle();
    }

    if (ui->sliFader->value() <= ui->sliFader->minimum()) {
        timerAutoFader->stop();
        ui->ledFadeLeft->off();
        isFading = false;

        if (player2->isStarted())
            player2->stop();
        playList2->skipForward();
        if (ui->toggleAutoDJ->isChecked())
            djSession->updatePlaylists();

        resetAllDecksSyncState();
    }
    if (ui->sliFader->value() >= ui->sliFader->maximum()) {
        timerAutoFader->stop();
        isFading = false;
        ui->ledFadeRight->off();
   
        if (player1->isStarted())
            player1->stop();
        playList1->skipForward();
        if (ui->toggleAutoDJ->isChecked())
            djSession->updatePlaylists();

        resetAllDecksSyncState();
    }
    changeVolumes();
}

void Knowthelist::timerRateRestore_timeOut()
{
    qDebug() << Q_FUNC_INFO << "timerRateRestore_timeOut()";
    if (!m_rateRestorePlayer) {
        m_rateRestoreTimer->stop();
        return;
    }

    if (!m_rateRestorePlayer->isStarted()) {
        m_rateRestorePlayer->setTempoRate(1.0);
        m_rateRestoreTimer->stop();
        m_rateRestorePlayer = nullptr;
        return;
    }

    const double current = m_rateRestorePlayer->tempoRate();
    const double delta = qAbs(current - 1.0);
    const double step = qBound(0.0005, delta * 0.25, 0.0015);
    if (delta <= step) {
        m_rateRestorePlayer->setTempoRate(1.0);
        m_rateRestoreTimer->stop();
        // Reset the sync state after successful tempo restoration to properly turn off UI elements
        m_rateRestorePlayer->resetSyncState();
        m_rateRestorePlayer = nullptr;
    } else {
        m_rateRestorePlayer->setTempoRate(current + (current < 1.0 ? step : -step));
    }
}

void Knowthelist::savePlaylists()
{
    djSession->storePlaylists("defaultKnowthelist", true);
        playList1->saveXML( playList1->defaultPlaylistPath() );
        playList2->saveXML( playList2->defaultPlaylistPath() );
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
    // Update VU meter directly from the JUCE level signal (cross-thread safe via queued).
    connect(monitorPlayer, &MonitorPlayer::levelChanged, this, [this]() {
        monitorMeter->setValueLeft(monitorPlayer->levelLeft());
        monitorMeter->setValueRight(monitorPlayer->levelRight());
    }, Qt::QueuedConnection);

    qDebug() << Q_FUNC_INFO << "END ";
    return true;
}

void Knowthelist::currentDjChanged(Dj* dj)
{
    // show Dj Name on panel
    ui->lblDjName->setText(dj->name);
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

void Knowthelist::on_cmdMonitorSettings_clicked()
{
    preferences->setCurrentTab(SettingsDialog::TabMonitor);
    editSettings();
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
    if (checked)
        ui->ledAGC->on();
    else
        ui->ledAGC->off();
    if (checked) {
        timerGain1->start();
        timerGain2->start();
        timerGain1_timeOut();
        timerGain2_timeOut();
    } else {
        timerGain1->stop();
        timerGain2->stop();
    }
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
    if (!checked) {
        clearAutoFadeSyncState();
        resetWaitingDeckTempoPreviews();
    }
    setFaderModeToPlayer();
}

void Knowthelist::on_toggleAutoSync_toggled(bool checked)
{
    QSettings settings;
    settings.setValue("autoSyncEnabled", checked);
    applyAutoSyncEnabled(checked);
}

void Knowthelist::on_toggleBeatVisual_toggled(bool checked)
{
    QSettings settings;
    settings.setValue("beatSyncVisualMode", checked);
    applyBeatVisualMode(checked);
    if (!checked && m_toggleAutoSyncButton && m_toggleAutoSyncButton->isChecked())
        m_toggleAutoSyncButton->setChecked(false);
}

void Knowthelist::player1_monitorRouteToggled(bool enabled)
{
    QSettings settings;
    settings.setValue("player1MonitorRoute", enabled);
}

void Knowthelist::player2_monitorRouteToggled(bool enabled)
{
    QSettings settings;
    settings.setValue("player2MonitorRoute", enabled);
}

void Knowthelist::updatePlayerMonitorRouting()
{
    if (!player1 || !player2)
        return;

    QSettings settings;
    QString monitorDeviceId;
    bool monitorAvailable = false;

    if (monitorPlayer && !monitorPlayer->isDisabled()) {
        monitorDeviceId = monitorPlayer->outputDeviceName().trimmed();
        if (monitorDeviceId.isEmpty())
            monitorDeviceId = monitorPlayer->outputDeviceID().trimmed();
        monitorAvailable = !monitorDeviceId.isEmpty();
    }

    player1->setMonitorOutputDeviceId(monitorDeviceId);
    player2->setMonitorOutputDeviceId(monitorDeviceId);
    player1->setMonitorRouteAvailable(monitorAvailable);
    player2->setMonitorRouteAvailable(monitorAvailable);

    player1->setMonitorRouteEnabled(settings.value("player1MonitorRoute", false).toBool());
    player2->setMonitorRouteEnabled(settings.value("player2MonitorRoute", false).toBool());
}

void Knowthelist::playlist1_currentTrackChanged(Track* track)
{
    Q_UNUSED(track)
    resetWaitingDeckTempoPreviews();
}

void Knowthelist::playlist2_currentTrackChanged(Track* track)
{
    Q_UNUSED(track)
    resetWaitingDeckTempoPreviews();
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
    const double v = value / 100.0;
    monitorPlayer->setVolume(v);
    // Mirror the same volume to each deck's pre-fader monitor branch
    if (player1) player1->setMonitorVolume(v);
    if (player2) player2->setMonitorVolume(v);
}
