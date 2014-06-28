/*
    Copyright (C) 2005-2014 Mario Stephan <mstephan@shared-files.de>

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

#ifndef DJSESSION_H
#define DJSESSION_H

#include <QObject>
#include <dj.h>
//#include <track.h>
#include "collectiondb.h"
class Track;

class DjSession : public QObject
{
    Q_OBJECT

public:
    DjSession();
    ~DjSession();
    Dj mainDJ;
    int minCount();
    void setMinCount(int value);
    void setCurrentDj(Dj*);
    Dj* currentDj();

Q_SIGNALS:
    void foundTracks_Playlist1(QList<Track*>);
    void foundTracks_Playlist2(QList<Track*>);

public slots:
    void fillPlaylists();
    void onTracksChanged_Playlist1(QList<Track*> ts);
    void onTracksChanged_Playlist2(QList<Track*> ts);
    void onTrackFinished(Track *track);
    void on_dj_filterChanged(Filter* f);
    void onResetStats();

private:
    class DjSessionPrivate *p;
    void searchTracks();

    Track* getRandomTrack();

};

#endif // DJSESSION_H
