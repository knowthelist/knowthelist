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
    if (!m_owner.trackanalyzer || !m_owner.trackanalyzer->finished())
        return QTime();

    QTime beatEnd   = m_owner.trackanalyzer->beatActivityEndPosition();
    QTime trackEnd  = m_owner.trackanalyzer->endPosition();
    QTime fadePoint;

    if (beatEnd.isValid() && beatEnd > QTime(0, 0))
        fadePoint = beatEnd;
    if (trackEnd.isValid() && trackEnd > QTime(0, 0)) {
        if (!fadePoint.isValid() || trackEnd < fadePoint)
            fadePoint = trackEnd;
    }

    return (fadePoint.isValid() && fadePoint > QTime(0, 0)) ? fadePoint : m_owner.trackanalyzer->endPosition();
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

    if (mode == CUE_BEAT_OCCURRENCE) {
        const QTime firstEnergy = m_owner.trackanalyzer->firstSignificantEnergyPosition();
        result.start = firstEnergy.isValid() && firstEnergy > QTime(0, 0)
                           ? firstEnergy
                           : (m_owner.trackanalyzer->startPosition() > QTime(0, 0) ? m_owner.trackanalyzer->startPosition() : QTime(0, 0));
        result.end = computeFadePoint();
        result.valid = result.start.isValid() && result.start > QTime(0, 0);
        return result;
    }

    const QTime firstEnergy = m_owner.trackanalyzer->firstSignificantEnergyPosition();
    result.start = firstEnergy.isValid() && firstEnergy > QTime(0, 0)
                       ? firstEnergy
                       : (m_owner.trackanalyzer->startPosition() > QTime(0, 0) ? m_owner.trackanalyzer->startPosition() : QTime(0, 0));

    result.end = computeFadePoint();

    result.valid = true;
    return result;
}

QTime PlayerCueManager::calculateCuePosition(CueMode mode) const
{
    // For CUE_BEAT_OCCURRENCE mode, we can return a valid cue position even when
    // the analyzer hasn't finished yet, as long as we have the first significant
    // energy position from asyncOpen(). This allows auto-cue to work with cached
    // tempo data before the analyzer completes.
    if (mode == CUE_BEAT_OCCURRENCE) {
        // Try the beat-grid snapped position first (from detectTempo)
        if (m_owner.trackanalyzer && m_owner.trackanalyzer->finished()) {
            QTime firstEnergy = m_owner.trackanalyzer->firstSignificantEnergyPosition();
            if (firstEnergy.isValid() && firstEnergy > QTime(0, 0)) {
                return firstEnergy;
            }
        }
        // Fallback to the raw first significant energy position (from asyncOpen)
        // This is available even when cached tempo is used (STANDARD mode)
        if (m_owner.trackanalyzer) {
            const int cachedCueStartMs = m_owner.trackanalyzer->property("cachedCueStartMs").toInt();
            if (cachedCueStartMs > 0)
                return QTime(0, 0).addMSecs(cachedCueStartMs);

            QTime firstEnergy = m_owner.trackanalyzer->startPosition();
            if (firstEnergy.isValid() && firstEnergy > QTime(0, 0)) {
                return firstEnergy;
            }
        }
        // If analyzer hasn't finished yet and no first significant energy position
        // is available, return invalid to let the analyzer complete first.
        // The auto-cue will be applied after the analyzer finishes.
        return QTime(0, 0);
    }

    // Skip_Silent and silent-end modes need live analyzer results — guard is correct.
    if (!m_owner.trackanalyzer || !m_owner.trackanalyzer->finished()) {
        return QTime();
    }

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
    // Guard to match PlayerWidget::applyAutoCueAfterAnalysis: do not reposition while playing.
    if (m_owner.m_isStarted && !preferBeatCue)
        return;

    // If preferBeatCue is true but analyzer hasn't finished and we don't have cached beat data,
    // skip auto-cue - the analyzer will call this again when finished.
    if (preferBeatCue && !m_owner.trackanalyzer->finished() && m_owner.m_bpm <= 0) {
        qDebug() << "PlayerCueManager::applyAutoCueAfterAnalysis: skipping auto-cue, analyzer not finished and no cached BPM";
        return;
    }

    CueMode mode = preferBeatCue ? CUE_BEAT_OCCURRENCE : (m_skipSilentEnd ? CUE_SKIP_SILENT : CUE_BEAT_OCCURRENCE);
    QTime cuePosition = calculateCuePosition(mode);

    // If analysis isn't ready yet, use analyzer-provided first-energy/start positions only.
    // Do not fall back to cached beat phase offset (m_beatPosition), because that is not
    // a cue start and causes premature cueing around ~0.4s (e.g. 00:00:00.449).
    if ((!cuePosition.isValid() || cuePosition <= QTime(0, 0))) {
        // Try the raw first significant energy position (from asyncOpen) first
        if (m_owner.trackanalyzer) {
            const int cachedCueStartMs = m_owner.trackanalyzer->property("cachedCueStartMs").toInt();
            if (cachedCueStartMs > 0)
                cuePosition = QTime(0, 0).addMSecs(cachedCueStartMs);

            QTime firstEnergy = m_owner.trackanalyzer->startPosition();
            if (firstEnergy.isValid() && firstEnergy > QTime(0, 0)) {
                cuePosition = firstEnergy;
            }
        }
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

QTime PlayerCueManager::determineSilentEndCuePosition(CueMode mode) const
{
    if (!m_owner.trackanalyzer || !m_owner.trackanalyzer->finished()) {
        return QTime();
    }
    const CuePoints cue = computeCuePoints(mode);
    return cue.valid ? cue.end : QTime();
}
