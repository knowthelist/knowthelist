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
#include "player.h"
#include "playerbpmwidget.h"
#include "trackanalyser.h"
#include "ui_playerwidget.h"
#include "vumeter.h"

#include <QDragEnterEvent>
#include <QGridLayout>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <cmath>

struct PlayerWidgetPrivate {
    bool isEndAnnounced;
};

namespace {
constexpr int kTempoCacheVersion = 9;

struct CachedTempo {
    bool valid;
    int bpm;
    int beatOffsetMs;

    CachedTempo()
        : valid(false)
        , bpm(0)
        , beatOffsetMs(0)
    {
    }
};

bool ensureTempoCacheTable()
{
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS analysis_cache ("
                "url VARCHAR(120) PRIMARY KEY,"
                "bpm INTEGER,"
                "beat_offset_ms INTEGER,"
                "changedate INTEGER );")) {
        return false;
    }

    if (!q.exec("PRAGMA table_info(analysis_cache)"))
        return false;

    bool hasVersionColumn = false;
    while (q.next()) {
        if (q.value(1).toString() == QLatin1String("analysis_version")) {
            hasVersionColumn = true;
            break;
        }
    }

    if (!hasVersionColumn) {
        QSqlQuery alterQuery(db);
        if (!alterQuery.exec("ALTER TABLE analysis_cache ADD COLUMN analysis_version INTEGER DEFAULT 0"))
            return false;
    }

    return true;
}

CachedTempo loadCachedTempo(const QUrl& url)
{
    CachedTempo cached;
    if (!ensureTempoCacheTable())
        return cached;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare("SELECT bpm, beat_offset_ms, analysis_version FROM analysis_cache WHERE url = :url");
    q.bindValue(":url", url.toLocalFile());
    if (!q.exec())
        return cached;

    if (q.next()) {
        const int analysisVersion = q.value(2).toInt();
        if (analysisVersion == kTempoCacheVersion) {
            cached.valid = true;
            cached.bpm = q.value(0).toInt();
            cached.beatOffsetMs = q.value(1).toInt();
        }
    }

    return cached;
}

void storeCachedTempo(const QUrl& url, int bpm, const QTime& beatPosition)
{
    if (bpm <= 0)
        return;
    if (!ensureTempoCacheTable())
        return;

    const int beatOffsetMs = QTime(0, 0).msecsTo(beatPosition);
    QSqlQuery q(QSqlDatabase::database());
    q.prepare("INSERT OR REPLACE INTO analysis_cache (url, bpm, beat_offset_ms, changedate, analysis_version) "
              "VALUES (:url, :bpm, :beat_offset_ms, strftime('%s','now'), :analysis_version)");
    q.bindValue(":url", url.toLocalFile());
    q.bindValue(":bpm", bpm);
    q.bindValue(":beat_offset_ms", beatOffsetMs);
    q.bindValue(":analysis_version", kTempoCacheVersion);
    q.exec();
}
}

PlayerWidget::PlayerWidget(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::PlayerWidget)
    , songTime(0)
    , mTrackFinishEmitTime(12000)
    , m_CurrentTrack(nullptr)
    , remainCueTime(0)
    , m_isStarted(false)
    , m_isHanging(false)
    , m_beatSyncEnabled(true)
    , m_beatCueEnabled(true)
    , m_beatVisualMode(false)
    , m_tempoRate(1.0)
    , m_syncAdopting(false)
    , m_bpmAnalysed(false)
    , m_bpm(0)
    , p(new PlayerWidgetPrivate)
{
    ui->setupUi(this);

    if (QGridLayout* mainGrid = qobject_cast<QGridLayout*>(ui->frame_3->layout())) {
        mainGrid->setContentsMargins(4, 1, 4, 1);
        mainGrid->setVerticalSpacing(2);
    }
    if (QLayout* meterLayoutBase = ui->fraVuMeter->layout()) {
        meterLayoutBase->setContentsMargins(0, 1, 0, 1);
        meterLayoutBase->setSpacing(0);
    }
    if (QVBoxLayout* displayLayout = qobject_cast<QVBoxLayout*>(ui->fraDisplay->layout())) {
        displayLayout->setContentsMargins(6, 2, 6, 2);
        displayLayout->setSpacing(2);
    }

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
    QAbstractButton* const syncButton = findChild<QAbstractButton*>("butSync");
    if (syncButton) {
        syncButton->setCheckable(true);
        syncButton->setChecked(false);
    }

    // Display panel takes the bulk; button panel capped to a narrow stripe
    ui->horizontalLayout->setStretch(0, 4);
    ui->horizontalLayout->setStretch(1, 1);
    ui->frame_2->setMaximumWidth(150);
    ui->frame_3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Track fraVuMeter's own resize so bpmWidget geometry stays in sync
    ui->fraVuMeter->installEventFilter(this);

    ui->frame_4->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->frame_5->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->frame_6->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->frame_7->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->frame_4->setMinimumWidth(0);
    ui->frame_5->setMinimumWidth(0);
    ui->frame_6->setMinimumWidth(0);
    ui->butPlay->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    ui->butCue->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    ui->butRew->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    ui->butFwd->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    if (QAbstractButton* beatModeButton = findChild<QAbstractButton*>("butBeatMode"))
        beatModeButton->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    if (syncButton) {
        syncButton->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        syncButton->show();
        syncButton->raise();
    }
    // Track fraVuMeter for resize (keeps bpmWidget geometry correct)
    // and frame_3 for mouse clicks (toggle BPM/VU mode)
    ui->frame_3->installEventFilter(this);
    ui->butPlay->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->butCue->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->butRew->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->butFwd->setMaximumWidth(QWIDGETSIZE_MAX);
    if (QAbstractButton* syncButton = findChild<QAbstractButton*>("butSync"))
        syncButton->setMaximumWidth(QWIDGETSIZE_MAX);

    vuMeter = ui->vuMeter;
    vuMeter->setOrientation(Qt::Horizontal);
    vuMeter->LevelColorNormal.setRgb(112, 146, 190);
    vuMeter->LevelColorHigh.setRgb(218, 59, 9);
    vuMeter->LevelColorOff.setRgb(31, 45, 65);
    vuMeter->setLinesPerSegment(2);
    vuMeter->setSpacesBetweenSegments(1);
    vuMeter->setSegmentsPerPeak(2);

    bpmWidget = new PlayerBpmWidget(vuMeter->parentWidget());
    // Initial geometry: will be corrected by eventFilter on first fraVuMeter resize
    bpmWidget->setGeometry(ui->fraVuMeter->rect());
    bpmWidget->setTempoInfo(m_tempoRate, m_syncAdopting);
    bpmWidget->hide();

    QSettings settings;
    applyBeatVisualLayout(settings.value("beatSyncVisualMode", false).toBool());

    timerLevel = new QTimer(this);
    connect(timerLevel, SIGNAL(timeout()), SLOT(timerLevel_timeOut()));

    timerPosition = new QTimer(this);
    connect(timerPosition, SIGNAL(timeout()), SLOT(timerPosition_timeOut()));

    connect(player, SIGNAL(finish()), this, SLOT(playerFinished()));
    connect(player, SIGNAL(error()), this, SLOT(playerError()));
    connect(player, SIGNAL(loadFinished()), this, SLOT(playerLoaded()));

    ui->lblTitle->setText("");
    ui->lblInfo->setText("");

    QFont font = ui->lblInfo->font();
    QFont fonttime = ui->lblTime->font();
#if defined(Q_OS_DARWIN)
    int newSize = font.pointSize() - 4;
    fonttime.setPointSize(fonttime.pointSize() + 2);
#else
    int newSize = font.pointSize() - 1;
#endif
    font.setPointSize(newSize);
    ui->lblInfo->setFont(font);
    ui->lblTime->setFont(fonttime);
    ui->lblTimeRemain->setFont(fonttime);

    m_isStarted = false;
    m_pendingPlay = false;
    setAcceptDrops(true);
    updateResponsiveLayout();
    this->stop();

    trackanalyser = new TrackAnalyser(this);
    connect(trackanalyser, SIGNAL(finishGain()), this, SLOT(analyseGainFinished()));

    tempoAnalyser = new TrackAnalyser(this);
    connect(tempoAnalyser, SIGNAL(finishTempo()), this, SLOT(analyseTempoFinished()));
}

PlayerWidget::~PlayerWidget()
{
    delete player;
    delete timerPosition;
    delete timerLevel;
    delete trackanalyser;
    trackanalyser = nullptr;
    delete tempoAnalyser;
    tempoAnalyser = nullptr;
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

void PlayerWidget::setTempoRate(double rate)
{
    m_tempoRate = rate;
    player->setRate(rate);
    bpmWidget->setTempoInfo(m_tempoRate, m_syncAdopting);
    if (std::fabs(m_tempoRate - 1.0) < 0.001 && !m_syncAdopting)
        updateSyncButtonState(false);
}

void PlayerWidget::setSyncActive(bool active)
{
    updateSyncButtonState(active);
}

void PlayerWidget::setSyncAdopting(bool active)
{
    m_syncAdopting = active;
    bpmWidget->setTempoInfo(m_tempoRate, m_syncAdopting);
}

void PlayerWidget::updateSyncButtonState(bool active)
{
    if (QAbstractButton* syncButton = findChild<QAbstractButton*>("butSync")) {
        if (syncButton->isChecked() != active) {
            const QSignalBlocker blocker(syncButton);
            syncButton->setChecked(active);
        }
    }
    Q_EMIT syncStateChanged(active);
}

bool PlayerWidget::supportsSmoothTempo() const
{
    return player->supportsSmoothTempo();
}

void PlayerWidget::setBeatVisualMode(bool enabled)
{
    m_beatVisualMode = enabled;
    QSettings settings;
    settings.setValue("beatSyncVisualMode", enabled);
    applyBeatVisualLayout(enabled);
    if (m_beatVisualMode) {
        vuMeter->hide();
        bpmWidget->show();
        bpmWidget->raise();
    } else {
        bpmWidget->hide();
        vuMeter->show();
    }
}

void PlayerWidget::applyBeatVisualLayout(bool enabled)
{
    // Keep the same outer dimensions in both modes so the player never resizes on toggle
    setMinimumHeight(182);
    ui->frame_2->setMaximumHeight(195);
    ui->frame_3->setMaximumHeight(195);
    ui->fraDisplay->setMaximumHeight(195);
    ui->fraDisplay->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->fraVuMeter->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->fraVuMeter->setMinimumHeight(110);
    ui->fraDigits->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->vuMeter->setMaximumWidth(QWIDGETSIZE_MAX);
    if (enabled) {
        // BPM mode: vuMeter is hidden behind bpmWidget — no height restriction needed
        ui->vuMeter->setMinimumHeight(31);
        ui->vuMeter->setMaximumHeight(QWIDGETSIZE_MAX);
    } else {
        // VU mode: keep bars at a compact natural height, centred with padding top+bottom
        ui->vuMeter->setFixedHeight(42);
        if (QHBoxLayout* meterLayout = qobject_cast<QHBoxLayout*>(ui->fraVuMeter->layout()))
            meterLayout->setAlignment(ui->vuMeter, Qt::AlignVCenter);
    }

    if (enabled)
        ui->lblInfo->setStyleSheet("font-size: 9pt;");
    else
        ui->lblInfo->setStyleSheet("font-size: 11pt;");

    drawTitle();
    updateResponsiveLayout();
}

void PlayerWidget::updateResponsiveLayout()
{
    const int width = this->width();
    const bool veryCompact = width < 360;
    const bool compact = width < 500;
    QAbstractButton* const beatModeButton = findChild<QAbstractButton*>("butBeatMode");
    QAbstractButton* const syncButton = findChild<QAbstractButton*>("butSync");

    int outerMargin = compact ? 2 : 4;
    int rowSpacing = veryCompact ? 0 : (compact ? 1 : 2);
    if (QGridLayout* mainGrid = qobject_cast<QGridLayout*>(ui->frame_3->layout())) {
        mainGrid->setContentsMargins(outerMargin, 1, outerMargin, 1);
        mainGrid->setVerticalSpacing(rowSpacing);
    }

    if (QVBoxLayout* displayLayout = qobject_cast<QVBoxLayout*>(ui->fraDisplay->layout())) {
        const int horizontalMargin = compact ? 3 : 6;
        displayLayout->setContentsMargins(horizontalMargin, 2, horizontalMargin, 2);
        displayLayout->setSpacing(compact ? 1 : 2);
    }

    if (QLayout* layout = ui->horizontalLayout_2)
        layout->setSpacing(veryCompact ? 0 : (compact ? 1 : 4));
    if (QLayout* layout = ui->horizontalLayout_3)
        layout->setSpacing(veryCompact ? 0 : (compact ? 2 : 6));
    if (QLayout* layout = ui->horizontalLayout_4)
        layout->setSpacing(veryCompact ? 0 : (compact ? 1 : 6));
    if (QLayout* layout = findChild<QLayout*>("horizontalLayout_9"))
        layout->setSpacing(veryCompact ? 0 : (compact ? 1 : 4));

    const int smallButtonHeight = veryCompact ? 16 : (compact ? 18 : 22);
    const int playButtonHeight = veryCompact ? 28 : (compact ? 34 : 38);
    const int playButtonWidth = veryCompact ? 40 : (compact ? 48 : 60);
    const int cueButtonWidth = veryCompact ? 40 : (compact ? 48 : 59);
    const int smallButtonWidth = veryCompact ? 18 : (compact ? 24 : 36);
    const int syncButtonWidth = veryCompact ? 30 : (compact ? 40 : 52);
    const int iconSize = veryCompact ? 18 : (compact ? 22 : 26);

    //ui->frame_5->setMinimumHeight(playButtonHeight);
    //ui->frame_6->setMinimumHeight(smallButtonHeight + 6);
    //ui->frame_4->setMinimumHeight(smallButtonHeight + 2);
    //if (QFrame* syncFrame = findChild<QFrame*>("frame_7"))
     //   syncFrame->setMinimumHeight(smallButtonHeight + 4);

    ui->butPlay->setMinimumSize(QSize(playButtonWidth, playButtonHeight));
    ui->butPlay->setMaximumHeight(playButtonHeight);
    ui->butCue->setMinimumSize(QSize(cueButtonWidth, smallButtonHeight));
    ui->butCue->setMaximumHeight(playButtonHeight);
    ui->butRew->setMinimumSize(QSize(smallButtonWidth, smallButtonHeight));
    ui->butRew->setMaximumHeight(smallButtonHeight);
    ui->butFwd->setMinimumSize(QSize(smallButtonWidth, smallButtonHeight));
    ui->butFwd->setMaximumHeight(smallButtonHeight);
    if (beatModeButton) {
        beatModeButton->setMinimumSize(QSize(smallButtonWidth, smallButtonHeight));
        beatModeButton->setMaximumHeight(smallButtonHeight);
    }
    if (syncButton) {
        syncButton->setMinimumSize(QSize(syncButtonWidth, smallButtonHeight));
        syncButton->setMaximumHeight(smallButtonHeight);
        QFont syncFont = syncButton->font();
        syncFont.setPointSize(veryCompact ? qMax(7, syncFont.pointSize() - 3)
                                          : (compact ? qMax(8, syncFont.pointSize() - 2)
                                                     : syncFont.pointSize()));
        syncButton->setFont(syncFont);
    }

    ui->butPlay->setIconSize(QSize(iconSize, iconSize));
    ui->butRew->setIconSize(QSize(iconSize, iconSize));
    ui->butFwd->setIconSize(QSize(iconSize, iconSize));
    if (syncButton)
        syncButton->setText(veryCompact ? tr("S") : tr("SYNC"));
}

void PlayerWidget::setInfo(QPair<int, int> info)
{
    QString strTrack = (info.first > 1) ? tr("Tracks") : tr("Track");
    ui->lblInfo->setText(QString("%1 %2       %3 %4")
                             .arg(info.first)
                             .arg(strTrack)
                             .arg(Track::prettyTime(info.second))
                             .arg(tr("Hours")));
}

void PlayerWidget::setEqualizer(EqBand band, int value)
{
    //ranging from -24.0 to +12.0.
    player->setEqualizer("band" + QString::number(band), (value - 240) / 10.0);
}

void PlayerWidget::setPositionMarkers()
{
    if (trackanalyser->finished()) {
        if (m_skipSilentEnd && trackanalyser->endPosition() > QTime(0, 0)) {
            qDebug() << Q_FUNC_INFO << "endPosition:" << trackanalyser->endPosition();
            qDebug() << Q_FUNC_INFO << "length:" << trackanalyser->length();
            remainCueTime = trackanalyser->endPosition().msecsTo(trackanalyser->length());
        } else
            remainCueTime = 0;

        ui->txtCue->setText("-" + QString::number(remainCueTime / 1000));
    }

    if (!m_isStarted && m_skipSilentBegin && trackanalyser->finished()) {
        player->setPosition(trackanalyser->startPosition());
        ui->butCue->setChecked(true);
    }
}

void PlayerWidget::play()
{
    m_isStarted = true;
    if (m_CurrentTrack) {
        ui->butPlay->setIcon(QIcon(":pause.png"));
        ui->butPlay->setChecked(true);
        player->play();
        ui->butCue->setChecked(false);
        timerLevel->start(50);
        timerPosition->start(100);
        Q_EMIT statusChanged(m_isStarted);
    } else
        m_isHanging = true;
}

void PlayerWidget::pause()
{
    ui->butPlay->setIcon(QIcon(":play.png"));
    ui->butPlay->setChecked(false);
    m_isStarted = false;
    m_pendingPlay = false;
    player->pause();
    m_syncAdopting = false;
    updateSyncButtonState(false);
    timerLevel->stop();
    timerPosition->stop();
    vuMeter->reset();
    bpmWidget->clearEnvelope();
    bpmWidget->setTempoInfo(m_tempoRate, m_syncAdopting);
    Q_EMIT statusChanged(m_isStarted);
    Q_EMIT levelChanged(0, 0);
}

void PlayerWidget::stop()
{
    ui->butPlay->setIcon(QIcon(":play.png"));
    ui->butPlay->setChecked(false);
    m_isStarted = false;
    m_isHanging = false;
    m_pendingPlay = false;
    m_tempoRate = 1.0;
    m_syncAdopting = false;
    updateSyncButtonState(false);
    player->stop();
    timerLevel->stop();
    timerPosition->stop();
    vuMeter->reset();
    bpmWidget->clearEnvelope();
    bpmWidget->setTempoInfo(m_tempoRate, m_syncAdopting);
    Q_EMIT statusChanged(m_isStarted);
    Q_EMIT levelChanged(0, 0);
}

void PlayerWidget::on_butPlay_clicked()
{
    if (m_isStarted) {
        this->pause();
    } else {
        this->play();
    }
}

void PlayerWidget::analyseGainFinished()
{
    qDebug() << Q_FUNC_INFO << ":" << objectName();
    // got gain factor -> emit
    if (trackanalyser->gainDB() != TrackAnalyser::GAIN_INVALID) {
        Q_EMIT gainChanged(trackanalyser->gainFactor());
    }
    if (m_CurrentTrack) {
        setPositionMarkers();
        updateTimeAndPositionDisplay();

        if (m_beatSyncEnabled) {
            QSettings settings;
            const bool analyseTempo = settings.value("beatSyncAnalyzeTempo", true).toBool();
            if (analyseTempo && !m_bpmAnalysed)
                bpmWidget->setState(0, player->position(), m_beatPosition, m_isStarted, false);
        }
    }
}

void PlayerWidget::analyseTempoFinished()
{
    if (!m_CurrentTrack)
        return;

    m_bpmAnalysed = true;
    m_bpm = tempoAnalyser->bpm();
    m_beatPosition = tempoAnalyser->beatPosition();

    if (m_bpm > 0)
        Q_EMIT tempoChanged(m_bpm, m_beatPosition);

    if (m_CurrentTrack)
        storeCachedTempo(m_CurrentTrack->url(), m_bpm, m_beatPosition);

    bpmWidget->setState(m_bpm, player->position(), m_beatPosition, m_isStarted, true);
}

void PlayerWidget::onTrackPropertyChanged(Track* track)
{
    if (!track || !m_CurrentTrack)
        return;

    if (track != m_CurrentTrack && track->url() != m_CurrentTrack->url())
        return;

    const int trackBpm = track->bpm();
    if (trackBpm <= 0)
        return;

    if (m_bpm == trackBpm && m_bpmAnalysed)
        return;

    if (m_bpm <= 0 || !m_bpmAnalysed) {
        m_bpm = trackBpm;
        m_bpmAnalysed = true;
        bpmWidget->setState(m_bpm, player->position(), m_beatPosition, m_isStarted, true);
    }
}

void PlayerWidget::timerLevel_timeOut()
{
    const double inLeft = player->levelLeft();
    const double inRight = player->levelRight();
    const double outLeft = player->levelOutLeft();
    const double outRight = player->levelOutRight();

    const float env = static_cast<float>(qBound(0.0, qMax(inLeft, inRight), 1.0));
    bpmWidget->appendEnvelopeSample(env);

    if (m_beatVisualMode) {
        bpmWidget->setState(m_bpm, player->position(), m_beatPosition, m_isStarted, m_bpmAnalysed);
    } else {
        vuMeter->setValueLeft(inLeft);
        vuMeter->setValueRight(inRight);
    }
    Q_EMIT levelChanged(outLeft, outRight);
}

void PlayerWidget::timerPosition_timeOut()
{
    updateTimeAndPositionDisplay();
}

void PlayerWidget::dragEnterEvent(QDragEnterEvent* event)
{
    //ToDo: remove forein classname tracklist"
    if (!event->source())
        return;
    event->setDropAction(Qt::CopyAction);
    QString sourceSite = event->source()->objectName();
    QString dropSite = this->objectName();
    qDebug() << "PlayerWidget: dragEnterEvent: sourceSite=" << sourceSite << " dropSite=" << dropSite;
    if (sourceSite.left(4) == dropSite.left(4) || sourceSite.left(9) == "tracklist") {
        qDebug() << "PlayerWidget: dragEnterEvent: acceptProposedAction";
        event->acceptProposedAction();
    }
}

void PlayerWidget::dragMoveEvent(QDragMoveEvent* event)
{
    event->acceptProposedAction();
}

void PlayerWidget::dropEvent(QDropEvent* event)
{
    qDebug() << "PlayerWidget: dragEnterEvent: " << event->mimeData();
    if (event->mimeData()->hasUrls()) {
        QList<QUrl> urlList = event->mimeData()->urls(); // returns list of QUrls
        event->ignore();

        if (urlList.size() > 0) // if at least one QUrl is present in list
        {
            //load first
            loadFile(urlList.at(1));
        }
    } else if (event->mimeData()->hasFormat("text/playlistitem")) {

        //decode playlistitem
        QByteArray itemData = event->mimeData()->data("text/playlistitem");
        QDataStream stream(&itemData, QIODevice::ReadOnly);
        QVector<QStringList> tags;

        stream >> tags;
        event->setDropAction(Qt::MoveAction);
        event->accept();

        //publish dropped Tracks to connected playlist
        for (const QStringList& tag : tags) {
            Track* track = new Track(tag);
            Q_EMIT trackDropped(track);
        }

    } else
        event->ignore();
}

void PlayerWidget::loadFile(QUrl file)
{
    qDebug() << Q_FUNC_INFO << "url=" << file;
    loadTrack(new Track(file));
}

void PlayerWidget::loadTrack(Track* track)
{
    if (track)
        qDebug() << Q_FUNC_INFO << ":" << objectName() << " track=" << track->url();

    m_CurrentTrack = track;
    m_pendingPlay = false;
    m_bpmAnalysed = false;
    m_bpm = 0;
    m_tempoRate = 1.0;
    m_syncAdopting = false;
    m_beatPosition = QTime();
    updateSyncButtonState(false);
    bpmWidget->setState(m_bpm, QTime(0, 0), m_beatPosition, false, track == nullptr);
    bpmWidget->setTempoInfo(m_tempoRate, m_syncAdopting);
    bpmWidget->clearEnvelope();

    if (track != nullptr) {

        drawTitle();

        bool doPlay = m_isStarted;
        player->stop();

        QUrl url = track->url();
        player->open(url);

        trackanalyser->setMode(TrackAnalyser::STANDARD);
        trackanalyser->open(url);

        if (m_beatSyncEnabled) {
            QSettings settings;
            const bool analyseTempo = settings.value("beatSyncAnalyzeTempo", true).toBool();
            const CachedTempo cached = loadCachedTempo(url);
            if (cached.valid && cached.bpm > 0) {
                m_bpmAnalysed = true;
                m_bpm = cached.bpm;
                m_beatPosition = QTime(0, 0).addMSecs(cached.beatOffsetMs);
                bpmWidget->setState(m_bpm, player->position(), m_beatPosition, m_isStarted, true);
                Q_EMIT tempoChanged(m_bpm, m_beatPosition);
            }

            // Cached BPM can be stale after detector improvements, so re-check
            // in the background whenever auto analysis is enabled.
            if (analyseTempo) {
                tempoAnalyser->setTempoScanDurationSeconds(qMax(16, settings.value("beatSyncScanSeconds", 16).toInt()));
                tempoAnalyser->setMode(TrackAnalyser::TEMPO);
                tempoAnalyser->open(url);
            }
        }

        m_pendingPlay = doPlay;

    } else {
        if (player->lastError != "")
            ui->lblTitle->setText(player->lastError);
        else
            ui->lblTitle->setText("no track");

        ui->lblTime->setText("-:-");
        ui->lblTimeMs->setText(".-");
        ui->lblTimeRemain->setText("-:-");
        ui->lblTimeRemainMs->setText(".-");
        stop();
    }

    remainCueTime = 0;
    ui->sliPosition->setValue(0);
    ui->txtCue->setText("-");
    ui->butCue->setChecked(false);
}

void PlayerWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    updateResponsiveLayout();
    drawTitle();
    // bpmWidget geometry is updated via eventFilter on fraVuMeter
}

bool PlayerWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == ui->fraVuMeter && event->type() == QEvent::Resize) {
        // fraVuMeter has its final geometry now — fill it exactly
        bpmWidget->setGeometry(ui->fraVuMeter->rect());
        return false;
    }
    if (obj == ui->frame_3 && event->type() == QEvent::MouseButtonPress) {
        // Click on the display panel toggles BPM/VU mode
        setBeatVisualMode(!m_beatVisualMode);
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void PlayerWidget::drawTitle()
{
    int width = ui->lblTitle->width() - 2;
    if (m_beatVisualMode) {
        if (width < 300)
            ui->lblTitle->setStyleSheet("* { font-size: 13pt; }");
        else if (width < 400)
            ui->lblTitle->setStyleSheet("* { font-size: 14pt; }");
        else
            ui->lblTitle->setStyleSheet("* { font-size: 16pt; }");
    } else {
        if (width < 300)
            ui->lblTitle->setStyleSheet("* { font-size: 15pt; }");
        else if (width < 400)
            ui->lblTitle->setStyleSheet("* { font-size: 16pt; }");
        else
            ui->lblTitle->setStyleSheet("* { font-size: 18pt; }");
    }

    QFontMetrics metrix(ui->lblTitle->font());

    QString clippedText = tr("No track");
    if (m_CurrentTrack)
        clippedText = metrix.elidedText(m_CurrentTrack->prettyTitle(), Qt::ElideRight, width);

    ui->lblTitle->setText(clippedText);
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
    QTime remain(0, 0, 0);
    long remainMs;

    //Some tracks deliver no length in state pause
    if (length == QTime(0, 0) && m_CurrentTrack)
        length = QTime(0, 0, 0).addSecs(m_CurrentTrack->length());

    remainMs = curpos.msecsTo(length);
    remain = QTime(0, 0, 0).addMSecs(remainMs);

    //qDebug()<<remainMs << " :" <<remain;

    ui->lblTime->setText(curpos.toString("mm:ss"));
    ui->lblTimeMs->setText("." + curpos.toString("zzz").left(1));
    ui->lblTimeRemain->setText("-" + remain.toString("mm:ss"));
    ui->lblTimeRemainMs->setText("." + remain.toString("zzz").left(1));

    //Signal end of track or media error
    //ToDo: better recognition of media error (play pressed but player is not running)

    if ((remainMs - remainCueTime - mTrackFinishEmitTime <= 0
            && 0 < remainMs)
        || m_isHanging) {
        if (!p->isEndAnnounced) {
            qDebug() << Q_FUNC_INFO << ":" << objectName() << " EMIT aboutFinished";
            qDebug() << Q_FUNC_INFO << ": curpos:" << curpos;
            qDebug() << Q_FUNC_INFO << ": remainMs:" << remainMs;
            qDebug() << Q_FUNC_INFO << ": remainCueTime:" << remainCueTime;
            qDebug() << Q_FUNC_INFO << ": mTrackFinishEmitTime:" << mTrackFinishEmitTime;
            qDebug() << Q_FUNC_INFO << ": m_isHanging:" << m_isHanging;

            //send signals only once
            p->isEndAnnounced = true;
            Q_EMIT aboutFinished();
            Q_EMIT trackPlayed(m_CurrentTrack);
        }
    } else
        p->isEndAnnounced = false;

    //update position slider only if triggerd by timer
    if (isPassive) {
        if (length != QTime(0, 0, 0))
            ui->sliPosition->setValue(curpos.msecsTo(QTime(0, 0, 0)) * 1000 / length.msecsTo(QTime(0, 0, 0)));
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

    if (m_pendingPlay) {
        m_pendingPlay = false;
        play();
    }
}

void PlayerWidget::on_butRew_clicked()
{
    if (player->position() < QTime(0, 0, 3))
        Q_EMIT rewindPressed();
    else
        player->setPosition(QTime(0, 0, 0));
}

void PlayerWidget::on_butFwd_clicked()
{
    Q_EMIT forwardPressed();
}

void PlayerWidget::setTrackFinishEmitTime(const int sec)
{
    if (sec >= 0 && sec < 60)
        mTrackFinishEmitTime = sec * 1000;
}

void PlayerWidget::on_sliPosition_sliderMoved(int value)
{
    uint length = -player->length().msecsTo(QTime(0, 0, 0));
    if (length != 0 && value > 0) {
        QTime pos = QTime(0, 0, 0);
        pos = pos.addMSecs(length * (value / 1000.0));
        qDebug() << "pos:" << pos;
        player->setPosition(pos);
    }
    updateTimeAndPositionDisplay(false);
}

void PlayerWidget::on_sliPosition_actionTriggered(int action)
{
    //a workaround for page moving
    int posi;
    switch (action) {
    case 3:
        posi = ui->sliPosition->value() + 100;
        break;
    case 4:
        posi = ui->sliPosition->value() - 100;
        if (posi < 100)
            posi = 1;
        break;
    case 1:
        posi = ui->sliPosition->value() + 10;
        break;
    case 2:
        posi = ui->sliPosition->value() - 10;
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

    QTime cuePosition = trackanalyser->startPosition();
    if (m_beatCueEnabled && m_bpm > 0 && m_beatPosition.isValid())
        cuePosition = m_beatPosition;

    player->setPosition(cuePosition);
    updateTimeAndPositionDisplay();
}

QTime PlayerWidget::currentPosition() const
{
    return player->position();
}

void PlayerWidget::alignCueToReferenceBeat(int referenceBpm, const QTime& referencePosition,
                                            const QTime& referenceBeatAnchor)
{
    if (m_isStarted || m_bpm <= 0 || referenceBpm <= 0)
        return;

    const double ownBeatMs = 60000.0 / static_cast<double>(m_bpm);
    const double refBeatMs = 60000.0 / static_cast<double>(referenceBpm);

    // Use a 4-beat bar for bar-level cue alignment
    const int    BAR_BEATS = 4;
    const double refBarMs  = BAR_BEATS * refBeatMs;

    const qint64 refMs = QTime(0, 0).msecsTo(referencePosition);
    const qint64 refAnchorMs = referenceBeatAnchor.isValid()
                                   ? QTime(0, 0).msecsTo(referenceBeatAnchor) : 0LL;

    double refBarPhase = fmod(static_cast<double>(refMs - refAnchorMs), refBarMs);
    if (refBarPhase < 0.0) refBarPhase += refBarMs;

    // Scale to this track's beat period
    const double targetBarPhase = refBarPhase * (ownBeatMs / refBeatMs);

    // Base cue: the waiting track's beat anchor (first detected strong beat).
    // Best cue position = baseCueMs + targetBarPhase
    const QTime baseCue = m_beatPosition.isValid() ? m_beatPosition : trackanalyser->startPosition();
    const qint64 baseCueMs = QTime(0, 0).msecsTo(baseCue);

    const qint64 bestMs = baseCueMs + static_cast<qint64>(targetBarPhase + 0.5);
    player->setPosition(QTime(0, 0).addMSecs(static_cast<int>(bestMs)));
    updateTimeAndPositionDisplay(false);
}

void PlayerWidget::syncNowToReferenceBeat(int referenceBpm, const QTime& referencePosition,
                                          const QTime& referenceBeatAnchor)
{
    // Works while playing: seeks to the nearest bar-aligned position that puts
    // this deck on the same beat-of-the-bar as the reference deck, so both
    // decks share the same beat-1 in a standard 4/4 bar.
    if (m_bpm <= 0 || referenceBpm <= 0)
        return;

    const double ownBeatMs   = 60000.0 / static_cast<double>(m_bpm);
    const double refBeatMs   = 60000.0 / static_cast<double>(referenceBpm);

    // Use a 4-beat bar so both decks land on the same beat-of-the-bar (beat 1).
    const int    BAR_BEATS   = 4;
    const double ownBarMs    = BAR_BEATS * ownBeatMs;
    const double refBarMs    = BAR_BEATS * refBeatMs;

    const qint64 refMs       = QTime(0, 0).msecsTo(referencePosition);
    const qint64 refAnchorMs = referenceBeatAnchor.isValid()
                                   ? QTime(0, 0).msecsTo(referenceBeatAnchor) : 0LL;

    // Phase of reference within one bar (0 .. 4×refBeatMs)
    double refBarPhase = fmod(static_cast<double>(refMs - refAnchorMs), refBarMs);
    if (refBarPhase < 0.0) refBarPhase += refBarMs;

    // Scale bar phase to this track's beat period (handles tempo differences)
    const double targetBarPhase = refBarPhase * (ownBeatMs / refBeatMs);

    // This track's beat anchor
    const QTime baseCue   = m_beatPosition.isValid() ? m_beatPosition : trackanalyser->startPosition();
    const qint64 anchorMs = QTime(0, 0).msecsTo(baseCue);
    const qint64 currentMs = QTime(0, 0).msecsTo(player->position());

    // Bar phase of current position
    double currentBarPhase = fmod(static_cast<double>(currentMs - anchorMs), ownBarMs);
    if (currentBarPhase < 0.0) currentBarPhase += ownBarMs;

    // Smallest delta within ±half a bar (±2 beats) to land on targetBarPhase
    double delta = targetBarPhase - currentBarPhase;
    if (delta >  ownBarMs / 2.0) delta -= ownBarMs;
    if (delta < -ownBarMs / 2.0) delta += ownBarMs;

    const qint64 newMs = qMax(anchorMs, currentMs + static_cast<qint64>(delta + 0.5));
    player->setPosition(QTime(0, 0).addMSecs(static_cast<int>(newMs)));
    updateTimeAndPositionDisplay(false);

    // If BPMs differ, also match the tempo rate
    if (referenceBpm != m_bpm)
        setTempoRate(static_cast<double>(referenceBpm) / m_bpm);
}

void PlayerWidget::on_butSync_toggled(bool checked)
{
    updateSyncButtonState(checked);
    if (checked) {
        Q_EMIT syncRequested();
    } else if (!m_syncAdopting && std::fabs(m_tempoRate - 1.0) >= 0.001) {
        setTempoRate(1.0);
    }
}

