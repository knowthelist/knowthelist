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

#include "playercuemanager.h"

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
#include <cmath>

struct PlayerWidgetPrivate {
    bool isEndAnnounced;
};

namespace {
constexpr int kEnvelopeAnalysisIntervalMs = 8; // TrackAnalyzer uses 120 fps for envelope analysis.
constexpr double kMinimumBarPhaseConfidence = 0.58;
constexpr double kScrubSeekGain = 1.5;
constexpr int kScrubSeekMinDeltaMs = 10;
constexpr int kScrubSeekCoalesceMs = 20;

double normalizeSyncBpm(double bpm)
{
    if (bpm <= 0.0)
        return bpm;

    // Fold half/double-time BPM detections into a practical DJ range.
    while (bpm < 60.0)
        bpm *= 2.0;
    while (bpm > 220.0)
        bpm *= 0.5;
    return bpm;
}

void setElidedLabelText(QLabel* label, const QString& text)
{
    if (!label)
        return;

    // Use the label's actual layout allocation. QLabel margins are already part
    // of contentsRect(), so subtracting them again would under-report the slot.
    const int availableWidth = label->contentsRect().width();
    if (availableWidth <= 0) {
        label->clear();
        return;
    }

    const QFontMetrics metrics(label->font());
    const QString fittedText = metrics.horizontalAdvance(text) <= availableWidth
        ? text
        : metrics.elidedText(text, Qt::ElideRight, availableWidth);
    label->setText(fittedText);
}

void setInfoLabelText(QLabel* label, const QString& baseText, const QString& highlight)
{
    if (!label)
        return;

    const int availableWidth = label->contentsRect().width();
    if (availableWidth <= 0) {
        label->clear();
        return;
    }

    const QFontMetrics metrics(label->font());
    const QString suffix = highlight.isEmpty() ? QString() : QString("   [ %1 ]").arg(highlight);
    const QString combined = baseText + suffix;

    // The playlist summary is the useful persistent part. Do not shorten it
    // just because a transient BPM or mix-out status is also being displayed.
    if (!baseText.isEmpty() && metrics.horizontalAdvance(combined) > availableWidth) {
        setElidedLabelText(label, baseText);
        return;
    }

    setElidedLabelText(label, combined.isEmpty() ? highlight : combined);
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
    , m_skipSilentEnd(true)
    , m_skipSilentBegin(true)
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
    , m_cuePosition()
    , m_barAnchorPosition()
    , m_barPhaseConfidence(0.0)
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
    , m_syncButton(nullptr)
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
    // The legacy generated UI can carry a 124 px cap; the display should fill
    // the available height alongside the controls panel.
    ui->fraDisplay->setMaximumHeight(QWIDGETSIZE_MAX);

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

    ui->frame_4->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->frame_5->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->frame_6->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->frame_4->setMinimumWidth(0);
    ui->frame_5->setMinimumWidth(0);
    ui->frame_6->setMinimumWidth(0);
    ui->butPlay->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    ui->butCue->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    ui->butRew->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    ui->butFwd->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    if (QAbstractButton* beatModeButton = findChild<QAbstractButton*>("butBeatMode"))
        beatModeButton->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    ui->butPlay->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->butCue->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->butRew->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->butFwd->setMaximumWidth(QWIDGETSIZE_MAX);

    ui->sliPosition->setFixedHeight(14);

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

    // ---- Sub-object instantiation ----
    m_cueManager = std::make_unique<PlayerCueManager>(*this);

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
    font.setPointSize(std::max(8, font.pointSize()));
    font.setLetterSpacing(QFont::PercentageSpacing, 95);

    // Force concrete monospace fonts to prevent digit width jitter on time labels.
    // Fira Mono is bundled via Qt resources and loaded in main.cpp, so it's always available.
    QFont fonttime = ui->lblTime->font();
    static const char *monoCandidates[] = { "Fira Mono", "Menlo", "Courier New" };
    for (const char *candidate : monoCandidates) {
        QFont testFont(candidate);
        if (QFontInfo(testFont).family() == candidate) {
            fonttime.setFamily(candidate);
            break; // Found a valid monospace font
        }
    }
    
    // Force monospace family on all time labels to prevent digit width jitter.
#if defined(Q_OS_DARWIN)
    int newSize = font.pointSize();
    fonttime.setPointSize(std::max(14, fonttime.pointSize() + 4));
    fonttime.setWeight(QFont::ExtraLight); // thinner
#else
    int newSize = font.pointSize();
    fonttime.setPointSize(std::max(14, fonttime.pointSize() + 2));
#endif
    font.setPointSize(newSize);
    fonttime.setLetterSpacing(QFont::PercentageSpacing, 96);
    ui->lblInfo->setFont(font);
    ui->lblTime->setFont(fonttime);
    ui->lblTimeRemain->setFont(fonttime);

    //.ms labels use a smaller font, aligned bottom with time labels
    QFont fonttimems = fonttime;
    fonttimems.setPointSize(std::max(10, fonttimems.pointSize() - 4));
    ui->lblTimeMs->setFont(fonttimems);
    ui->lblTimeRemainMs->setFont(fonttimems);

    // Keep these labels width-elastic so changing text never expands layouts.
    ui->lblTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    ui->lblTitle->setMinimumWidth(0);
    ui->lblTitle->setMinimumHeight(10);
    ui->lblTitle->setWordWrap(false);
    // Let the playlist summary use the space left between the two time displays.
    ui->lblInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    ui->lblInfo->setMinimumWidth(0);
    ui->lblInfo->setMinimumHeight(12);
    ui->lblInfo->setMargin(0);
    ui->lblInfo->setAlignment(Qt::AlignBottom | Qt::AlignHCenter);
    ui->lblTimeRemain->setAlignment(Qt::AlignBottom | Qt::AlignRight);
    ui->lblTimeRemainMs->setAlignment(Qt::AlignBottom | Qt::AlignRight);
    if (QHBoxLayout* digitsLayout = qobject_cast<QHBoxLayout*>(ui->fraDigits->layout())) {
        digitsLayout->setContentsMargins(0, 1, 0, 1);
        digitsLayout->setSpacing(4);
        // Keep the summary in the existing middle slot. Set stretch on the
        // actual widgets instead of inserting index-based spacer items, whose
        // indices shift as the layout is modified.
        for (int i = 0; i < digitsLayout->count(); ++i) {
            QLayoutItem* layoutItem = digitsLayout->itemAt(i);
            if (layoutItem && layoutItem->widget() == ui->lblInfo)
                digitsLayout->setStretch(i, 1);
            else
                digitsLayout->setStretch(i, 0);
        }
    }

    // Allow the time strip to fill the player width so the remaining-time display
    // stays aligned with the right edge instead of leaving unused space.
    ui->fraDigits->setMinimumWidth(0);
    ui->fraDigits->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->fraDigits->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

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
        QFont f = label->font();
        f.setLetterSpacing(QFont::PercentageSpacing, 85);
        label->setFont(f);
        const QFontMetrics metrics(label->font());
        label->setFixedWidth(metrics.horizontalAdvance(sampleText));
        label->setMinimumHeight(metrics.height() + 2);
    };

    fixTimeLabelGeometry(ui->lblTime, "00:00");
    fixTimeLabelGeometry(ui->lblTimeMs, ".8 ");
    fixTimeLabelGeometry(ui->lblTimeRemain, "-00:00");
    fixTimeLabelGeometry(ui->lblTimeRemainMs, ".8");

    m_isStarted = false;
    m_pendingPlay = false;
    setAcceptDrops(true);
    updateResponsiveLayout();
    enforcePanelSplit();
    QTimer::singleShot(0, this, [this]() {
        updateResponsiveLayout();
        enforcePanelSplit();
        syncDisplayHeightToControls();
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
                  v == 0 ? QStringLiteral("0.0%")
                       : QString("%1%2%").arg(v > 0 ? "+" : "")
                                 .arg(v / 20.0, 0, 'f', 1));
        }
    }

    if (std::fabs(m_tempoRate - 1.0) < 0.001 && !m_syncAdopting)
        updateSyncButtonState(false);
}

void PlayerWidget::setSyncActive(bool active)
{
    setSyncAdopting(active);

    // Keep tempo reset behavior identical no matter whether sync is changed by
    // UI toggle or by controller logic. Latency compensation is configured by
    // Knowthelist from both output paths.
    if (!active)
        setTempoRate(1.0);
}

void PlayerWidget::setSyncAdopting(bool active)
{   
    if (m_syncAdopting == active)
        return;

    m_syncAdopting = active;
    bpmWidget->setTempoInfo(m_tempoRate, m_syncAdopting);
    updateSyncButtonState(active);
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

void PlayerWidget::resetSyncState()
{
    // Reset sync state: turn off sync adopting and set tempo rate to 1.0
    m_syncAdopting = false;
    setTempoRate(1.0);
    updateSyncButtonState(false);
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
                trackanalyzer->open(m_CurrentTrack->url());
            }
        }
    } else {
        if (timerVisual->isActive())
            timerVisual->stop();
        bpmWidget->hide();
        vuMeter->show();
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

int PlayerWidget::outputLatencyMs() const
{
    return player->outputLatencyMs();
}

void PlayerWidget::setInterPlayerDelayCompensation(int milliseconds)
{
    player->setDelayCompensation(milliseconds);
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
    setMinimumHeight(0);

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
    if (QVBoxLayout* controlsLayout = qobject_cast<QVBoxLayout*>(ui->frame_2->layout())) {
        const int controlsMargin = compact ? 1 : 2;
        controlsLayout->setContentsMargins(0, controlsMargin, 0, controlsMargin);
        controlsLayout->setSpacing(compact ? 2 : 4);
    }

    ui->fraVuMeter->setMinimumHeight(110);
    ui->fraDigits->setMinimumHeight(20);
    ui->fraDisplay->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->fraDisplay->setMinimumHeight(0);

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
    const int smallButtonWidth = veryCompact ? 18 : (compact ? 24 : 36);
    const int iconSize = veryCompact ? 18 : (compact ? 22 : 26);

    ui->butPlay->setMinimumSize(playButtonWidth, playButtonHeight);
    ui->butPlay->setMaximumHeight(playButtonHeight);
    ui->butCue->setMinimumSize(playButtonWidth, smallButtonHeight);
    ui->butCue->setMaximumHeight(playButtonHeight);
    ui->butPlay->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->butCue->setMaximumWidth(QWIDGETSIZE_MAX);
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

    syncDisplayHeightToControls();
}

void PlayerWidget::syncDisplayHeightToControls()
{
    if (!ui || !ui->frame || !ui->frame_3 || !ui->fraDisplay || !ui->sliPosition) {
        qDebug() << Q_FUNC_INFO << "return: missing UI object";
        return;
    }

    QGridLayout* displayGrid = qobject_cast<QGridLayout*>(ui->frame_3->layout());
    QLayout* outerLayout = ui->frame->layout();
    if (!displayGrid || !outerLayout || ui->frame->height() <= 0) {
        qDebug() << Q_FUNC_INFO << "return: invalid grid or frame height"
                 << "grid=" << displayGrid
                 << "outerLayout=" << outerLayout
                 << "frameHeight=" << ui->frame->height()
                 << "frame3Height=" << ui->frame_3->height();
        return;
    }

    const QMargins outerMargins = outerLayout->contentsMargins();
    ui->frame_3->setMinimumHeight(0);
    ui->frame_3->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->frame_3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    displayGrid->setRowStretch(0, 1);
    displayGrid->setRowStretch(1, 0);
    displayGrid->activate();

    const QMargins margins = displayGrid->contentsMargins();
    const int availableHeight = ui->frame_3->height() - margins.top() - margins.bottom();
    const int scrollbarHintHeight = ui->sliPosition->sizeHint().height();
    const int displayHeight = availableHeight - displayGrid->verticalSpacing() - scrollbarHintHeight;
    if (displayHeight <= 0) {
        qDebug() << Q_FUNC_INFO << "return: calculated height is non-positive";
        return;
    }

    ui->fraDisplay->setMinimumHeight(0);
    ui->fraDisplay->setMaximumHeight(QWIDGETSIZE_MAX);
    displayGrid->activate();
    qDebug() << Q_FUNC_INFO << "released fixed constraints"
             << "calculatedAvailableDisplayHeight=" << displayHeight
             << "newConstraints=" << ui->fraDisplay->minimumHeight() << ui->fraDisplay->maximumHeight()
             << "displayGeometry=" << ui->fraDisplay->geometry()
             << "gridItemGeometry=" << displayGrid->cellRect(0, 0);
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

    m_pitchResetButton = new QPushButton("0.0%", pitchFrame);
    m_pitchResetButton->setObjectName("butPitchReset");
    m_pitchResetButton->setToolTip(tr("Current tempo offset — click to reset to 0 %"));
    QFont smallFont = m_pitchResetButton->font();
    smallFont.setPointSizeF(smallFont.pointSizeF() * 0.82);
    m_pitchResetButton->setFont(smallFont);
    m_pitchResetButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    const QFontMetrics pitchResetMetrics(m_pitchResetButton->font());
    const int pitchResetWidth = pitchResetMetrics.horizontalAdvance(QStringLiteral("+12.0%"));
    m_pitchResetButton->setFixedWidth(pitchResetWidth);
    connect(m_pitchResetButton, &QPushButton::clicked, this, [this]() {
        if (m_pitchSlider)
            m_pitchSlider->setValue(0);
    });

    // Sync button — minimal "S" button right of pitch reset
    m_syncButton = new QPushButton("S", pitchFrame);
    m_syncButton->setObjectName("butSync");
    m_syncButton->setCheckable(true);
    m_syncButton->setChecked(false);
    m_syncButton->setToolTip(tr("Sync to master deck"));
    m_syncButton->setFont(smallFont);
    m_syncButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    const QFontMetrics syncMetrics(m_syncButton->font());
    const int syncWidth = syncMetrics.horizontalAdvance(QStringLiteral("S")) + 8;
    m_syncButton->setFixedWidth(syncWidth);
    connect(m_syncButton, &QPushButton::toggled, this, &PlayerWidget::on_butSync_toggled);

    pitchLayout->addWidget(m_pitchSlider, 1);
    pitchLayout->addWidget(m_pitchResetButton);
    pitchLayout->addWidget(m_syncButton);
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
    
    Q_EMIT syncRequested(false);
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
    if (!m_CurrentTrack)
        return QTime();

    // Manual CUE follows beat-cue setting: when enabled, jump to first beat occurrence.
    const auto mode = m_beatCueEnabled ? PlayerCueManager::CUE_BEAT_OCCURRENCE
                                       : PlayerCueManager::CUE_SKIP_SILENT;
    return m_cueManager->calculateCuePosition(mode);
}

// ---------------------------------------------------------------------------
// Unified cue-point computation – single source of truth for both modes
// ---------------------------------------------------------------------------

CuePoints PlayerWidget::computeCuePoints(CueMode mode) const
{
    // Delegate to shared CueManager implementation.
    const auto cue = m_cueManager->computeCuePoints(static_cast<PlayerCueManager::CueMode>(mode));
    CuePoints result{cue.valid, cue.start, cue.end};
    return result;
}

// ---------------------------------------------------------------------------
//  Internal helpers that centralise the "fade point" logic and were previously
//  copy-pasted across computeCuePoints(), seekOvershootsFadePoint(),
//  setPositionMarkers() and analyzeEnvelopeFinished().
// ---------------------------------------------------------------------------

/**
 * Compute the earliest valid cue-end (fade) point.
 *
 * The fade point is the *minimum* of:
 *   - the last beat-activity position (if valid / non-zero)
 *   - the track end position  (if valid / non-zero)
 *
 * Falls back to raw trackanalyzer->endPosition() when neither source is valid.
 */
QTime PlayerWidget::computeFadePoint() const {
    return m_cueManager->computeFadePoint(
        static_cast<PlayerCueManager::CueMode>(m_transitionCueMode));
}

QTime PlayerWidget::computeFadePoint(CueMode mode) const {
    return m_cueManager->computeFadePoint(static_cast<PlayerCueManager::CueMode>(mode));
}

void PlayerWidget::setTransitionCueMode(CueMode mode) {
    m_transitionCueMode = mode;
    const CuePoints cue = computeCuePoints(mode);
    if (cue.valid) {
        m_transitionCuePlanned = true;
        m_cuePosition = cue.start;
        bpmWidget->setCuePosition(cue.start);
    }
}

/**
 * Compute the remaining cue time in milliseconds between a fade point and the
 * end of the track.
 */
long PlayerWidget::computeRemainCueTime(const QTime& fadePoint) const {
    return m_cueManager->computeRemainCueTime(fadePoint);
}

int PlayerWidget::finishTriggerLeadTimeMs() const
{
    // A hard cut must be scheduled at the end of the audible material, not
    // with the normal blend lead time configured for longer fades.
    return m_transitionCueMode == CUE_HARD_CUT ? 500 : mTrackFinishEmitTime;
}

// Apply a CuePoints struct to the player: set position, remainCueTime, UI, bpmWidget.
void PlayerWidget::applyCuePoints(const CuePoints& cue, bool isManual)
{
    if (!cue.valid)
        return;

    if (cue.start.isValid() && cue.start > QTime(0, 0))
        player->setPosition(cue.start);

    // Compute remainCueTime from the end cue point to track length.
    const QTime length = trackanalyzer ? trackanalyzer->length() : player->length();
    if (cue.end.isValid() && cue.end > QTime(0, 0) && length > QTime(0, 0)) {
        remainCueTime = qMax(0, cue.end.msecsTo(length));
        ui->txtCue->setText("-" + QString::number(remainCueTime / 1000));
    }

    bpmWidget->setTrackLength(player->length());
    m_cuePosition = cue.start;
    bpmWidget->setCuePosition(cue.start);
    bpmWidget->setState(m_bpm, cue.start, m_beatPosition, m_isStarted, m_bpmAnalyzed);

    // A valid cue applied to a waiting deck is the active cue, regardless of
    // whether it came from manual input or transition planning.
    if (isManual || !m_isStarted)
        ui->butCue->setChecked(true);

    updateTimeAndPositionDisplay(false);
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

    // Use the unified computeFadePoint() from CueManager — matches setPositionMarkers() path.
    const int targetMs = qMax(0, QTime(0, 0).msecsTo(targetPos));

    if (trackanalyzer->finished() && (m_skipSilentEnd || m_beatCueEnabled || m_transitionCuePlanned)) {
        const QTime fadePoint = computeFadePoint();
        if (fadePoint.isValid() && fadePoint > QTime(0, 0)) {
            const int triggerPosMs = qMax(0, QTime(0, 0).msecsTo(fadePoint)
                                                   - finishTriggerLeadTimeMs());
            return targetMs >= triggerPosMs;
        }
    }

    const QTime length = player->length();
    const int lengthMs = QTime(0, 0).msecsTo(length);
    if (lengthMs <= 0)
        return false;

    const int triggerPosMs = qMax(0, lengthMs - static_cast<int>(remainCueTime)
                                      - finishTriggerLeadTimeMs());
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
    const int totalSeconds = qMax(0, info.second);
    QString duration;
    if (totalSeconds < 60 * 60) {
        duration = QStringLiteral("%1m").arg(Track::prettyTime(totalSeconds, false));
    } else if (totalSeconds < 24 * 60 * 60) {
        duration = QStringLiteral("%1h").arg(Track::prettyTime(totalSeconds, true));
    } else {
        duration = QStringLiteral("%1d").arg(totalSeconds / (24.0 * 60.0 * 60.0), 0, 'f', 1);
    }

    m_infoBaseText = QString("%1 %2 | %3")
                         .arg(info.first)
                         .arg(strTrack)
                         .arg(duration);
    setElidedLabelText(ui->lblInfo, m_infoBaseText);
}

void PlayerWidget::setEqualizer(EqBand band, int value)
{
    //ranging from -24.0 to +12.0.
    player->setEqualizer("band" + QString::number(band), (value - 240) / 10.0);
}

void PlayerWidget::setPositionMarkers()
{
    if (trackanalyzer->finished()) {
        if (m_skipSilentEnd || m_beatCueEnabled) {
            const QTime fadePoint = computeFadePoint();
            remainCueTime = m_cueManager->computeRemainCueTime(fadePoint);
            if (remainCueTime > 0) {
                qDebug() << Q_FUNC_INFO << "fadeStartPoint:" << fadePoint;
                qDebug() << Q_FUNC_INFO << "length:" << trackanalyzer->length();
            }
        } else {
            remainCueTime = 0;
        }

        ui->txtCue->setText("-" + QString::number(remainCueTime / 1000));
    }

    m_cueManager->applyAutoCueAfterAnalysis(m_beatCueEnabled);
}

void PlayerWidget::applyAutoCueAfterAnalysis(bool preferBeatCue)
{
    if (!m_CurrentTrack || !trackanalyzer)
        return;

    // Never reposition an actively playing track from background analysis.
    if (m_isStarted)
        return;

    // Once the transition planner has a valid result, it owns the cue for
    // this track. Do not let generic analyzer auto-cueing replace it.
    if (m_transitionCuePlanned) {
        const CuePoints plannedCue = computeCuePoints(m_transitionCueMode);
        if (plannedCue.valid) {
            applyCuePoints(plannedCue, false);
            return;
        }
    }

    // Delegate to CueManager but preserve PlayerWidget-specific UI state (bpmWidget/ui updates).
    m_cueManager->applyAutoCueAfterAnalysis(preferBeatCue);

    // Sync UI using the position that was actually applied by CueManager.
    const QTime cuePosition = player->position();

    qDebug() << Q_FUNC_INFO << ":" << objectName()
             << " preferBeatCue=" << preferBeatCue
             << " beatPhaseValid=" << m_beatPosition.isValid()
             << " beatPhase=" << m_beatPosition
             << " analyzerFinished=" << trackanalyzer->finished()
             << " appliedCuePosition=" << cuePosition;

    bpmWidget->setTrackLength(player->length());
    m_cuePosition = cuePosition;
    bpmWidget->setCuePosition(cuePosition);
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

    // Transport changes (pause/play) can shift phase perception between decks.
    // Let the central sync logic decide which deck should be snapped.
    Q_EMIT syncRequested(false);
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

        // For STANDARD analysis (TEMPO not yet available), apply skip-silent
        // start only when beat cueing is disabled. Otherwise keep the beat-based
        // auto-cue position from setPositionMarkers().
        bool autoCueApplied = false;
        if (m_skipSilentBegin && !m_beatCueEnabled) {
            const QTime startPos = trackanalyzer->startPosition();
            if (startPos.isValid() && startPos > QTime(0, 0)) {
                if (!m_isStarted || player->isLoaded()) {
                    player->setPosition(startPos);
                    m_cueManager->applyCuePoints({true, startPos, trackanalyzer->endPosition()}, false);
                    autoCueApplied = true;
                }
            }
        }

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

        if ((autoCueApplied || ui->butCue->isChecked()) && !m_isStarted) {
            // CUE button should show checked when auto-cue was applied.
            // Only checked here if not yet playing (don't flicker during playback).
            ui->butCue->setChecked(true);
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
    m_barAnchorPosition = trackanalyzer->barAnchorPosition();
    m_barPhaseConfidence = trackanalyzer->barPhaseConfidence();

    if (m_bpm > 0) {
        qDebug() << "Tempo analysis:"
                 << objectName()
                 << "bpm=" << m_bpm
                 << "exactBpm=" << trackanalyzer->exactBpm()
                 << "beatPhase=" << m_beatPosition
                 << "barAnchor=" << m_barAnchorPosition
                 << "barConfidence=" << m_barPhaseConfidence;
        emit tempoChanged(m_bpm, m_beatPosition);
    }
    applyAutoCueAfterAnalysis(m_beatCueEnabled);
    bpmWidget->setTrackLength(player->length());
    bpmWidget->setState(m_bpm, trackanalyzer->exactBpm(), player->position(), m_beatPosition, m_isStarted, true);

    // TEMPO analysis populates the envelope internally (needed for beat detection).
    // Process it here so the waveform appears on first load before caching.
    if (m_beatVisualMode && !bpmWidget->isEnvelopePreloaded())
        analyzeEnvelopeFinished();
}

void PlayerWidget::analyzeEnvelopeFinished()
{
    if (!m_CurrentTrack)
        return;

    const QVector<float> env = trackanalyzer->amplitudeEnvelope();
    if (env.isEmpty())
        return;

    qDebug() << "ENVELOPE analysis finished:" << objectName() << "samples=" << env.size();
    bpmWidget->setTrackLength(trackanalyzer->length());
    const int envelopeDurationMs = QTime(0, 0).msecsTo(trackanalyzer->length());
    bpmWidget->setPreloadedEnvelope(env, kEnvelopeAnalysisIntervalMs, envelopeDurationMs);

    // Use beat-stop (beatActivityEnd) from the same analysis pass already done in TrackAnalyzer.
    // Skip CueManager::computeFadePoint() here — it would redo identical work that's already in
    // m_beatStopPosition / endPosition. The fade point stays up to date via setPositionMarkers().
    if (m_beatSyncEnabled && (m_skipSilentEnd || m_beatCueEnabled) && trackanalyzer->finished()) {
        const QTime beatStop = trackanalyzer->beatEndPosition();
        const int lastBeatMs = QTime(0, 0).msecsTo(beatStop);
        if (lastBeatMs > 3 * 1000) {                         // at least 3s of content after first beat
            const QTime trackLen = trackanalyzer->length();
            const int newRemainMs = qMax(0, QTime(0, 0).msecsTo(trackLen) - lastBeatMs);
            if (newRemainMs > static_cast<int>(remainCueTime)) {
                remainCueTime = newRemainMs;
                ui->txtCue->setText("-" + QString::number(remainCueTime / 1000));
                qDebug() << Q_FUNC_INFO << "beat-stop override:" << beatStop
                         << "newRemainCue=" << remainCueTime;
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

    // The position timer owns playback-state transitions. A visual refresh can
    // observe a stopped backend briefly while the audio device is changing state.
    if (!player->isPlaying()) {
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
    m_cuePosition = QTime();
    m_transitionCuePlanned = false;
    m_tempoRate = 1.0;
    m_syncAdopting = false;
    m_visualLatencyMs = 0;
    m_pendingEnvelope.clear();
    m_liveEnvelopeStarted = false;
    m_liveEnvelopeSmoothed = 0.0f;
    m_beatPosition = QTime();
    m_barAnchorPosition = QTime();
    m_barPhaseConfidence = 0.0;
    bpmWidget->setState(m_bpm, QTime(0, 0), m_beatPosition, false, track == nullptr);
    bpmWidget->setCuePosition(QTime());
    bpmWidget->setTempoInfo(m_tempoRate, m_syncAdopting);
    bpmWidget->setTrackLength(QTime(0, 0));
    bpmWidget->clearEnvelope();
    m_envelopeScrubbing = false;

    if (track == nullptr) {
        ui->lblTitle->setText("no track");
        ui->lblTime->setText("-:-");
        ui->lblTimeMs->setText(".-");
        ui->lblTimeRemain->setText("-:-");
        ui->lblTimeRemainMs->setText(".-");
        stop();
        remainCueTime = 0;
        ui->sliPosition->setValue(0);
        ui->txtCue->setText("-");
        ui->butCue->setChecked(false);
        return;
    }

    drawTitle();

    bool doPlay = m_isStarted;
    player->stop();

    // Playlist items already carry the integer BPM from analysis_cache. Reuse it
    // immediately for display, but let TrackAnalyzer still calculate the beat
    // phase so the waveform grid is anchored to the analyzed audio.
    m_bpm = track->bpm();
    m_bpmAnalyzed = m_bpm > 0;
    bpmWidget->setState(m_bpm, player->position(), m_beatPosition, false, m_bpmAnalyzed);

    QUrl url = track->url();
    player->open(url);

    // Start TrackAnalyzer when any analysis-dependent feature is needed.
    // All cache reads/writes are centralized in Player::analyze().
    QSettings settings;
    const bool analyzeTempo = settings.value("beatSyncAnalyzeTempo", true).toBool();
    if ((m_beatSyncEnabled && analyzeTempo) || m_skipSilentBegin || m_skipSilentEnd ||
        (m_beatVisualMode && !bpmWidget->isEnvelopePreloaded())) {
        qDebug() << Q_FUNC_INFO << "Starting TrackAnalyzer for:" << url;
        trackanalyzer->open(url);
    }

    m_pendingPlay = doPlay;

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
    syncDisplayHeightToControls();
    drawTitle();
    // setInfo() can run before the time strip has its final width. Rebuild the
    // displayed text now so an earlier narrow elision does not remain stale.
    updateTimeAndPositionDisplay(false);
    QTimer::singleShot(0, this, [this]() {
        updateTimeAndPositionDisplay(false);
    });
    // bpmWidget geometry is updated via eventFilter on fraVuMeter
}

bool PlayerWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == ui->fraVuMeter && event->type() == QEvent::Resize) {
        // fraVuMeter has its final geometry now — fill it exactly
        bpmWidget->setGeometry(ui->fraVuMeter->rect());
        return false;
    }
    return QWidget::eventFilter(obj, event);
}

void PlayerWidget::drawTitle()
{
    int width = ui->lblTitle->width() - 2;
    if (width < 300)
        ui->lblTitle->setStyleSheet("* { font-size: 13pt; }");
    else if (width < 400)
        ui->lblTitle->setStyleSheet("* { font-size: 14pt; }");
    else
        ui->lblTitle->setStyleSheet("* { font-size: 21pt; }");

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
    ui->lblTimeMs->setText("." + QString::number(QTime(0, 0).msecsTo(curpos) % 10));
    ui->lblTimeRemain->setText("-" + remain.toString("mm:ss"));
    ui->lblTimeRemainMs->setText("." + QString::number((remainMs < 0 ? -remainMs : remainMs) % 10));

    bool nearEndByTime = false;
    if (trackanalyzer->finished() && (m_skipSilentEnd || m_beatCueEnabled || m_transitionCuePlanned)) {
        const QTime fadePoint = computeFadePoint();
        if (fadePoint.isValid() && fadePoint > QTime(0, 0)) {
            const int triggerPosMs = qMax(0, QTime(0, 0).msecsTo(fadePoint)
                                                   - finishTriggerLeadTimeMs());
            const int curMs = QTime(0, 0).msecsTo(curpos);
            nearEndByTime = (curMs >= triggerPosMs) && (remainMs > 0);
        }
    }
    if (!nearEndByTime)
        nearEndByTime = (remainMs <= finishTriggerLeadTimeMs() && 0 < remainMs);
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

    setInfoLabelText(ui->lblInfo, m_infoBaseText, highlight);

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
    
    Q_EMIT syncRequested(false);
}

void PlayerWidget::on_butFwd_clicked()
{
    Q_EMIT forwardPressed();
    
    Q_EMIT syncRequested(false);
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
    
    Q_EMIT syncRequested(false);
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
    if (!ui->butCue->isChecked()) { // Ignore unchecked toggle signal; act only when toggling ON
        return;
    }

    // Recall the last cue defined for this track. Only use analysis as a
    // fallback when no cue has been established yet.
    QTime cuePosition = m_cuePosition;
    if (!cuePosition.isValid() || cuePosition <= QTime(0, 0))
        cuePosition = calculateCuePosition();

    if (!cuePosition.isValid() || cuePosition <= QTime(0, 0)) {
        qWarning() << Q_FUNC_INFO << "no valid cue position available";
        ui->butCue->setChecked(false);
        return;
    }

    qDebug() << Q_FUNC_INFO << "manual CUE applied:" << cuePosition
              << "m_beatCueEnabled:" << m_beatCueEnabled
              << " beatPosition:" << m_beatPosition
              << " bpm:" << m_bpm;

    //ToDo: Visualize skipped silent at start and at the end (color bar)
    this->pause();
    suppressAboutFinishForMs(1000);

    player->setPosition(cuePosition);
    m_cuePosition = cuePosition;

    // Apply remainCueTime from the fade point so the text label updates.
    const QTime fadePoint = computeFadePoint();
    if (fadePoint.isValid() && fadePoint > QTime(0, 0)) {
        remainCueTime = computeRemainCueTime(fadePoint);
        ui->txtCue->setText("-" + QString::number(remainCueTime / 1000));
    } else {
        remainCueTime = 0;
    }

    bpmWidget->setTrackLength(player->length());
    bpmWidget->setCuePosition(cuePosition);
    bpmWidget->setState(m_bpm, cuePosition, m_beatPosition, m_isStarted, m_bpmAnalyzed);
    updateTimeAndPositionDisplay();
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

double PlayerWidget::lowEndConfidence() const
{
    return trackanalyzer ? trackanalyzer->lowEndConfidence() : 0.0;
}

void PlayerWidget::alignCueToReferenceBeat(double referenceBpm, const QTime& referencePosition,
                                            const QTime& referenceBeatAnchor, bool alignToBar,
                                            const QTime& referenceBeatPosition)
{
    const double ownBpm = normalizeSyncBpm(exactBpmForSync());
    const double refBpm = normalizeSyncBpm(referenceBpm);
    if (m_isStarted || ownBpm <= 0.0 || refBpm <= 0.0)
        return;

    const double ownBeatMs = 60000.0 / ownBpm;
    const double refBeatMs = 60000.0 / refBpm;

    const bool useBarAlignment = alignToBar
        && m_barPhaseConfidence >= kMinimumBarPhaseConfidence
        && referenceBeatAnchor.isValid();
    const int gridBeats = useBarAlignment ? 4 : 1;
    const double ownGridMs = gridBeats * ownBeatMs;
    const double refGridMs = gridBeats * refBeatMs;

    const qint64 refMs = QTime(0, 0).msecsTo(referencePosition);
    const QTime phaseAnchor = useBarAlignment
        ? referenceBeatAnchor
        : referenceBeatPosition;
    const qint64 refAnchorMs = phaseAnchor.isValid()
                                   ? QTime(0, 0).msecsTo(phaseAnchor) : 0LL;

    double refGridPhase = fmod(static_cast<double>(refMs - refAnchorMs), refGridMs);
    if (refGridPhase < 0.0) refGridPhase += refGridMs;

    // Scale to this track's beat period
    const double targetGridPhase = refGridPhase * (ownBeatMs / refBeatMs);

    // Move from the current cue/playhead to the *next* matching bar phase.
    // This keeps pre-roll alignment stable and avoids large absolute jumps.
    const QTime ownAnchor = useBarAlignment && m_barAnchorPosition.isValid()
        ? m_barAnchorPosition
        : m_beatPosition;
    const qint64 anchorMs = ownAnchor.isValid()
        ? QTime(0, 0).msecsTo(ownAnchor) : 0LL;
    const qint64 currentMs = QTime(0, 0).msecsTo(player->position());

    double currentGridPhase = fmod(static_cast<double>(currentMs - anchorMs), ownGridMs);
    if (currentGridPhase < 0.0) currentGridPhase += ownGridMs;

    double delta = targetGridPhase - currentGridPhase;
    if (delta < 0.0) delta += ownGridMs;

    qint64 newMs = currentMs + static_cast<qint64>(delta + 0.5);
    const qint64 lengthMs = QTime(0, 0).msecsTo(player->length());
    if (lengthMs > 0)
        newMs = qBound<qint64>(0LL, newMs, lengthMs);

    player->setPosition(QTime(0, 0).addMSecs(static_cast<int>(newMs)));
    bpmWidget->setState(m_bpm, player->position(), m_beatPosition, m_isStarted, m_bpmAnalyzed);
    updateTimeAndPositionDisplay(false);
}

void PlayerWidget::syncNowToReferenceBeat(double referenceBpm, const QTime& referencePosition,
                                          const QTime& referenceBeatAnchor, bool alignToBar,
                                          bool matchTempo,
                                          const QTime& referenceBeatPosition)
{
    // Works while playing: seeks to the nearest bar-aligned position that puts
    // this deck on the same beat-of-the-bar as the reference deck, so both
    // decks share the same beat-1 in a standard 4/4 bar.
    const double ownBpm = normalizeSyncBpm(exactBpmForSync());
    const double refBpm = normalizeSyncBpm(referenceBpm);
    if (ownBpm <= 0.0 || refBpm <= 0.0)
        return;

    const double ownBeatMs   = 60000.0 / ownBpm;
    const double refBeatMs   = 60000.0 / refBpm;

    const bool useBarAlignment = alignToBar
        && m_barPhaseConfidence >= kMinimumBarPhaseConfidence
        && referenceBeatAnchor.isValid();
    const int gridBeats = useBarAlignment ? 4 : 1;
    const double ownGridMs = gridBeats * ownBeatMs;
    const double refGridMs = gridBeats * refBeatMs;

    const qint64 refMs       = QTime(0, 0).msecsTo(referencePosition);
    const QTime phaseAnchor = useBarAlignment
        ? referenceBeatAnchor
        : referenceBeatPosition;
    const qint64 refAnchorMs = phaseAnchor.isValid()
                                   ? QTime(0, 0).msecsTo(phaseAnchor) : 0LL;

    // Phase of reference within the selected beat/bar grid.
    double refGridPhase = fmod(static_cast<double>(refMs - refAnchorMs), refGridMs);
    if (refGridPhase < 0.0) refGridPhase += refGridMs;

    // Scale bar phase to this track's beat period (handles tempo differences)
    const double targetGridPhase = refGridPhase * (ownBeatMs / refBeatMs);

    // This track's beat anchor
    const QTime baseCue = useBarAlignment && m_barAnchorPosition.isValid()
        ? m_barAnchorPosition
        : (m_beatPosition.isValid() ? m_beatPosition
                                    : (trackanalyzer ? trackanalyzer->startPosition() : QTime()));
    const qint64 anchorMs = QTime(0, 0).msecsTo(baseCue);
    const qint64 currentMs = QTime(0, 0).msecsTo(player->position());

    // Phase of current position in the selected beat/bar grid.
    double currentGridPhase = fmod(static_cast<double>(currentMs - anchorMs), ownGridMs);
    if (currentGridPhase < 0.0) currentGridPhase += ownGridMs;

    // Smallest delta within half the selected grid to land on target phase.
    double delta = targetGridPhase - currentGridPhase;
    if (delta >  ownGridMs / 2.0) delta -= ownGridMs;
    if (delta < -ownGridMs / 2.0) delta += ownGridMs;

    const qint64 newMs = qMax(anchorMs, currentMs + static_cast<qint64>(delta + 0.5));
    const qint64 maxLocalCorrectionMs = qRound(ownGridMs / 2.0);
    if (qAbs(newMs - currentMs) > maxLocalCorrectionMs) {
        qDebug() << "Sync skipped: correction exceeds limit"
                 << "object=" << objectName()
                 << "currentMs=" << currentMs
                 << "targetMs=" << newMs
                 << "deltaMs=" << delta
                 << "limitMs=" << maxLocalCorrectionMs
                 << "gridBeats=" << gridBeats
                 << "ownBarConfidence=" << m_barPhaseConfidence
                 << "referenceAnchor=" << referenceBeatAnchor;
        return;
    }

    const qint64 lengthMs = QTime(0, 0).msecsTo(player->length());
    if (lengthMs > 0 && (newMs < 0 || newMs >= lengthMs)) {
        qDebug() << "Sync skipped: target outside track"
                 << "object=" << objectName()
                 << "currentMs=" << currentMs
                 << "targetMs=" << newMs
                 << "lengthMs=" << lengthMs;
        return;
    }

    qDebug() << "Applying calculated sync:"
             << "object=" << objectName()
             << "currentMs=" << currentMs
             << "targetMs=" << newMs
             << "deltaMs=" << delta
             << "gridBeats=" << gridBeats
             << "ownBarConfidence=" << m_barPhaseConfidence
             << "referenceAnchor=" << referenceBeatAnchor;
    player->setPosition(QTime(0, 0).addMSecs(static_cast<int>(newMs)));
    bpmWidget->setState(m_bpm, player->position(), m_beatPosition, m_isStarted, m_bpmAnalyzed);
    updateTimeAndPositionDisplay(false);

    // Optional: also match tempo rate when requested by caller.
    if (matchTempo && qAbs(referenceBpm - ownBpm) > 1.0e-6)
        setTempoRate(referenceBpm / ownBpm);
}

bool PlayerWidget::correctPhaseToReferenceBeat(double referenceBpm, const QTime& referencePosition,
                                               const QTime& referenceBeatAnchor,
                                               int toleranceMs, int maxCorrectionMs)
{
    const double ownBpm = normalizeSyncBpm(exactBpmForSync());
    const double refBpm = normalizeSyncBpm(referenceBpm);
    if (!m_isStarted || !referencePosition.isValid() || ownBpm <= 0.0 || refBpm <= 0.0)
        return false;

    const double ownBeatMs = 60000.0 / ownBpm;
    const double refBeatMs = 60000.0 / refBpm;
    const qint64 currentMs = QTime(0, 0).msecsTo(player->position());
    const QTime ownBeatAnchor = m_beatPosition.isValid()
        ? m_beatPosition : (trackanalyzer ? trackanalyzer->startPosition() : QTime());
    const qint64 ownAnchorMs = ownBeatAnchor.isValid()
        ? QTime(0, 0).msecsTo(ownBeatAnchor) : 0;
    const qint64 refMs = QTime(0, 0).msecsTo(referencePosition);
    const qint64 refAnchorMs = referenceBeatAnchor.isValid()
        ? QTime(0, 0).msecsTo(referenceBeatAnchor) : 0;

    double refPhase = std::fmod(static_cast<double>(refMs - refAnchorMs), refBeatMs);
    if (refPhase < 0.0)
        refPhase += refBeatMs;
    const double targetPhase = refPhase * ownBeatMs / refBeatMs;

    double ownPhase = std::fmod(static_cast<double>(currentMs - ownAnchorMs), ownBeatMs);
    if (ownPhase < 0.0)
        ownPhase += ownBeatMs;
    double correctionMs = targetPhase - ownPhase;
    if (correctionMs > ownBeatMs * 0.5)
        correctionMs -= ownBeatMs;
    else if (correctionMs < -ownBeatMs * 0.5)
        correctionMs += ownBeatMs;

    if (qAbs(correctionMs) <= qMax(0, toleranceMs)
        || qAbs(correctionMs) > qMax(0, maxCorrectionMs))
        return false;

    const qint64 lengthMs = QTime(0, 0).msecsTo(player->length());
    const qint64 correctedMs = currentMs + qRound64(correctionMs);
    if (correctedMs < 0 || (lengthMs > 0 && correctedMs >= lengthMs))
        return false;

    player->setPosition(QTime(0, 0).addMSecs(static_cast<int>(correctedMs)));
    bpmWidget->setState(m_bpm, player->position(), m_beatPosition, m_isStarted, m_bpmAnalyzed);
    updateTimeAndPositionDisplay(false);
    return true;
}

void PlayerWidget::on_butSync_toggled(bool checked)
{
    updateSyncButtonState(checked);
    if (checked) {
        m_syncAdopting = true;
        Q_EMIT syncRequested(true);
    } else {
        // When sync is turned off, clear all sync state to stop blinking
        m_syncAdopting = false;
        setTempoRate(1.0);
    }
    
    // Emit custom signal for sync button changes
    Q_EMIT syncButtonToggled(checked);
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
                                        .arg(value / 20.0, 0, 'f', 1));
    }

    if (std::fabs(m_tempoRate - 1.0) < 0.001 && !m_syncAdopting)
        updateSyncButtonState(false);
}
