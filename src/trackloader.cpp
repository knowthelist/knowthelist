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

#include "trackloader.h"
#include "playerwidget.h"
#include "player.h"
#include "trackanalyzer.h"
#include "ui_playerwidget.h"
#include "vumeter.h"

TrackLoader::TrackLoader(PlayerWidget* parent)
    : m_parent(parent)
{
}

TrackLoader::~TrackLoader()
{
}

void TrackLoader::loadTrack(Track* track)
{
    if (track) {
        qDebug() << Q_FUNC_INFO << ":" << m_parent->objectName() << " track=" << track->url();

        // Update current track reference in parent widget
        m_parent->m_CurrentTrack = track;
        m_parent->m_pendingPlay = false;
        m_parent->m_bpmAnalyzed = false;
        m_parent->m_bpm = 0;
        m_parent->m_tempoRate = 1.0;
        m_parent->m_syncAdopting = false;
        m_parent->m_visualLatencyMs = 0;        
        // Draw title for new track
        drawTitleHelper(m_parent);

        bool doPlay = m_parent->m_isStarted;
        player::stop();

        QUrl url = track->url();
        player->open(url);

        // Check cache first to skip redundant analyzer loading
        QSettings settings;
        const bool analyzeTempo = settings.value("beatSyncAnalyzeTempo", true).toBool();
        CachedTempo cachedTempo = m_parent->loadCachedTempo(url);
        CachedEnvelope cachedEnvelope = m_parent->loadCachedEnvelope(url);