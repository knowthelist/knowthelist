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

#ifndef KNOWTHELIST_H
#define KNOWTHELIST_H

#include "collectionwidget.h"
#include "beatsyncwidget.h"
#include "djbrowser.h"
#include "djsession.h"
#include "filebrowser.h"
#include "monitorplayer.h"
#include "playerwidget.h"
#include "qled.h"
#include "playlist.h"
#include "playlistbrowser.h"
#include "settingsdialog.h"
#include "vumeter.h"

#include <QMainWindow>
#include <QPushButton>
#include <QSplitter>
#include <QTime>
#include <QToolButton>

namespace Ui {
class Knowthelist;
}

class Knowthelist : public QMainWindow {
    Q_OBJECT

public:
    explicit Knowthelist(QWidget* parent = 0);
    ~Knowthelist();

private Q_SLOTS:
    //Auto connect slots
    void on_potHigh_1_valueChanged(int value);
    void on_potMid_1_valueChanged(int value);
    void on_potLow_1_valueChanged(int value);
    void on_potHigh_2_valueChanged(int value);
    void on_potMid_2_valueChanged(int value);
    void on_potLow_2_valueChanged(int value);
    void on_toggleAutoFade_toggled(bool checked);
    void on_toggleAutoDJ_toggled(bool checked);
    void on_toggleAGC_toggled(bool checked);
    void on_potGain_2_valueChanged(int value);
    void on_potGain_1_valueChanged(int value);
    void on_sliMonitor_sliderMoved(int position);
    void on_cmdMonitorPlay_clicked();
    void on_cmdMonitorStop_clicked();
    void on_cmdMonitorSettings_clicked();
    void on_resetAnalysisCachePressed();
    void on_cmdFade_clicked();

    void timerMonitor_timeOut();
    void timerAutoFader_timerOut();

    void player_aboutTrackFinished();
    void player1_gainChanged(double newGain);
    void player2_gainChanged(double gainValue);
    void player1_trackFinished();
    void player2_trackFinished();
    void player1_levelChanged(double left, double right);
    void player2_levelChanged(double left, double right);
    void player1_tempoChanged(int bpm, QTime beatPosition);
    void player2_tempoChanged(int bpm, QTime beatPosition);
    void player1_syncRequested();
    void player2_syncRequested();
    void on_playerSyncButtonToggled(bool checked);
    void player1_monitorRouteToggled(bool enabled);
    void player2_monitorRouteToggled(bool enabled);
    void playlist1_currentTrackChanged(Track* track);
    void playlist2_currentTrackChanged(Track* track);
    void on_toggleAutoSync_toggled(bool checked);
    void on_toggleBeatVisual_toggled(bool checked);

    void slider1_valueChanged(int);
    void slider2_valueChanged(int);
    void sliFader_valueChanged(int);

    void savePlaylists();
    void monitorPlayer_trackTimeChanged(qint64, qint64);
    void timerMonitor_loadFinished();
    void startAutoDj();
    void currentDjChanged(Dj*);

    void timerGain1_timeOut();
    void timerGain2_timeOut();
    void timerBeatSyncVisual_timeOut();
    void timerRateRestore_timeOut();
    void Track_doubleClicked(Track*);
    void trackList_wantLoad(Track*, QString target);
    void Track_selectionChanged(Track*);
    bool initMonitorPlayer();
    void editSettings();
    void on_cmdOptions_clicked();
    void showCollectionSetup();
    void onWantLoad(QList<Track*>, QString);
    void on_lblSoundcard_linkActivated(const QString& link);

    void on_sliMonitor_actionTriggered(int action);

    void on_sliMonitorVolume_valueChanged(int value);

    // New methods to handle synchronized beat sync
    void setPlayer1BeatSyncEnabled(bool enabled);
    void setPlayer2BeatSyncEnabled(bool enabled);

private:
    enum FadeSyncPhase {
        FadeSyncIdle = 0,
        FadeSyncPreRoll,
        FadeSyncCrossfade,
        FadeSyncRestore
    };

    Ui::Knowthelist* ui;
    void createUI();
    void fadeNow();
    void beginPlainFade(PlayerWidget* incoming);
    void beginAutoFadeSync(PlayerWidget* outgoing, PlayerWidget* incoming,
                           int outgoingBpm, int incomingBpm,
                           const QTime& outgoingBeatPosition);
    void applyAutoFadeSharedTempo(double sharedTempoBpm);
    double autoFadeSharedTempoForStep(int step) const;
    void clearAutoFadeSyncState();
    void resetWaitingDeckTempoPreviews();
    void resetAllDecksSyncState();
    void applyBeatVisualMode(bool enabled);
    void applyAutoSyncEnabled(bool enabled);
    void updatePlayerMonitorRouting();
    double selectAutoFadeTargetTempo(double startTempoBpm, int outgoingBpm, int incomingBpm) const;
    void setFaderModeToPlayer();
    QTimer* timerAutoFader;
    int m_xfadeDir;
    int gain1Target;
    int gain2Target;
    bool isFading;
    VUMeter* vuMeter1;
    VUMeter* vuMeter2;
    BeatSyncWidget* beatSyncWidget;
    VUMeter* monitorMeter;
    QTimer* timerMeter;
    QTimer* timerMonitor;
    QTimer* timerGain1;
    QTimer* timerGain2;
    QTimer* timerBeatSyncVisual;
    Playlist* playList1;
    Playlist* playList2;
    Playlist* trackList;
    Playlist* trackList2;

    QSplitter* splitter;
    QSplitter* splitterPlaylist;

    CollectionWidget* collectionBrowser;
    MonitorPlayer* monitorPlayer;
    DjSession* djSession;
    DjBrowser* djBrowser;

    PlayerWidget* player1;
    PlayerWidget* player2;
    QToolButton* m_monitorSettingsButton;
    QPushButton* m_toggleAutoSyncButton;
    QPushButton* m_toggleBeatVisualButton;
    QLed* m_autoSyncLed;
    FileBrowser* filetree;
    PlaylistBrowser* playlistBrowser;

    SettingsDialog* preferences;

    bool autoFadeOn;

    QString m_AutoDJGenre;
    int mAutofadeLength;
    int mAboutFinishTime;
    int mMinTracks;
    bool wantSeek;
    Track* m_MonitorTrack;
    int m_Player1Bpm;
    int m_Player2Bpm;
    QTime m_Player1BeatPosition;
    QTime m_Player2BeatPosition;
    QTimer* m_rateRestoreTimer;
    PlayerWidget* m_rateRestorePlayer;
    bool m_autoSyncEnabled;
    FadeSyncPhase m_fadeSyncPhase;
    PlayerWidget* m_fadeSyncOutgoingPlayer;
    PlayerWidget* m_fadeSyncIncomingPlayer;
    int m_fadeSyncOutgoingBpm;
    int m_fadeSyncIncomingBpm;
    QTime m_fadeSyncOutgoingBeatPosition;
    double m_fadeSyncStartTempoBpm;
    double m_fadeSyncTargetTempoBpm;
    int m_fadeSyncStep;
    int m_fadeSyncPreRollSteps;
    int m_fadeSyncCrossfadeSteps;
    int m_fadeSyncRestoreSteps;
    int m_fadeSyncTotalSteps;
    bool m_fadeSyncWaitingBeatStart;
    int m_fadeSyncBeatWaitSteps;

protected:
    virtual void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

    void changeVolumes();
    void loadStartSettings();
    void loadCurrentSettings();
};

#endif // KNOWTHELIST_H
