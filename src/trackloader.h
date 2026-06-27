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

#ifndef TRACKLOADER_H
#define TRACKLOADER_H

#include "track.h"
#include "playerwidget.h"

class TrackLoader {
public:
    explicit TrackLoader(PlayerWidget* parent);
    ~TrackLoader();

    void loadTrack(Track* track);
    void applyCuePoints(const CuePoints& cuePoints);
    void setPositionMarkers(QTime startMarker, QTime endMarker);

private:
    PlayerWidget* m_parent;
};

#endif // TRACKLOADER_H
