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
#include "trackanalyzer.h"
#include "ui_playerwidget.h"
#include "vumeter.h"

#include <QDragEnterEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSettings>
#include <QtGlobal>
#include <QApplication>
#include <QMargins>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <cmath>

struct PlayerWidgetPrivate {
    bool isEndAnnounced;
};

namespace {
constexpr int kTempoCacheVersion = 11;
constexpr int kEnvelopeCacheVersion = 2;
constexpr int kEnvelopeAnalysisIntervalMs = 8; // TrackAnalyzer uses 120 fps for envelope analysis.
constexpr double kScrubSeekGain = 1.5;
constexpr int kScrubSeekMinDeltaMs = 10;
constexpr int kScrubSeekCoalesceMs = 20;

struct CachedTempo {
    bool valid;
    int bpm;
    int beatOffsetMs;
    double exactBpm;

    CachedTempo()
        : valid(false)
        , bpm(0)
        , beatOffsetMs(0)
        , exactBpm(0.0)
    {
    }
};

struct CachedEnvelope {
    bool valid;
    QVector<float> samples;
    int durationMs;

    CachedEnvelope()
        : valid(false)
        , durationMs(0)
    {
    }
};

QByteArray encodeEnvelopeSamples(const QVector<float>& samples)
{
    QByteArray payload;
    payload.reserve(samples.size() * static_cast<int>(sizeof(float)) + 16);
    QDataStream out(&payload, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_12);
    out << samples;
    return qCompress(payload, 6);
}

QVector<float> decodeEnvelopeSamples(const QByteArray& compressed)
{
    QVector<float> samples;
    if (compressed.isEmpty())
        return samples;

    QByteArray payload = qUncompress(compressed);
    if (payload.isEmpty())
        return samples;

    QDataStream in(&payload, QIODevice::ReadOnly);
    in.setVersion(QDataStream::Qt_5_12);
    in >> samples;
    return samples;
}

bool addColumnIfMissing(QSqlDatabase& db, const QString& columnName, const QString& alterSql)
{
    QSqlQuery q(db);
    if (!q.exec("PRAGMA table_info(analysis_cache)"))
        return false;

    while (q.next()) {
        if (q.value(1).toString() == columnName)
            return true;
    }

    QSqlQuery alterQuery(db);
    return alterQuery.exec(alterSql);
}

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

    if (!addColumnIfMissing(db, QLatin1String("analysis_version"),
                            QLatin1String("ALTER TABLE analysis_cache ADD COLUMN analysis_version INTEGER DEFAULT 0")))
        return false;

    if (!addColumnIfMissing(db, QLatin1String("envelope_version"),
                            QLatin1String("ALTER TABLE analysis_cache ADD COLUMN envelope_version INTEGER DEFAULT 0")))
        return false;

    if (!addColumnIfMissing(db, QLatin1String("exact_bpm"),
                            QLatin1String("ALTER TABLE analysis_cache ADD COLUMN exact_bpm REAL DEFAULT 0.0")))
        return false;

    if (!addColumnIfMissing(db, QLatin1String("envelope_data"),
                            QLatin1String("ALTER TABLE analysis_cache ADD COLUMN envelope_data BLOB")))
        return false;

    if (!addColumnIfMissing(db, QLatin1String("envelope_duration_ms"),
                            QLatin1String("ALTER TABLE analysis_cache ADD COLUMN envelope_duration_ms INTEGER DEFAULT 0")))
        return false;

    return true;
}

CachedTempo loadCachedTempo(const QUrl& url)
{
    CachedTempo cached;
    if (!ensureTempoCacheTable())
        return cached;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare("SELECT bpm, beat_offset_ms, exact_bpm, analysis_version FROM analysis_cache WHERE url = :url");
    q.bindValue(":url", url.toLocalFile());
    if (!q.exec()) {
        // Older schema without exact_bpm
        q.prepare("SELECT bpm, beat_offset_ms, analysis_version FROM analysis_cache WHERE url = :url");
        q.bindValue(":url", url.toLocalFile());
        if (!q.exec())
            return cached;

        if (q.next()) {
            const int analysisVersion = q.value(2).toInt();
            const int cachedBpm = q.value(0).toInt();
            if (analysisVersion == kTempoCacheVersion || (analysisVersion <= 0 && cachedBpm > 0)) {
                cached.valid = true;
                cached.bpm = cachedBpm;
                cached.beatOffsetMs = q.value(1).toInt();
                cached.exactBpm = static_cast<double>(cachedBpm);
            }
        }
        return cached;
    }

    if (q.next()) {
        const int cachedBpm = q.value(0).toInt();
        const int beatOffsetMs = q.value(1).toInt();
        const double cachedExactBpm = q.value(2).toDouble();
        const int analysisVersion = q.value(3).toInt();
        if (analysisVersion == kTempoCacheVersion || (analysisVersion <= 0 && cachedBpm > 0)) {
            cached.valid = true;
            cached.bpm = cachedBpm;
            cached.beatOffsetMs = beatOffsetMs;
            cached.exactBpm = (cachedExactBpm > 0.0) ? cachedExactBpm : static_cast<double>(cachedBpm);
        }
    }

    return cached;
}

CachedEnvelope loadCachedEnvelope(const QUrl& url)
{
    CachedEnvelope cached;
    if (!ensureTempoCacheTable())
        return cached;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare("SELECT envelope_version, envelope_data, envelope_duration_ms FROM analysis_cache WHERE url = :url");
    q.bindValue(":url", url.toLocalFile());
    if (!q.exec())
        return cached;

    if (q.next() && q.value(0).toInt() == kEnvelopeCacheVersion) {
        const QVector<float> decoded = decodeEnvelopeSamples(q.value(1).toByteArray());
        if (!decoded.isEmpty()) {
            cached.valid = true;
            cached.samples = decoded;
            cached.durationMs = qMax(0, q.value(2).toInt());
        }
    }

    return cached;
}

void storeCachedTempo(const QUrl& url, int bpm, double exactBpm, const QTime& beatPosition)
{
    if (bpm <= 0)
        return;
    if (!ensureTempoCacheTable())
        return;

    const int beatOffsetMs = QTime(0, 0).msecsTo(beatPosition);
    QSqlQuery q(QSqlDatabase::database());
    q.prepare("INSERT OR REPLACE INTO analysis_cache (url, bpm, beat_offset_ms, exact_bpm, changedate, analysis_version, envelope_version, envelope_data) "
              "VALUES (:url, :bpm, :beat_offset_ms, :exact_bpm, strftime('%s','now'), :analysis_version, "
              "COALESCE((SELECT envelope_version FROM analysis_cache WHERE url = :url), 0), "
              "(SELECT envelope_data FROM analysis_cache WHERE url = :url))");
    q.bindValue(":url", url.toLocalFile());
    q.bindValue(":bpm", bpm);
    q.bindValue(":beat_offset_ms", beatOffsetMs);
    q.bindValue(":exact_bpm", exactBpm);
    q.bindValue(":analysis_version", kTempoCacheVersion);
    q.exec();
}

void storeCachedEnvelope(const QUrl& url, const QVector<float>& samples, int durationMs)
{
    if (samples.isEmpty())
        return;
    if (!ensureTempoCacheTable())
        return;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare("INSERT OR REPLACE INTO analysis_cache (url, bpm, beat_offset_ms, changedate, analysis_version, envelope_version, envelope_data, envelope_duration_ms) "
              "VALUES (:url, "
              "COALESCE((SELECT bpm FROM analysis_cache WHERE url = :url), 0), "
              "COALESCE((SELECT beat_offset_ms FROM analysis_cache WHERE url = :url), 0), "
              "strftime('%s','now'), "
              "COALESCE((SELECT analysis_version FROM analysis_cache WHERE url = :url), 0), "
              ":envelope_version, :envelope_data, :envelope_duration_ms)");
    q.bindValue(":url", url.toLocalFile());
    q.bindValue(":envelope_version", kEnvelopeCacheVersion);
    q.bindValue(":envelope_data", encodeEnvelopeSamples(samples));
    q.bindValue(":envelope_duration_ms", qMax(0, durationMs));
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
    , m_visualLatencyMs(0)
    , m_liveEnvelopeStarted(false)
    , m_liveEnvelopeSmoothed(0.0f)
    , m_bpmAnalyzed(false)
    , m_bpm(0)
    , m_infoBaseText("")
    , m_envelopeScrubbing(false)
    , m_envelopeScrubAnchorMs(0)
    , m_envelopeScrubSeekTimer(nullptr)
    , m_pendingEnvelopeScrubTargetMs(-1)
    , m_lastEnvelopeScrubAppliedMs(-1)
    , m_aboutFinishSuppressUntilMs(0)
    , m_aboutFinishStableTicks(0)
    , m_monitorRouteButton(nullptr)
    , m_pitchSlider(nullptr)
    , m_pitchResetButton(nullptr)
    , m_lastControlsPanelWidth(-1)
    , m_monitorRouteAvailable(false)
    , m_monitorRouteEnabled(false)
    , m_simulatedPositionMs(-1)
    , m_isSimulating(false)
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
    m_sessionTimer.start();

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

    // Display panel takes the bulk; exact 80:20 split is enforced in
    // enforcePanelSplit() so panel sizes stay stable while interacting.
    ui->horizontalLayout->setStretch(0, 4);
    ui->horizontalLayout->setStretch(1, 1);
    //ui->frame_3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Track fraVuMeter's own resize so bpmWidget geometry stays in sync
    ui->fraVuMeter->installEventFilter(this);

    ui->frame_4->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->frame_5->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->frame_6->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    ui->frame_4->setMinimumWidth(0);
    ui->frame_5->setMinimumWidth(0);
    ui->frame_6->setMinimumWidth(0);
    ui->butPlay->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    ui->butCue->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    ui->butRew->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    ui->butFwd->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    if (QAbstractButton* beatModeButton = findChild<QAbstractButton*>("butBeatMode"))
        beatModeButton->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    // Track fraVuMeter for resize (keeps bpmWidget geometry correct)
    // and frame_3 for mouse clicks (toggle BPM/VU mode)
    ui->frame_3->installEventFilter(this);
    ui->butPlay->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->butCue->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->butRew->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->butFwd->setMaximumWidth(QWIDGETSIZE_MAX);

    vuMeter = ui->vuMeter;
    vuMeter->setOrientation(Qt::Horizontal);
    vuMeter->LevelColorNormal.setRgb(112, 146, 190);
    vuMeter->LevelColorHigh.setRgb(218, 59, 9);
    vuMeter->LevelColorOff.setRgb(31, 45, 65);
    vuMeter->setLinesPerSegment(2);
    vuMeter->setSpacesBetweenSegments(1);
    vuMeter->setSegmentsPerPeak(2);
    vuMeter->setMargin(3); // slightly wider center gap between left/right channels

    bpmWidget = new PlayerBpmWidget(vuMeter->parentWidget());
    // Initial geometry: will be corrected by eventFilter on first fraVuMeter resize
    bpmWidget->setGeometry(ui->fraVuMeter->rect());
    bpmWidget->setTempoInfo(m_tempoRate, m_syncAdopting);
    bpmWidget->hide();
    connect(bpmWidget, &PlayerBpmWidget::envelopeScrubStarted, this, &PlayerWidget::onEnvelopeScrubStarted);
    connect(bpmWidget, &PlayerBpmWidget::envelopeScrubPositionChanged, this, &PlayerWidget::onEnvelopeScrubPositionChanged);

    for (int i = 0; i < 4; ++i)
        m_beatJumpButtons[i] = nullptr;
    createPerformanceControls();

    QSettings settings;
    applyBeatVisualLayout(settings.value("beatSyncVisualMode", false).toBool());

    timerLevel = new QTimer(this);
    connect(timerLevel, SIGNAL(timeout()), SLOT(timerLevel_timeOut()));

    timerPosition = new QTimer(this);
    connect(timerPosition, SIGNAL(timeout()), SLOT(timerPosition_timeOut()));

    timerVisual = new QTimer(this);
    timerVisual->setInterval(16);  // ~60fps refresh rate (16.67ms ≈ 16ms)
    timerVisual->setTimerType(Qt::PreciseTimer);
    connect(timerVisual, SIGNAL(timeout()), SLOT(timerVisual_timeOut()));

    m_envelopeScrubSeekTimer = new QTimer(this);
    m_envelopeScrubSeekTimer->setSingleShot(true);
    m_envelopeScrubSeekTimer->setInterval(kScrubSeekCoalesceMs);
    connect(m_envelopeScrubSeekTimer, SIGNAL(timeout()), SLOT(applyPendingEnvelopeScrubSeek()));

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
    ui->lblTimeMs->setFont(fonttime);
    ui->lblTimeRemainMs->setFont(fonttime);

    // Keep these labels width-elastic so changing text never expands layouts.
    ui->lblTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    ui->lblTitle->setMinimumWidth(0);
    ui->lblInfo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->lblInfo->setMinimumWidth(0);

    // Keep time labels fixed-width/height to avoid jitter and clipping on macOS.
    // Note: The original code had redundant calls here, but they are kept for context.
    // fixTimeLabelGeometry(ui->lblTime, "00:00");
    // fixTimeLabelGeometry(ui->lblTimeMs, ".8");
    // fixTimeLabelGeometry(ui->lblTimeRemain, "-00:00");
    // fixTimeLabelGeometry(ui->lblTimeRemainMs, ".8");

    auto fixTimeLabelGeometry = [](QLabel* label, const QString& sampleText) {
        if (!label)
            return;
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        const QFontMetrics metrics(label->font());
        label->setFixedWidth(metrics.horizontalAdvance(sampleText) + 8);
        label->setMinimumHeight(metrics.height() + 4);
    };

    fixTimeLabelGeometry(ui->lblTime, "00:00");
    fixTimeLabelGeometry(ui->lblTimeMs, ".8");
    fixTimeLabelGeometry(ui->lblTimeRemain, "-00:00");
    fixTimeLabelGeometry(ui->lblTimeRemainMs, ".8");

    m_isStarted = false;
    m_pendingPlay = false;
    setAcceptDrops(true);
    updateResponsiveLayout();
    enforcePanelSplit();
    QTimer::singleShot(0, this, [this]() {
        enforcePanelSplit();
        //syncDisplayHeightToControls();
    });
    this->stop();

    trackanalyzer = new TrackAnalyzer(this);
    connect(trackanalyzer, SIGNAL(finishGain()), this, SLOT(analyzeGainFinished()));
    connect(trackanalyzer, SIGNAL(finishTempo()), this, SLOT(analyzeTempoFinished()));
    connect(trackanalyzer, SIGNAL(finishEnvelope()), this, SLOT(analyzeEnvelopeFinished()));
}

PlayerWidget::~PlayerWidget()
{
    delete player;
    delete timerPosition;
    delete timerLevel;
    delete trackanalyzer;
    trackanalyzer = nullptr;
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

    // Keep the pitch slider in sync when the rate is set externally (e.g. beat-sync).
    if (m_pitchSlider) {
        const QSignalBlocker blocker(m_pitchSlider);
        m_pitchSlider->setValue(qBound(-240, qRound((rate - 1.0) * 2000.0), 240));
        if (m_pitchResetButton) {
            const int v = m_pitchSlider->value();
            m_pitchResetButton->setText(
                  v == 0 ? QStringLiteral("0.00%")
                       : QString("%1%2%").arg(v > 0 ? "+" : "")
                                 .arg(v / 20.0, 0, 'f', 2));
        }
    }

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

        if (m_isStarted && !timerVisual->isActive()) {
            timerVisual->start();
            m_visualFrameTimer.restart();
            m_lastWaveformRebuildPosMs = -1;
        }

        if (m_CurrentTrack && !bpmWidget->isEnvelopePreloaded()) {
            if (trackanalyzer->finished()) {
                analyzeEnvelopeFinished();
            } else {
                trackanalyzer->setMode(TrackAnalyzer::ENVELOPE);
                trackanalyzer->open(m_CurrentTrack->url());
            }
        }
        applyAutoCueAfterAnalysis(true);
    } else {
        if (timerVisual->isActive())
            timerVisual->stop();
        bpmWidget->hide();
        vuMeter->show();
        applyAutoCueAfterAnalysis(false);
    }
}

void PlayerWidget::setMonitorOutputDeviceId(const QString& deviceId)
{
    m_monitorOutputDeviceId = deviceId.trimmed();
    player->setMonitorDeviceId(m_monitorOutputDeviceId);
    player->setUseMonitorOutput(m_monitorRouteEnabled && m_monitorRouteAvailable);
}

void PlayerWidget::setMonitorVolume(double v)
{
    player->setMonitorVolume(v);
}

void PlayerWidget::setMonitorRouteAvailable(bool available)
{
    m_monitorRouteAvailable = available;
    if (m_monitorRouteButton)
        m_monitorRouteButton->setEnabled(available);

    if (!available && m_monitorRouteEnabled)
        setMonitorRouteEnabled(false);
    else
        player->setUseMonitorOutput(m_monitorRouteEnabled && m_monitorRouteAvailable);
}

void PlayerWidget::setMonitorRouteEnabled(bool enabled)
{
    const bool clamped = enabled && m_monitorRouteAvailable;
    m_monitorRouteEnabled = clamped;

    if (m_monitorRouteButton && m_monitorRouteButton->isChecked() != clamped) {
        const QSignalBlocker blocker(m_monitorRouteButton);
        m_monitorRouteButton->setChecked(clamped);
    }

    player->setUseMonitorOutput(clamped);
}

void PlayerWidget::applyBeatVisualLayout(bool enabled)
{
    // Keep the same outer dimensions in both modes so the player never resizes on toggle
    setMinimumHeight(182);

    ui->fraDisplay->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->fraVuMeter->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->fraVuMeter->setMinimumHeight(100);
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

    drawTitle();
    updateResponsiveLayout();
}

void PlayerWidget::updateResponsiveLayout()
{
    const int width = this->width();
    const bool veryCompact = width < 360;
    const bool compact = width < 500;
    QAbstractButton* const beatModeButton = findChild<QAbstractButton*>("butBeatMode");

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
    const int iconSize = veryCompact ? 18 : (compact ? 22 : 26);

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

    ui->butPlay->setIconSize(QSize(iconSize, iconSize));
    ui->butRew->setIconSize(QSize(iconSize, iconSize));
    ui->butFwd->setIconSize(QSize(iconSize, iconSize));

    for (int i = 0; i < 4; ++i) {
        if (m_beatJumpButtons[i]) {
            m_beatJumpButtons[i]->setMinimumSize(QSize(smallButtonWidth + 8, smallButtonHeight));
            m_beatJumpButtons[i]->setMaximumHeight(smallButtonHeight);
        }
    }
    // Pitch slider row — keep slider and reset button compact
    if (m_pitchSlider) {
        m_pitchSlider->setMaximumHeight(smallButtonHeight);
    }
    if (m_pitchResetButton) {
        m_pitchResetButton->setMinimumHeight(smallButtonHeight);
        m_pitchResetButton->setMaximumHeight(smallButtonHeight);
    }

    //syncDisplayHeightToControls();
}

void PlayerWidget::syncDisplayHeightToControls()
{
    if (!ui || !ui->frame_2 || !ui->frame_3)
        return;

    const int controlsHeight = qMax(ui->frame_2->sizeHint().height(), ui->frame_2->minimumSizeHint().height());
    if (controlsHeight <= 0)
        return;

    if (ui->frame_3->minimumHeight() == controlsHeight && ui->frame_3->maximumHeight() == controlsHeight)
        return;

    ui->frame_3->setMinimumHeight(controlsHeight);
    ui->frame_3->setMaximumHeight(controlsHeight);
}

void PlayerWidget::enforcePanelSplit()
{
    if (!ui || !ui->horizontalLayout)
        return;

    const QMargins margins = ui->horizontalLayout->contentsMargins();
    const int spacing = ui->horizontalLayout->spacing();
    const int available = qMax(0, ui->frame->width() - margins.left() - margins.right() - spacing);

    if (available <= 0)
        return;

    const int minControls = qMax(95, ui->frame_2->minimumSizeHint().width());
    const int maxControls = qMax(minControls, available - 80);
    int controlsWidth = qRound(static_cast<double>(available) * 0.20);
    controlsWidth = qBound(minControls, controlsWidth, maxControls);

    if (controlsWidth == m_lastControlsPanelWidth)
        return;

    m_lastControlsPanelWidth = controlsWidth;

    // Keep controls fixed at ~20%; leave display unconstrained so text/interaction
    // cannot force the top-level window to grow by constraint feedback.
    ui->frame_2->setMinimumWidth(controlsWidth);
    ui->frame_2->setMaximumWidth(controlsWidth);
    ui->frame_3->setMinimumWidth(0);
    ui->frame_3->setMaximumWidth(QWIDGETSIZE_MAX);
}

void PlayerWidget::createPerformanceControls()
{
    QVBoxLayout* controls = qobject_cast<QVBoxLayout*>(ui->frame_2->layout());
    // The rest of the function body is kept as is.
    if (!controls)
        return;

    QFrame* beatJumpFrame = new QFrame(ui->frame_2);
    beatJumpFrame->setObjectName("frameBeatJump");
    QHBoxLayout* beatLayout = new QHBoxLayout(beatJumpFrame);
    beatLayout->setContentsMargins(0, 0, 0, 0);
    beatLayout->setSpacing(2);

    const int beatSteps[2] = { -4, 4 };
    const char* beatLabels[2] = { "-4", "+4" };
    for (int i = 0; i < 4; ++i)
        m_beatJumpButtons[i] = nullptr;

    for (int i = 0; i < 2; ++i) {
        m_beatJumpButtons[i] = new QPushButton(QString::fromLatin1(beatLabels[i]), beatJumpFrame);
        m_beatJumpButtons[i]->setProperty("beats", beatSteps[i]);
        const QString dir = (beatSteps[i] < 0) ? tr("back") : tr("forward");
        m_beatJumpButtons[i]->setToolTip(tr("Jump %1 by 4 beats").arg(dir));
        connect(m_beatJumpButtons[i], SIGNAL(clicked()), this, SLOT(onBeatJumpButtonClicked()));
        beatLayout->addWidget(m_beatJumpButtons[i]);
    }
    controls->addWidget(beatJumpFrame);

    QFrame* monitorFrame = new QFrame(ui->frame_2);
    monitorFrame->setObjectName("frameMonitorRoute");
    QHBoxLayout* monitorLayout = new QHBoxLayout(monitorFrame);
    monitorLayout->setContentsMargins(0, 0, 0, 0);
    monitorLayout->setSpacing(2);

    m_monitorRouteButton = new QPushButton(tr("MON"), monitorFrame);
    m_monitorRouteButton->setObjectName("butMon");
    m_monitorRouteButton->setCheckable(true);
    m_monitorRouteButton->setToolTip(tr("Route this deck to monitor output"));
    m_monitorRouteButton->setEnabled(false);
    connect(m_monitorRouteButton, SIGNAL(toggled(bool)), this, SLOT(on_monitorRoute_toggled(bool)));
    monitorLayout->addWidget(m_monitorRouteButton);
    controls->addWidget(monitorFrame);

    // ── Pitch / tempo fader ──────────────────────────────────────────────────
    // Range +/-12 % in steps of 0.05 % (slider unit = 0.05 %, so range -240..+240).
    // The reset button shows the current offset and snaps back to 0 % on click.
    QFrame* pitchFrame = new QFrame(ui->frame_2);
    pitchFrame->setObjectName("framePitch");
    QHBoxLayout* pitchLayout = new QHBoxLayout(pitchFrame);
    pitchLayout->setContentsMargins(0, 0, 0, 0);
    pitchLayout->setSpacing(2);

    m_pitchSlider = new QSlider(Qt::Horizontal, pitchFrame);
    m_pitchSlider->setObjectName("sliPitch");
    m_pitchSlider->setRange(-240, 240);
    m_pitchSlider->setSingleStep(1);
    m_pitchSlider->setPageStep(10);
    m_pitchSlider->setValue(0);
    m_pitchSlider->setFocusPolicy(Qt::StrongFocus);
    m_pitchSlider->setTickPosition(QSlider::TicksBelow);
    m_pitchSlider->setTickInterval(120);   // marks at -6 %, 0 %, +6 %
    m_pitchSlider->setToolTip(tr("Tempo fader +/-12 % (0.05 % per step). Arrow keys: fine adjust; click % to reset"));
    connect(m_pitchSlider, SIGNAL(valueChanged(int)), this, SLOT(on_pitchSlider_valueChanged(int)));

    m_pitchResetButton = new QPushButton("0.00%", pitchFrame);
    m_pitchResetButton->setObjectName("butPitchReset");
    m_pitchResetButton->setToolTip(tr("Current tempo offset — click to reset to 0 %"));
    QFont smallFont = m_pitchResetButton->font();
    smallFont.setPointSizeF(smallFont.pointSizeF() * 0.82);
    m_pitchResetButton->setFont(smallFont);
    m_pitchResetButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    const QFontMetrics pitchResetMetrics(m_pitchResetButton->font());
    const int pitchResetWidth = pitchResetMetrics.horizontalAdvance(QStringLiteral("+12.00%")) + 14;
    m_pitchResetButton->setFixedWidth(pitchResetWidth);
    connect(m_pitchResetButton, &QPushButton::clicked, this, [this]() {
        if (m_pitchSlider)
            m_pitchSlider->setValue(0);
    });

    pitchLayout->addWidget(m_pitchSlider, 1);
    pitchLayout->addWidget(m_pitchResetButton);
    controls->addWidget(pitchFrame);
}

QString PlayerWidget::currentTrackKey() const
{
    if (!m_CurrentTrack)
        return QString();
    return m_CurrentTrack->url().toLocalFile();
}

void PlayerWidget::jumpByBeats(int beatCount)
{
    if (!m_CurrentTrack || beatCount == 0)
        return;
    // If we are in VU mode and no beat analysis is available, jump by beats might be meaningless.
    // We rely on the caller (onBeatJumpButtonClicked) to handle this contextually if needed.
        return;

    const int curMs = QTime(0, 0).msecsTo(player->position());
    const int lenMs = QTime(0, 0).msecsTo(player->length());
    const int fallbackBeatMs = (m_bpm > 0) ? qRound(60000.0 / static_cast<double>(m_bpm)) : 500;

    int targetMs = curMs;

    const double exactBpm = trackanalyzer ? trackanalyzer->exactBpm() : 0.0;
    if (m_bpm > 0 && exactBpm > 0.0 && m_beatPosition.isValid()) {
        const double beatMs = 60000.0 / exactBpm;
        const qint64 anchorMs = QTime(0, 0).msecsTo(m_beatPosition);
        const double beatIndex = (static_cast<double>(curMs) - static_cast<double>(anchorMs)) / beatMs;
        const qint64 baseBeatIndex = (beatCount < 0)
                ? static_cast<qint64>(qFloor(beatIndex))
                : static_cast<qint64>(qCeil(beatIndex));
        const qint64 targetBeatIndex = baseBeatIndex + static_cast<qint64>(beatCount);
        targetMs = static_cast<int>(qRound(static_cast<double>(anchorMs) + static_cast<double>(targetBeatIndex) * beatMs));

        if (beatCount < 0 && targetMs >= curMs)
            targetMs -= qMax(1, qRound(beatMs));
        else if (beatCount > 0 && targetMs <= curMs)
            targetMs += qMax(1, qRound(beatMs));
    } else {
        targetMs = curMs + beatCount * fallbackBeatMs;
    }

    // Final semantic guard.
    if (beatCount < 0 && targetMs >= curMs)
        targetMs = curMs - qMax(1, qAbs(beatCount) * fallbackBeatMs);
    else if (beatCount > 0 && targetMs <= curMs)
        targetMs = curMs + qMax(1, qAbs(beatCount) * fallbackBeatMs);

    if (targetMs < 0)
        targetMs = 0;
    if (lenMs > 0)
        targetMs = qMin(targetMs, lenMs);

    const QTime targetPos = QTime(0, 0).addMSecs(targetMs);
    player->setPosition(targetPos);
    ui->butCue->setChecked(false);
    bpmWidget->setState(m_bpm, targetPos, m_beatPosition, m_isStarted, m_bpmAnalyzed);
    updateTimeAndPositionDisplay(false);
}

void PlayerWidget::onBeatJumpButtonClicked()
{
    QObject* src = sender();
    if (!src)
        return;

    int beats = src->property("beats").toInt();
    if (beats == 0) {
        if (QAbstractButton* btn = qobject_cast<QAbstractButton*>(src)) {
            const QString text = btn->text().trimmed();
            if (text.startsWith('-'))
                beats = -4;
            else if (text.startsWith('+'))
                beats = 4;
        }
    }

    if (beats == 0)
        return;
    jumpByBeats(beats);
}

void PlayerWidget::onEnvelopeScrubStarted()
{
    m_envelopeScrubbing = true;
    m_envelopeScrubAnchorMs = QTime(0, 0).msecsTo(player->position());
    m_pendingEnvelopeScrubTargetMs = -1;
    m_lastEnvelopeScrubAppliedMs = -1;
    if (m_envelopeScrubSeekTimer->isActive())
        m_envelopeScrubSeekTimer->stop();
    suppressAboutFinishForMs(800);
    qDebug() << "SCRUB start:" << objectName()
             << "anchorMs=" << m_envelopeScrubAnchorMs
             << "playing=" << player->isPlaying();
}

void PlayerWidget::onEnvelopeScrubPositionChanged(double normalizedPosition, bool finished)
{
    if (!m_CurrentTrack)
        return;

    if (!m_envelopeScrubbing)
        m_envelopeScrubAnchorMs = QTime(0, 0).msecsTo(player->position());

    m_envelopeScrubbing = !finished;

    const int windowMs = bpmWidget->windowMilliseconds();
    const double deltaNorm = qBound(-1.0, normalizedPosition, 1.0);
    int targetMs = m_envelopeScrubAnchorMs - qRound(deltaNorm * static_cast<double>(windowMs) * kScrubSeekGain);

    const int lenMs = QTime(0, 0).msecsTo(player->length());
    if (targetMs < 0)
        targetMs = 0;
    if (lenMs > 0)
        targetMs = qMin(targetMs, lenMs);

    qDebug() << "SCRUB move:" << objectName()
             << "deltaNorm=" << deltaNorm
             << "windowMs=" << windowMs
             << "anchorMs=" << m_envelopeScrubAnchorMs
             << "targetMs=" << targetMs
             << "finished=" << finished;

    if (!finished) {
        m_pendingEnvelopeScrubTargetMs = targetMs;

        // Skip tiny target changes to avoid seek storm and non-linear feel.
        if (m_lastEnvelopeScrubAppliedMs >= 0
            && qAbs(m_pendingEnvelopeScrubTargetMs - m_lastEnvelopeScrubAppliedMs) < kScrubSeekMinDeltaMs) {
            return;
        }

        if (!m_envelopeScrubSeekTimer->isActive())
            m_envelopeScrubSeekTimer->start();
    } else {
        if (m_envelopeScrubSeekTimer->isActive())
            m_envelopeScrubSeekTimer->stop();

        m_pendingEnvelopeScrubTargetMs = targetMs;
        applyPendingEnvelopeScrubSeek();
    }

    if (finished) {
        const int expectedMs = targetMs;
        QTimer::singleShot(140, this, [this, expectedMs]() {
            const int actualMs = QTime(0, 0).msecsTo(player->position());
            qDebug() << "SCRUB replay:" << objectName()
                     << "expectedMs=" << expectedMs
                     << "actualMs=" << actualMs
                     << "deltaMs=" << (actualMs - expectedMs)
                     << "playing=" << player->isPlaying();
        });
    }

    updateTimeAndPositionDisplay(false);
    
    // Update waveform window position during scrub so it scrolls in real-time
    if (!finished || !player->isPlaying()) {
        bpmWidget->setState(m_bpm, QTime(0, 0).addMSecs(targetMs), m_beatPosition, m_isStarted, m_bpmAnalyzed);
    }
}

QTime PlayerWidget::calculateCuePosition() const
{
    if (!m_CurrentTrack || !player->isPlaying())
        return QTime();

    // If beat analysis is available and we are in BPM mode, prioritize the beat position.
    if (m_beatVisualMode && m_bpm > 0) {
        // Use the stored beat position if it's valid, otherwise use current time as fallback.
        return m_beatPosition.isValid() ? m_beatPosition : player->position();
    }

    // Otherwise, calculate based on silent end detection (VU mode).
    return determineSilentEndCuePosition(); // Centralized calculation
}

QTime PlayerWidget::determineSilentEndCuePosition() const
{
    if (!trackanalyzer || !trackanalyzer->finished()) {
        return QTime();
    }

    // Use the logic from setPositionMarkers/analyzeEnvelopeFinished for silent end detection.
    QTime fadePoint;
    QTime beatEnd = trackanalyzer->beatActivityEndPosition();
    QTime trackEnd = trackanalyzer->endPosition();

    if (beatEnd.isValid() && beatEnd > QTime(0, 0))
        fadePoint = beatEnd;
    if (trackEnd.isValid() && trackEnd > QTime(0, 0)) {
        // Take the earliest valid point between beat end and track end
        if (!fadePoint.isValid() || trackEnd < fadePoint)
            fadePoint = trackEnd;
    }

    return fadePoint;
}

void PlayerWidget::applyPendingEnvelopeScrubSeek()
{
    if (m_pendingEnvelopeScrubTargetMs < 0)
        return;

    const int targetMs = m_pendingEnvelopeScrubTargetMs;
    m_pendingEnvelopeScrubTargetMs = -1;
    m_lastEnvelopeScrubAppliedMs = targetMs;

    const bool overshotFadePoint = seekOvershootsFadePoint(QTime(0, 0).addMSecs(targetMs));
    if (overshotFadePoint)
        armImmediateAboutFinish();
    else
        suppressAboutFinishForMs(800);
    player->setPosition(QTime(0, 0).addMSecs(targetMs));
    updateTimeAndPositionDisplay();
    
    // Refresh waveform window view when scrubbing while paused
    bpmWidget->setState(m_bpm, player->position(), m_beatPosition, m_isStarted, m_bpmAnalyzed);
}

void PlayerWidget::suppressAboutFinishForMs(int ms)
{
    const qint64 nowMs = m_sessionTimer.isValid() ? m_sessionTimer.elapsed() : 0;
    m_aboutFinishSuppressUntilMs = nowMs + qMax(0, ms);
    m_aboutFinishStableTicks = 0;
    p->isEndAnnounced = false;
}

bool PlayerWidget::seekOvershootsFadePoint(const QTime& targetPos) const
{
    if (!m_isStarted)
        return false;

    const int targetMs = qMax(0, QTime(0, 0).msecsTo(targetPos));

    if (trackanalyzer->finished() && m_skipSilentEnd) {
        QTime fadePoint;
        QTime beatEnd = trackanalyzer->beatActivityEndPosition();
        QTime trackEnd = trackanalyzer->endPosition();
        if (beatEnd.isValid() && beatEnd > QTime(0, 0))
            fadePoint = beatEnd;
        if (trackEnd.isValid() && trackEnd > QTime(0, 0)) {
            if (!fadePoint.isValid() || trackEnd < fadePoint)
                fadePoint = trackEnd;
        }

        if (fadePoint.isValid() && fadePoint > QTime(0, 0)) {
            const int triggerPosMs = qMax(0, QTime(0, 0).msecsTo(fadePoint) - mTrackFinishEmitTime);
            return targetMs >= triggerPosMs;
        }
    }

    const QTime length = player->length();
    const int lengthMs = QTime(0, 0).msecsTo(length);
    if (lengthMs <= 0)
        return false;

    const int triggerPosMs = qMax(0, lengthMs - static_cast<int>(remainCueTime) - mTrackFinishEmitTime);
    return targetMs >= triggerPosMs;
}

void PlayerWidget::armImmediateAboutFinish()
{
    m_aboutFinishSuppressUntilMs = 0;
    m_aboutFinishStableTicks = 2;
    p->isEndAnnounced = false;
}

void PlayerWidget::setInfo(QPair<int, int> info)
{
    QString strTrack = (info.first > 1) ? tr("Tracks") : tr("Track");
    m_infoBaseText = QString("%1 %2       %3 %4")
                         .arg(info.first)
                         .arg(strTrack)
                         .arg(Track::prettyTime(info.second))
                         .arg(tr("Hours"));
    ui->lblInfo->setText(m_infoBaseText);
}

void PlayerWidget::setEqualizer(EqBand band, int value)
{
    //ranging from -24.0 to +12.0.
    player->setEqualizer("band" + QString::number(band), (value - 240) / 10.0);
}

void PlayerWidget::setPositionMarkers()
{
    if (trackanalyzer->finished()) {
        if (m_skipSilentEnd) {
            QTime fadePoint;
            QTime beatEnd = trackanalyzer->beatActivityEndPosition();
            QTime trackEnd = trackanalyzer->endPosition();
            if (beatEnd.isValid() && beatEnd > QTime(0, 0))
                fadePoint = beatEnd;
            if (trackEnd.isValid() && trackEnd > QTime(0, 0)) {
                if (!fadePoint.isValid() || trackEnd < fadePoint)
                    fadePoint = trackEnd;
            }

            if (fadePoint.isValid() && fadePoint > QTime(0, 0)) {
                qDebug() << Q_FUNC_INFO << "fadeStartPoint:" << fadePoint;
                qDebug() << Q_FUNC_INFO << "length:" << trackanalyzer->length();
                remainCueTime = fadePoint.msecsTo(trackanalyzer->length());
            } else {
                remainCueTime = 0;
            }
        } else {
            remainCueTime = 0;
        }

        ui->txtCue->setText("-" + QString::number(remainCueTime / 1000));
    }

    applyAutoCueAfterAnalysis(m_beatVisualMode);
}

void PlayerWidget::applyAutoCueAfterAnalysis(bool preferBeatCue)
{
    // Skip if already playing and we are not explicitly forcing a beat cue (redundant check kept for safety).
    if (m_isStarted && !preferBeatCue) // Only auto cue if not actively trying to beat sync/cue manually while playing
        return;

    // Use the centralized function to determine the best cue position (replaces redundant calls).
    const QTime cuePosition = calculateCuePosition();

    qDebug() << Q_FUNC_INFO << ":" << objectName()
             << " preferBeatCue=" << preferBeatCue
            << " beatPositionValid=" << m_beatPosition.isValid()
             << " beatPositionValid=" << m_beatPosition.isValid()
             << " beatPosition=" << m_beatPosition
             << " analyzerFinished=" << trackanalyzer->finished()
             << " calculatedCuePosition=" << cuePosition;

    if (cuePosition > QTime(0, 0))
        player->setPosition(cuePosition);

    bpmWidget->setTrackLength(player->length());
    ui->butCue->setChecked(true);
    bpmWidget->setState(m_bpm, cuePosition, m_beatPosition, m_isStarted, m_bpmAnalyzed);
    updateTimeAndPositionDisplay(false);
}

void PlayerWidget::play()
{
    m_isStarted = true;
    if (m_CurrentTrack) {
        ui->butPlay->setIcon(QIcon(":pause.png"));
        ui->butPlay->setChecked(true);
        player->play();
        ui->butCue->setChecked(false);
        if (!m_liveEnvelopeStarted) {
            // Keep pre-loaded envelope and continue with live samples.
            m_liveEnvelopeStarted = true;
            m_liveEnvelopeSmoothed = 0.0f;
            m_pendingEnvelope.clear();
        }
        timerLevel->start(50);  // Sample audio levels every 50ms
        timerPosition->start(100);
        if (m_beatVisualMode) {
            timerVisual->start();  // High-frequency waveform updates for smooth display
            m_visualFrameTimer.restart();
        }
        m_lastWaveformRebuildPosMs = -1;  // Reset for fresh rebuild on play
        m_isSimulating = false;  // Not simulating when playing
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
    timerLevel->stop();
    timerPosition->stop();
    timerVisual->stop();
    m_isSimulating = false;
    vuMeter->reset();
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
    player->stop();
    timerLevel->stop();
    timerPosition->stop();
    timerVisual->stop();
    m_isSimulating = false;
    vuMeter->reset();
    bpmWidget->clearEnvelope();
    m_liveEnvelopeStarted = false;
    m_liveEnvelopeSmoothed = 0.0f;
    m_pendingEnvelope.clear();
    m_visualLatencyMs = 0;
    m_lastWaveformRebuildPosMs = -1;
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

void PlayerWidget::analyzeGainFinished()
{
    qDebug() << Q_FUNC_INFO << ":" << objectName();
    // got gain factor -> emit
    if (trackanalyzer->gainDB() != TrackAnalyzer::GAIN_INVALID) {
        Q_EMIT gainChanged(trackanalyzer->gainFactor());
    }
    if (m_CurrentTrack) {
        setPositionMarkers();
        updateTimeAndPositionDisplay();

        qDebug() << Q_FUNC_INFO << ":" << objectName()
             << " prep marker update done"
             << " analyzerFinished=" << trackanalyzer->finished()
             << " cueChecked=" << ui->butCue->isChecked();

        if (m_beatSyncEnabled) {
            QSettings settings;
            const bool analyzeTempo = settings.value("beatSyncAnalyzeTempo", true).toBool();
            if (analyzeTempo && !m_bpmAnalyzed)
                bpmWidget->setState(0, player->position(), m_beatPosition, m_isStarted, false);
        }

        if (m_beatVisualMode && !bpmWidget->isEnvelopePreloaded())
            analyzeEnvelopeFinished();
    }
}

void PlayerWidget::analyzeTempoFinished()
{
    if (!m_CurrentTrack)
        return;

    m_bpmAnalyzed = true;
    m_bpm = trackanalyzer->bpm();
    m_beatPosition = trackanalyzer->beatPosition();

    if (m_bpm > 0) {
        emit tempoChanged(m_bpm, m_beatPosition);
    }

    if (m_CurrentTrack) {
        qDebug() << Q_FUNC_INFO << "Storing tempo cache: bpm=" << m_bpm << "exactBpm=" << trackanalyzer->exactBpm();
        storeCachedTempo(m_CurrentTrack->url(), m_bpm, trackanalyzer->exactBpm(), m_beatPosition);
    }

    applyAutoCueAfterAnalysis(m_beatVisualMode);
    bpmWidget->setTrackLength(player->length());
    bpmWidget->setState(m_bpm, trackanalyzer->exactBpm(), player->position(), m_beatPosition, m_isStarted, true);
}

void PlayerWidget::analyzeEnvelopeFinished()
{
    if (!m_CurrentTrack)
        return;

    const QVector<float> env = trackanalyzer->amplitudeEnvelope();
    if (env.isEmpty())
        return;

    qDebug() << "ENVELOPE analysis finished:" << objectName() << "samples=" << env.size();
    storeCachedEnvelope(m_CurrentTrack->url(), env, QTime(0, 0).msecsTo(trackanalyzer->length()));
    bpmWidget->setTrackLength(trackanalyzer->length());
    bpmWidget->setPreloadedEnvelope(env, kEnvelopeAnalysisIntervalMs);

    // If beat-sync mode is active and we found a meaningful beat-activity end,
    // override the cue time so voice/instrument-free tail passages are skipped.
    // Use the earliest valid cue point between beat activity end and the detected
    // track end so fade timing does not wait past the actual song end.
    if (m_beatSyncEnabled && m_skipSilentEnd) {
        const QTime beatEnd = trackanalyzer->beatActivityEndPosition();
        const QTime trackEnd = trackanalyzer->endPosition();
        QTime fadePoint;

        if (beatEnd.isValid() && beatEnd > QTime(0, 0))
            fadePoint = beatEnd;
        if (trackEnd.isValid() && trackEnd > QTime(0, 0)) {
            if (!fadePoint.isValid() || trackEnd < fadePoint)
                fadePoint = trackEnd;
        }

        if (fadePoint.isValid() && fadePoint > QTime(0, 0)) {
            const QTime trackLen = trackanalyzer->length();
            const int newRemainMs = fadePoint.msecsTo(trackLen);
            if (newRemainMs > 0 && newRemainMs > static_cast<int>(remainCueTime)) {
                remainCueTime = newRemainMs;
                ui->txtCue->setText("-" + QString::number(remainCueTime / 1000));
                qDebug() << Q_FUNC_INFO << "fade point overrides cue time:"
                         << fadePoint << "remainCue=" << remainCueTime;
            }
        }
    }
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

    if (m_bpm == trackBpm && m_bpmAnalyzed)
        return;

    if (m_bpm <= 0 || !m_bpmAnalyzed) {
        m_bpm = trackBpm;
        m_bpmAnalyzed = true;
        bpmWidget->setState(m_bpm, player->position(), m_beatPosition, m_isStarted, true);
    }
}

void PlayerWidget::timerLevel_timeOut()
{
    const double inLeft = player->levelLeft();
    const double inRight = player->levelRight();
    const double outLeft = player->levelOutLeft();
    const double outRight = player->levelOutRight();

    // Live envelope: blend RMS body with peak/detail so it keeps visible nuance.
    const float l = static_cast<float>(qBound(0.0, inLeft, 1.0));
    const float r = static_cast<float>(qBound(0.0, inRight, 1.0));
    const float rms = std::sqrt((l * l + r * r) * 0.5f);
    const float peak = qMax(l, r);
    const float detail = qBound(0.0f, peak - rms, 1.0f);
    const float target = qBound(0.0f, 0.70f * rms + 0.30f * peak + 0.20f * detail, 1.0f);
    m_liveEnvelopeSmoothed = 0.55f * m_liveEnvelopeSmoothed + 0.45f * target;
    const float env = qBound(0.0f, std::pow(m_liveEnvelopeSmoothed, 0.90f), 1.0f);

    // Align visuals to audible output using device latency from backend.
    m_visualLatencyMs = qBound(0, player->outputLatencyMs(), 250);
    const int rawPosMs = QTime(0, 0).msecsTo(player->position());
    const int visPosMs = qMax(0, rawPosMs - m_visualLatencyMs);
    const int delaySamples = qBound(0, (m_visualLatencyMs + (kEnvelopeAnalysisIntervalMs / 2)) / kEnvelopeAnalysisIntervalMs, 32);
    m_pendingEnvelope.enqueue(env);
    if (m_pendingEnvelope.size() > delaySamples && !bpmWidget->isEnvelopePreloaded())
        bpmWidget->appendEnvelopeSampleAt(visPosMs, m_pendingEnvelope.dequeue());

    if (m_beatVisualMode) {
        // Visual waveform display is updated by timerVisual_timeOut() at high frequency (~60fps).
        // This keeps visual smooth while timerLevel_timeOut() still appends audio samples.
    } else {
        vuMeter->setValueLeft(inLeft);
        vuMeter->setValueRight(inRight);
    }
    Q_EMIT levelChanged(outLeft, outRight);
}

void PlayerWidget::timerPosition_timeOut()
{
    if (m_isStarted) {
        // Use backend running state for end detection. Position can lead audible
        // output due buffering/read-ahead and would stop playback prematurely.
        if (!player->isPlaying()) {
            qDebug() << Q_FUNC_INFO << ": backend stopped, pausing";
            pause();
            return;
        }
    }

    updateTimeAndPositionDisplay(true);
}

void PlayerWidget::timerVisual_timeOut()
{
    // High-frequency visual update, but only rebuild waveform when position has moved enough.
    // This keeps the display smooth without jittering amplitude from constant recalculation.
    if (!bpmWidget || !m_beatVisualMode)
        return;

    if (!m_isStarted)
        return;

    // Playing: use actual player position
    if (!player->isPlaying()) {
        qDebug() << Q_FUNC_INFO << ": backend stopped, pausing";
        pause();
        return;
    }

    const QTime length = player->length();
    const QTime currentPos = player->position();

    const QTime displayPos = (length > QTime(0, 0) && currentPos > length) ? length : currentPos;

    const int visPosMs = qMax(0, QTime(0, 0).msecsTo(displayPos) - m_visualLatencyMs);

    // Update simulated position to track real position while playing.
    m_simulatedPositionMs = visPosMs;
    m_isSimulating = false;

    // Ensure track length is set for proper end-of-track windowing
    bpmWidget->setTrackLength(player->length());

    // Only rebuild waveform display if we've moved more than ~8ms to avoid jittering
    // while still maintaining responsive scrolling.
    constexpr int kRebuildThresholdMs = 8;
    if (m_lastWaveformRebuildPosMs < 0 || qAbs(visPosMs - m_lastWaveformRebuildPosMs) >= kRebuildThresholdMs) {
        m_lastWaveformRebuildPosMs = visPosMs;
        const double exactBpm = trackanalyzer ? trackanalyzer->exactBpm() : static_cast<double>(m_bpm);
        bpmWidget->setState(m_bpm, exactBpm, QTime(0, 0).addMSecs(visPosMs), m_beatPosition, m_isStarted, m_bpmAnalyzed);
    }
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
    m_bpmAnalyzed = false;
    m_bpm = 0;
    m_tempoRate = 1.0;
    m_syncAdopting = false;
    m_visualLatencyMs = 0;
    m_pendingEnvelope.clear();
    m_liveEnvelopeStarted = false;
    m_liveEnvelopeSmoothed = 0.0f;
    m_beatPosition = QTime();
    bpmWidget->setState(m_bpm, QTime(0, 0), m_beatPosition, false, track == nullptr);
    bpmWidget->setTempoInfo(m_tempoRate, m_syncAdopting);
    bpmWidget->setTrackLength(QTime(0, 0));
    bpmWidget->clearEnvelope();
    m_envelopeScrubbing = false;

    if (track != nullptr) {

        drawTitle();

        bool doPlay = m_isStarted;
        player->stop();

        QUrl url = track->url();
        player->open(url);

        // Check cache first to skip redundant analyzer loading
        QSettings settings;
        const bool analyzeTempo = settings.value("beatSyncAnalyzeTempo", true).toBool();
        const CachedTempo cachedTempo = loadCachedTempo(url);
        const CachedEnvelope cachedEnvelope = loadCachedEnvelope(url);

        // Apply cached data immediately to minimize wait time
        if (cachedTempo.valid && cachedTempo.bpm > 0) {
            m_bpmAnalyzed = true;
            m_bpm = cachedTempo.bpm;
            m_beatPosition = QTime(0, 0).addMSecs(cachedTempo.beatOffsetMs);
            qDebug() << Q_FUNC_INFO << "Loaded cached tempo: bpm=" << m_bpm << "exactBpm=" << cachedTempo.exactBpm << "beatOffsetMs=" << cachedTempo.beatOffsetMs;
            bpmWidget->setState(m_bpm, cachedTempo.exactBpm, player->position(), m_beatPosition, m_isStarted, true);
            Q_EMIT tempoChanged(m_bpm, m_beatPosition);
            // Apply auto-cue when cached BPM is available (before async analysis completes)
            applyAutoCueAfterAnalysis(m_beatVisualMode);
        }

        if (cachedEnvelope.valid) {
            if (cachedEnvelope.durationMs > 0)
                bpmWidget->setTrackLength(QTime(0, 0).addMSecs(cachedEnvelope.durationMs));
            else if (m_CurrentTrack && m_CurrentTrack->length() > 0)
                bpmWidget->setTrackLength(QTime(0, 0).addSecs(m_CurrentTrack->length()));
            bpmWidget->setPreloadedEnvelope(cachedEnvelope.samples, kEnvelopeAnalysisIntervalMs);
        }

        // Run exactly one analyzer pass when any analysis-dependent feature needs it.
        const bool haveCachedTempo = cachedTempo.valid && cachedTempo.bpm > 0;
        const bool needCueAnalysis = m_skipSilentBegin || m_skipSilentEnd;
        const bool needEnvelope = m_beatVisualMode && !cachedEnvelope.valid;
        const bool needTempo = m_beatSyncEnabled && analyzeTempo && !haveCachedTempo;
        const bool needAnalysis = needCueAnalysis || needEnvelope || needTempo;

        if (needAnalysis) {
            if (needTempo)
                trackanalyzer->setTempoScanDurationSeconds(qMax(16, settings.value("beatSyncScanSeconds", 16).toInt()));
            trackanalyzer->setMode(needTempo ? TrackAnalyzer::TEMPO : TrackAnalyzer::STANDARD);
            trackanalyzer->open(url);
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
    enforcePanelSplit();
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
            ui->lblTitle->setStyleSheet("* { font-size: 16pt; }");
        else if (width < 400)
            ui->lblTitle->setStyleSheet("* { font-size: 17pt; }");
        else
            ui->lblTitle->setStyleSheet("* { font-size: 19pt; }");
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
    if (length > QTime(0, 0) && curpos > length)
        curpos = length;
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

    const bool nearEndByTime = (remainMs - remainCueTime - mTrackFinishEmitTime <= 0
                                && 0 < remainMs);
    const bool aboutFinishCandidate = (nearEndByTime || m_isHanging) && m_isStarted;
    const qint64 nowMs = m_sessionTimer.isValid() ? m_sessionTimer.elapsed() : 0;
    const bool suppressionActive = nowMs < m_aboutFinishSuppressUntilMs;

    if (aboutFinishCandidate && !suppressionActive)
        ++m_aboutFinishStableTicks;
    else
        m_aboutFinishStableTicks = 0;

    if (m_aboutFinishStableTicks >= 3) {
        if (!p->isEndAnnounced) {
            qDebug() << Q_FUNC_INFO << ":" << objectName() << " EMIT aboutFinished";
            qDebug() << Q_FUNC_INFO << ": curpos:" << curpos;
            qDebug() << Q_FUNC_INFO << ": remainMs:" << remainMs;
            qDebug() << Q_FUNC_INFO << ": remainCueTime:" << remainCueTime;
            qDebug() << Q_FUNC_INFO << ": mTrackFinishEmitTime:" << mTrackFinishEmitTime;
            qDebug() << Q_FUNC_INFO << ": m_isHanging:" << m_isHanging;
            qDebug() << Q_FUNC_INFO << ": stableTicks:" << m_aboutFinishStableTicks;

            //send signals only once
            p->isEndAnnounced = true;
            Q_EMIT aboutFinished();
            Q_EMIT trackPlayed(m_CurrentTrack);
        }
    } else
        p->isEndAnnounced = false;

    QString highlight;
    if (m_bpm > 0)
        highlight = QString("BPM %1").arg(m_bpm);
    if (qAbs(m_tempoRate - 1.0) > 0.01) {
        if (!highlight.isEmpty())
            highlight += " | ";
        highlight += QString("x%1").arg(QString::number(m_tempoRate, 'f', 2));
    }
    if (remainMs > 0 && (remainMs - remainCueTime <= qMax(10000, mTrackFinishEmitTime + 2000))) {
        if (!highlight.isEmpty())
            highlight += " | ";
        highlight += tr("MIX OUT");
    }

    if (!highlight.isEmpty()) {
        if (!m_infoBaseText.isEmpty())
            ui->lblInfo->setText(m_infoBaseText + "   [ " + highlight + " ]");
        else
            ui->lblInfo->setText(highlight);
    } else if (!m_infoBaseText.isEmpty()) {
        ui->lblInfo->setText(m_infoBaseText);
    }

    //update position slider only if triggered by timer
    if (isPassive) {
        // Do not fight user input while dragging the seek slider.
        if (!ui->sliPosition->isSliderDown()) {
            const int lengthMs = QTime(0, 0).msecsTo(length);
            const int curMs = QTime(0, 0).msecsTo(curpos);
            const int minValue = ui->sliPosition->minimum();
            const int maxValue = ui->sliPosition->maximum();
            if (lengthMs > 0 && maxValue > minValue) {
                const int mapped = minValue
                    + qRound((static_cast<double>(qBound(0, curMs, lengthMs)) / static_cast<double>(lengthMs))
                             * static_cast<double>(maxValue - minValue));
                ui->sliPosition->setValue(mapped);
            } else {
                ui->sliPosition->setValue(minValue);
            }
        }
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

    // Analysis can finish before the async player load completes.
    // Re-apply markers here so auto-cue is reliably set on both decks.
    setPositionMarkers();

    qDebug() << Q_FUNC_INFO << ":" << objectName()
             << " player load finished"
             << " analyzerFinished=" << trackanalyzer->finished()
             << " cueChecked=" << ui->butCue->isChecked();

    if (m_pendingPlay) {
        m_pendingPlay = false;
        play();
    }
}

void PlayerWidget::on_butRew_clicked()
{
    if (player->position() < QTime(0, 0, 3))
        emit rewindPressed();
    else {
        suppressAboutFinishForMs(1000);
        player->setPosition(QTime(0, 0, 0));
    }
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
    QTime len = player->length();
    if (len == QTime(0, 0) && m_CurrentTrack)
        len = QTime(0, 0, 0).addSecs(m_CurrentTrack->length());

    const int lengthMs = QTime(0, 0).msecsTo(len);
    const int minValue = ui->sliPosition->minimum();
    const int maxValue = ui->sliPosition->maximum();
    if (lengthMs <= 0 || maxValue <= minValue)
        return;

    const int clampedValue = qBound(minValue, value, maxValue);
    const double norm = static_cast<double>(clampedValue - minValue) / static_cast<double>(maxValue - minValue);
    const int targetMs = qBound(0, qRound(norm * static_cast<double>(lengthMs)), lengthMs);
    const QTime pos = QTime(0, 0).addMSecs(targetMs);

    if (seekOvershootsFadePoint(pos))
        armImmediateAboutFinish();
    else
        suppressAboutFinishForMs(1200);

    player->setPosition(pos);
    bpmWidget->setState(m_bpm, pos, m_beatPosition, m_isStarted, m_bpmAnalyzed);

    // Update labels immediately with the target position for visual feedback
    const int remainMs = qMax(0, targetMs <= lengthMs ? (lengthMs - targetMs) : 0);
    ui->lblTime->setText(pos.toString("mm:ss"));
    ui->lblTimeMs->setText("." + pos.toString("zzz").left(1));
    ui->lblTimeRemain->setText("-" + QTime(0, 0).addMSecs(remainMs).toString("mm:ss"));
    ui->lblTimeRemainMs->setText("." + QTime(0, 0).addMSecs(remainMs).toString("zzz").left(1));
    ui->butCue->setChecked(false);
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
    if (!ui->butCue->isChecked()) { // Only act if the button is not already checked/active
        // Calculate cue position based on current mode/analysis results (using calculateCuePosition).
        const QTime cuePosition = calculateCuePosition();

        //ToDo: Visualize skipped silent at start and at the end (color bar)
        this->pause();

        // Use the variable defined above: (No redefinition needed)
        qDebug() << Q_FUNC_INFO << "cuePosition calculated:" << cuePosition << "m_beatCueEnabled:" << m_beatCueEnabled << " beatPosition:" << m_beatPosition << " bpm:" << m_bpm;

        suppressAboutFinishForMs(1000);
        player->setPosition(cuePosition);
        player->setPosition(cuePosition);
        bpmWidget->setTrackLength(player->length());
        player->setPosition(cuePosition);
        bpmWidget->setTrackLength(player->length());
        bpmWidget->setState(m_bpm, cuePosition, m_beatPosition, m_isStarted, m_bpmAnalyzed);
        bpmWidget->setState(m_bpm, cuePosition, m_beatPosition, m_isStarted, m_bpmAnalyzed);
        updateTimeAndPositionDisplay();
    }
}

QTime PlayerWidget::currentPosition() const
{
    return player->position();
}

double PlayerWidget::exactBpmForSync() const
{
    if (trackanalyzer) {
        const double exact = trackanalyzer->exactBpm();
        if (exact > 0.0)
            return exact;
    }
    return (m_bpm > 0) ? static_cast<double>(m_bpm) : 0.0;
}

void PlayerWidget::alignCueToReferenceBeat(double referenceBpm, const QTime& referencePosition,
                                            const QTime& referenceBeatAnchor)
{
    const double ownBpm = exactBpmForSync();
    if (m_isStarted || ownBpm <= 0.0 || referenceBpm <= 0.0)
        return;

    const double ownBeatMs = 60000.0 / ownBpm;
    const double refBeatMs = 60000.0 / referenceBpm;

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
    const QTime baseCue = m_beatPosition.isValid() ? m_beatPosition : trackanalyzer->startPosition();
    const qint64 baseCueMs = QTime(0, 0).msecsTo(baseCue);

    const qint64 bestMs = baseCueMs + static_cast<qint64>(targetBarPhase + 0.5);
    player->setPosition(QTime(0, 0).addMSecs(static_cast<int>(bestMs)));
    updateTimeAndPositionDisplay(false);
}

void PlayerWidget::syncNowToReferenceBeat(double referenceBpm, const QTime& referencePosition,
                                          const QTime& referenceBeatAnchor)
{
    // Works while playing: seeks to the nearest bar-aligned position that puts
    // this deck on the same beat-of-the-bar as the reference deck, so both
    // decks share the same beat-1 in a standard 4/4 bar.
    const double ownBpm = exactBpmForSync();
    if (ownBpm <= 0.0 || referenceBpm <= 0.0)
        return;

    const double ownBeatMs   = 60000.0 / ownBpm;
    const double refBeatMs   = 60000.0 / referenceBpm;

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
    const QTime baseCue   = m_beatPosition.isValid() ? m_beatPosition : trackanalyzer->startPosition();
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
    if (qAbs(referenceBpm - ownBpm) > 1.0e-6)
        setTempoRate(referenceBpm / ownBpm);
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

void PlayerWidget::on_monitorRoute_toggled(bool checked)
{
    const bool enabled = checked && m_monitorRouteAvailable;
    m_monitorRouteEnabled = enabled;
    if (m_monitorRouteButton && m_monitorRouteButton->isChecked() != enabled) {
        const QSignalBlocker blocker(m_monitorRouteButton);
        m_monitorRouteButton->setChecked(enabled);
    }

    player->setUseMonitorOutput(enabled);
    Q_EMIT monitorRouteToggled(enabled);
}

void PlayerWidget::on_pitchSlider_valueChanged(int value)
{
    // Each slider unit = 0.05 %, so divide by 2000 to get a rate multiplier.
    const double rate = 1.0 + value / 2000.0;
    m_tempoRate = rate;
    player->setRate(rate);
    bpmWidget->setTempoInfo(m_tempoRate, m_syncAdopting);

    if (m_pitchResetButton) {
        m_pitchResetButton->setText(
            value == 0 ? QStringLiteral("0.0%")
                       : QString("%1%2%").arg(value > 0 ? "+" : "")
                                        .arg(value / 20.0, 0, 'f', 2));
    }

    if (std::fabs(m_tempoRate - 1.0) < 0.001 && !m_syncAdopting)
        updateSyncButtonState(false);
}
