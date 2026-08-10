/*
    Copyright (C) 2005-2026 Mario Stephan <mstephan@shared-files.de>

    This library is free software and/or modify
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

#include "playercuemanager.h"
#include "trackanalyzer.h"
#include "playerwidget.h"
#include "../ui_playerwidget.h"
#include "playerbpmwidget.h"
#include <QDebug>
#include <QtGlobal>

PlayerCueManager::PlayerCueManager(PlayerWidget& owner)
    : m_owner(owner)
{
}

QTime PlayerCueManager::calculateCuePosition() const { return calculateCuePosition(CUE_SKIP_SILENT); }

void PlayerCueManager::setSkipSilentEnd(bool checked) { m_skipSilentEnd = checked; }

bool PlayerCueManager::skipSilentEnd() const { return m_skipSilentEnd; }

QTime PlayerCueManager::computeFadePoint() const
{
    return computeFadePoint(CUE_SKIP_SILENT_OCCURRENCE);
}

QTime PlayerCueManager::computeFadePoint(CueMode mode) const
{
    if (!m_owner.trackanalyzer || !m_owner.trackanalyzer->finished())
        return QTime();

    const QTime beatEnd = m_owner.trackanalyzer->beatEndPosition();
    const QTime trackEnd = m_owner.trackanalyzer->endPosition();

    if (mode == CUE_SKIP_SILENT)
        return trackEnd;
    if (mode == CUE_BEAT_OCCURRENCE)
        return beatEnd.isValid() && beatEnd > QTime(0, 0) ? beatEnd : trackEnd;

    QTime fadePoint;

    if (beatEnd.isValid() && beatEnd > QTime(0, 0))
        fadePoint = beatEnd;
    if (trackEnd.isValid() && trackEnd > QTime(0, 0)) {
        if (!fadePoint.isValid() || trackEnd < fadePoint)
            fadePoint = trackEnd;
    }

    return (fadePoint.isValid() && fadePoint > QTime(0, 0)) ? fadePoint : trackEnd;
}

long PlayerCueManager::computeRemainCueTime(const QTime& fadePoint) const
{
    if (!fadePoint.isValid() || fadePoint <= QTime(0, 0))
        return 0;

    QTime length = m_owner.trackanalyzer ? m_owner.trackanalyzer->length() : m_owner.player->length();
    return qMax(0, fadePoint.msecsTo(length));
}

PlayerCueManager::CuePoints PlayerCueManager::computeCuePoints(CueMode mode) const
{
    CuePoints result{};

    if (!m_owner.trackanalyzer || !m_owner.trackanalyzer->finished())
        return result;

    // Determine cue start based on mode semantics.
    if (mode == CUE_BEAT_OCCURRENCE || mode == CUE_SKIP_SILENT_OCCURRENCE) {
        // Use beatStartPosition() for beat-based modes.
        const QTime firstBeat = m_owner.trackanalyzer->beatStartPosition();
        const QTime firstEnergy = m_owner.trackanalyzer->startPosition();
        const QTime length = m_owner.trackanalyzer->length();
        const int lengthMs = length.msecsSinceStartOfDay();
        const int firstBeatMs = firstBeat.msecsSinceStartOfDay();
        const bool beatIsPlausible = firstBeat.isValid() && firstBeat > QTime(0, 0)
            && (lengthMs <= 0 || firstBeatMs < lengthMs / 2);
        result.start = (beatIsPlausible
                        && (!firstEnergy.isValid() || firstEnergy <= QTime(0, 0) || firstBeat >= firstEnergy))
                           ? firstBeat
                           : (firstEnergy > QTime(0, 0) ? firstEnergy : QTime(0, 0));
    } else {
        // CUE_SKIP_SILENT: skip the silent intro entirely → raw first active frame.
        result.start = m_owner.trackanalyzer->startPosition();
    }

    result.end       = computeFadePoint(mode);
    result.valid     = result.start.isValid() && result.start > QTime(0, 0);
    return result;
}

QTime PlayerCueManager::calculateCuePosition(CueMode mode) const
{
    // Beat-based modes can return a valid cue position even when the analyzer
    // has not yet finished.
    if (mode == CUE_BEAT_OCCURRENCE || mode == CUE_SKIP_SILENT_OCCURRENCE) {
        if (m_owner.trackanalyzer && m_owner.trackanalyzer->finished()) {
            const QTime firstBeat = m_owner.trackanalyzer->beatStartPosition();
            const QTime firstEnergy = m_owner.trackanalyzer->startPosition();
            const QTime length = m_owner.trackanalyzer->length();
            const int lengthMs = length.msecsSinceStartOfDay();
            const bool beatIsPlausible = firstBeat.isValid() && firstBeat > QTime(0, 0)
                && (lengthMs <= 0 || firstBeat.msecsSinceStartOfDay() < lengthMs / 2);
            if (beatIsPlausible
                    && (!firstEnergy.isValid() || firstEnergy <= QTime(0, 0) || firstBeat >= firstEnergy)) {
                return firstBeat;
            }
            if (firstEnergy.isValid() && firstEnergy > QTime(0, 0)) {
                return firstEnergy;
            }
        }
        // analyzer not ready or no beat — fall back to first-significant-energy
        return computeFallbackCueFromAnalyzer();
    }

    // CUE_SKIP_SILENT needs live analyzer results.
    if (!m_owner.trackanalyzer || !m_owner.trackanalyzer->finished())
        return QTime();

    if (mode == CUE_SKIP_SILENT) {
        const CuePoints cue = computeCuePoints(mode);
        return cue.valid ? cue.start : QTime();
    }

    return determineSilentEndCuePosition(mode);
}

void PlayerCueManager::applyCuePoints(const CuePoints& cue, bool isManual)
{
    if (!cue.valid)
        return;

    if (cue.start.isValid() && cue.start > QTime(0, 0)) {
        m_owner.player->setPosition(cue.start);
    }

    const QTime length = m_owner.trackanalyzer ? m_owner.trackanalyzer->length() : m_owner.player->length();
    if (cue.end.isValid() && cue.end > QTime(0, 0) && length > QTime(0, 0)) {
        m_owner.remainCueTime = qMax(0, cue.end.msecsTo(length));
        m_owner.ui->txtCue->setText("-" + QString::number(m_owner.remainCueTime / 1000));
    }

    m_owner.bpmWidget->setTrackLength(m_owner.player->length());
    m_owner.bpmWidget->setState(m_owner.m_bpm, cue.start,
                                m_owner.m_beatPosition, m_owner.m_isStarted, m_owner.m_bpmAnalyzed);

    if (isManual)
        m_owner.ui->butCue->setChecked(true);

    m_owner.updateTimeAndPositionDisplay(false);
}

void PlayerCueManager::applyAutoCueAfterAnalysis(bool preferBeatCue)
{
    if (!m_owner.m_CurrentTrack || !m_owner.trackanalyzer)
        return;

    // Never reposition an actively playing track from background analysis.
    if (m_owner.m_isStarted)
        return;

    // If analysis has not finished and we have no cached BPM, skip auto-cue.
    // The analyzer will call this again when finished.
    if (!m_owner.trackanalyzer->finished() && m_owner.m_bpm <= 0) {
        qDebug() << "PlayerCueManager::applyAutoCueAfterAnalysis: skipping auto-cue, analyzer not finished and no cached BPM";
        return;
    }

    CueMode mode = preferBeatCue ? CUE_BEAT_OCCURRENCE : CUE_SKIP_SILENT;
    QTime cuePosition = calculateCuePosition(mode);

    // If analysis isn't ready yet, fall back to first-significant-energy position.
    // Do not use cached beat phase offset (m_beatPosition) — it is not a cue start
    // and causes premature cueing around ~0.4s (e.g. 00:00:00.449).
    if ((!cuePosition.isValid() || cuePosition <= QTime(0, 0))) {
        cuePosition = computeFallbackCueFromAnalyzer();
    }

    if (cuePosition.isValid() && cuePosition > QTime(0, 0)) {
        m_owner.player->setPosition(cuePosition);

        if (mode == CUE_SKIP_SILENT) {
            const QTime fade = computeFadePoint();
            if (fade.isValid() && fade > QTime(0, 0))
                m_owner.remainCueTime = computeRemainCueTime(fade);
            else
                m_owner.remainCueTime = 0;
        }

        qInfo() << "CueManager: auto-cue applied. Start:" << cuePosition
                 << "remainCueTime:" << m_owner.remainCueTime
                 << "mode:" << (preferBeatCue ? "beatOccurrence" : "skipSilent");
    } else {
        qWarning("PlayerCueManager::applyAutoCueAfterAnalysis: no valid cue position, cached BPM=%d beatPosition=%s",
                 m_owner.m_bpm, m_owner.m_beatPosition.toString().toLatin1().constData());
    }
}

// ---- Private helpers ----

QTime PlayerCueManager::computeFallbackCueFromAnalyzer() const {
    if (!m_owner.trackanalyzer)
        return QTime();

    int cachedMs = m_owner.trackanalyzer->property("cachedCueStartMs").toInt();
    if (cachedMs > 0) {
        qDebug() << Q_FUNC_INFO << "Using cached cue start position:" << QTime(0, 0).addMSecs(cachedMs);
        return QTime(0, 0).addMSecs(cachedMs);
    }

    QTime firstEnergy = m_owner.trackanalyzer->startPosition();
    if (firstEnergy.isValid() && firstEnergy > QTime(0, 0)) {
        qDebug() << Q_FUNC_INFO << "Using first significant energy position:" << firstEnergy;
        return firstEnergy;
    }

    return QTime();
}

QTime PlayerCueManager::determineSilentEndCuePosition(CueMode mode) const
{
    if (!m_owner.trackanalyzer || !m_owner.trackanalyzer->finished()) {
        return QTime();
    }
    const CuePoints cue = computeCuePoints(mode);
    return cue.valid ? cue.end : QTime();
}
