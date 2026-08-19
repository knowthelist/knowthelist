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

#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QLabel>
#include <QAbstractButton>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QVector>
#include <QDropEvent>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <memory>

#include "player.h"
#include "playerbpmwidget.h"
#include "vumeter.h"

#include "playlistitem.h"
#include "trackanalyzer.h"

class PlayerCueManager;

// ---- Cue mode: two distinct strategies for finding start & end cue points ----
enum CueMode {
    CUE_SKIP_SILENT,        // skipSilent:   start at first non-silent sample / end at last non-silent beat-activity
    CUE_BEAT_OCCURRENCE,    // beatOccurrence: start at first significant beat / end where enough fade-room remains for beat-sync mix-out
    CUE_SKIP_SILENT_OCCURRENCE, // skip-silent start with beat-aware mix-out
    CUE_BEAT_START_SILENT_END,  // beat-based start with the silent/track end as mix-out point
    CUE_HARD_CUT                 // beat-based start with a late silent/track-end trigger
};

// Pair of cue points (start of track + mix-out point near the end)
struct CuePoints {
    bool valid{false};
    QTime start;               // cue point at song start (intro skipped)
    QTime end;                 // mix-out / fade-in start point near song end
};

namespace Ui {
class PlayerWidget;
}

class PlayerWidget : public QWidget {
    Q_OBJECT

public slots:
    void on_butSync_toggled(bool checked);

signals:
    void syncButtonToggled(bool checked);
public:
    explicit PlayerWidget(QWidget* parent = 0);
    ~PlayerWidget();

    enum EqBand { EQ_Low = 0,
        EQ_Mid = 1,
        EQ_High = 2
    };

    float currentLevelLeft();
    float currentLevelRight();
    void loadFile(QUrl);


    void play();
    void stop();
    void pause();
    bool isStarted() { return m_isStarted; }

    void setTrackFinishEmitTime(const int sec);
    int TrackFinishEmitTime() const { return mTrackFinishEmitTime; }
    void setVolume(double volume);
    void setGain(double gain);
    int currentBpm() const { return m_bpm; }
    double exactBpmForSync() const;
    double lowEndConfidence() const;
    QTime currentPosition() const;
    QTime beatPosition() const { return m_beatPosition; }
    QTime barAnchorPosition() const { return m_barAnchorPosition; }
    double barPhaseConfidence() const { return m_barPhaseConfidence; }
    void setTempoRate(double rate);
    double tempoRate() const { return m_tempoRate; }
    void setSyncActive(bool active);
    void setSyncAdopting(bool active);
    bool isSyncAdopting() const { return m_syncAdopting; }
    bool supportsSmoothTempo() const;
    // referenceBeatAnchor is the reference deck's 4/4 bar anchor when
    // alignToBar is true; callers may pass the beat anchor for beat-only sync.
    void syncNowToReferenceBeat(double referenceBpm, const QTime& referencePosition,
                                const QTime& referenceBeatAnchor = QTime(),
                                bool alignToBar = true,
                                bool matchTempo = false,
                                const QTime& referenceBeatPosition = QTime());
    bool correctPhaseToReferenceBeat(double referenceBpm, const QTime& referencePosition,
                                     const QTime& referenceBeatAnchor,
                                     int toleranceMs, int maxCorrectionMs);
    void alignCueToReferenceBeat(double referenceBpm, const QTime& referencePosition,
                                   const QTime& referenceBeatAnchor = QTime(), bool alignToBar = true,
                                   const QTime& referenceBeatPosition = QTime());
    QPushButton* getSyncButton() { return m_syncButton; }
    void setBeatSyncEnabled(bool enabled) { m_beatSyncEnabled = enabled; }
    void setBeatCueEnabled(bool enabled) { m_beatCueEnabled = enabled; }
    void setBeatVisualMode(bool enabled);
    QTime getTrackLength() const { return player->length(); }
    void setCurrentPosition(QTime pos) { return player->setPosition(pos); }
    void setMonitorOutputDeviceId(const QString& deviceId);
    void setMonitorRouteAvailable(bool available);
    void setMonitorRouteEnabled(bool enabled);
    bool isMonitorRouteEnabled() const { return m_monitorRouteEnabled; }
    void setMonitorVolume(double v);
    int outputLatencyMs() const;
    void setInterPlayerDelayCompensation(int milliseconds);
    void setSkipSilentEnd(bool checked)
    {
        m_skipSilentEnd = checked;
        setPositionMarkers();
    }
    void setSkipSilentBegin(bool checked)
    {
        m_skipSilentBegin = checked;
        setPositionMarkers();
    }


public Q_SLOTS:
    void setPositionMarkers();
    void loadTrack(Track* track);
    void analyzeGainFinished();
    void analyzeTempoFinished();
    void analyzeEnvelopeFinished();
    void onTrackPropertyChanged(Track* track);
    void setEqualizer(EqBand, int);
    void setInfo(QPair<int, int> info);

Q_SIGNALS:
    void trackFinished();
    void aboutFinished();
    void trackDropped(Track*);
    void trackPlayed(Track*);
    void forwardPressed();
    void rewindPressed();
    void statusChanged(bool);
    void gainChanged(double);
    void tempoChanged(int bpm, QTime beatPosition);
    void levelChanged(double, double);
    void syncRequested(bool adoptTempo);
    void syncStateChanged(bool active);
    void monitorRouteToggled(bool enabled);

private Q_SLOTS:
    void on_butCue_clicked();
    void on_sliPosition_actionTriggered(int action);
    void updateTimeAndPositionDisplay(bool isPassive = true);
    void playerFinished();
    void playerError();
    void playerLoaded();
    void timerLevel_timeOut();
    void timerPosition_timeOut();
    void timerVisual_timeOut();
    void on_sliPosition_sliderMoved(int);
    void onBeatJumpButtonClicked();
    void onEnvelopeScrubStarted();
    void onEnvelopeScrubPositionChanged(double normalizedPosition, bool finished);
    void applyPendingEnvelopeScrubSeek();

    void on_butPlay_clicked();
    void on_butRew_clicked();
    void on_butFwd_clicked();
    void on_monitorRoute_toggled(bool checked);
    void on_pitchSlider_valueChanged(int value);

protected:
    VUMeter* vuMeter;
    PlayerBpmWidget* bpmWidget;

    long songTime;

public:
    // ToDo: move privates to struct Private
    Ui::PlayerWidget* ui;
    QToolButton* initButton(QStyle::StandardPixmap icon, const QString& tip,
        QObject* dstobj, const char* slot_method, QLayout* layout);

    void createUI(QBoxLayout* appLayout);
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void drawTitle();
    void applyBeatVisualLayout(bool enabled);
    void updateSyncButtonState(bool active);
    void resetSyncState();
    void updateResponsiveLayout();
    void enforcePanelSplit();
    void syncDisplayHeightToControls();
    void createPerformanceControls();
    void jumpByBeats(int beatCount);
    void applyBeatJump(int beatCount, int basePositionMs);
    QString currentTrackKey() const;

    // ---- Unified cue-point helpers (delegated to PlayerCueManager) ----
    CuePoints computeCuePoints(CueMode mode) const;  // Now in playercuemanager.cpp
    void applyAutoCueAfterAnalysis(bool preferBeatCue);
    void applyCuePoints(const CuePoints& cue, bool isManual);
    void suppressAboutFinishForMs(int ms);
    bool seekOvershootsFadePoint(const QTime& targetPos) const;
    void armImmediateAboutFinish();
    QTime calculateCuePosition() const;  // Thin dispatcher -> m_cueManager

    // Allow cue manager to access protected members (trackanalyzer, player, m_bpm, etc.)
    friend class PlayerCueManager;

    // ---- Internal fade-point helpers (replaces duplicated logic) ----
    QTime computeFadePoint() const;
    QTime computeFadePoint(CueMode mode) const;
    long computeRemainCueTime(const QTime& fadePoint) const;
    int finishTriggerLeadTimeMs() const;
    void setTransitionCueMode(CueMode mode);

    // Sub-objects can access these shared members via PlayerWidget& or through their own methods.
    // Note: These are accessed by sub-classes during construction before the full class is defined.

protected:
    Player* player;
    TrackAnalyzer* trackanalyzer;
    float m_level;
    QLabel* m_positionLabel;
    QLabel* m_volumeLabel;

    QTimer* timerLevel;
    QTimer* timerPosition;
    QTimer* timerVisual;

    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
    Track* m_CurrentTrack;
    long remainCueTime;

    bool m_isStarted;
    bool m_isHanging;
    bool m_pendingPlay;
    bool m_skipSilentEnd;
    bool m_skipSilentBegin;
    bool m_beatSyncEnabled;
    bool m_beatCueEnabled;
    CueMode m_transitionCueMode{CUE_SKIP_SILENT_OCCURRENCE};
    bool m_transitionCuePlanned{false};
    bool m_beatVisualMode;
    double m_tempoRate;
    bool m_syncAdopting;
    int m_visualLatencyMs;
    int m_lastWaveformRebuildPosMs = -1;  // Track last position where waveform was rebuilt
    QQueue<float> m_pendingEnvelope;
    bool m_liveEnvelopeStarted;
    float m_liveEnvelopeSmoothed;
    bool m_bpmAnalyzed;
    int m_bpm;
    QTime m_cuePosition;
    QTime m_beatPosition;
    QTime m_barAnchorPosition;
    double m_barPhaseConfidence;
    QString m_infoBaseText;
    bool m_envelopeScrubbing;
    int m_envelopeScrubAnchorMs;
    QTimer* m_envelopeScrubSeekTimer;
    int m_pendingEnvelopeScrubTargetMs;
    int m_lastEnvelopeScrubAppliedMs;
    QElapsedTimer m_sessionTimer;
    QElapsedTimer m_visualFrameTimer;  // For interpolating position in timerVisual_timeOut
    qint64 m_aboutFinishSuppressUntilMs;
    int m_aboutFinishStableTicks;
    QPushButton* m_beatJumpButtons[4];
    QPushButton* m_monitorRouteButton;
    QSlider*     m_pitchSlider;
    QPushButton* m_pitchResetButton;
    QPushButton* m_syncButton;
    int m_lastControlsPanelWidth;
    QString m_monitorOutputDeviceId;
    bool m_monitorRouteAvailable;
    bool m_monitorRouteEnabled;
    qint64 m_simulatedPositionMs;
     bool m_isSimulating;
     int mTrackFinishEmitTime;

     struct PlayerWidgetPrivate* p;

    // Cue-point sub-object — computation of cue positions, fade points, auto-cue.
    std::unique_ptr<PlayerCueManager> m_cueManager;
};

#endif // PLAYERWIDGET_H
