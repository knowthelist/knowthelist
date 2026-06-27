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

#ifndef PLAYERCUEMANAGER_H
#define PLAYERCUEMANAGER_H

#include <QTime>
#include <QVector>

class TrackAnalyzer;
class PlayerBpmWidget;
class PlayerWidget;

/**
 * Manages cue-point computation and application for a player deck.
 *
 * Responsibilities:
 *   - Compute fade points (earliest of beat-activity-end / track-end)
 *   - Calculate remaining cue time
 *   - Apply computed cues to the player widget (position, slider, BPM widget state)
 *   - Coordinate with TrackAnalyzer for auto-cue after analysis
 */
class PlayerCueManager {
public:
    explicit PlayerCueManager(PlayerWidget& owner);
    ~PlayerCueManager() = default;

    // ---- Fade point helpers ----
    QTime computeFadePoint() const;
    long computeRemainCueTime(const QTime& fadePoint) const;

    // ---- Cue mode computation ----
    struct CuePoints {
        bool valid{false};
        QTime start;
        QTime end;
    };

    enum CueMode {
        CUE_SKIP_SILENT,
        CUE_BEAT_OCCURRENCE
    };

    CuePoints computeCuePoints(CueMode mode) const;
    QTime calculateCuePosition() const;
    QTime calculateCuePosition(CueMode mode) const;

    // ---- Apply / Auto-cue ----
    void applyCuePoints(const CuePoints& cue, bool isManual);
    void applyAutoCueAfterAnalysis(bool preferBeatCue);

    // ---- Position helpers ----
    QTime determineSilentEndCuePosition(CueMode mode) const;

    void setSkipSilentEnd(bool checked);
    bool skipSilentEnd() const;

private:
    PlayerWidget& m_owner;
    bool m_skipSilentEnd;
    bool m_skipSilentBegin;
};

#endif // PLAYERCUEMANAGER_H
