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

#include "djsession.h"
#include <QtConcurrentRun>
#include "track.h"
#include "dj.h"

struct DjSessionPrivate
{
        //QFutureWatcher<void> watcher1;
        //QFutureWatcher<void> watcher2;
        QMutex mutex1;
        QMutex mutex2;

        Dj* currentDj;
        int minCount;
        CollectionDB* database;
        QList<Track*> playList1_Tracks;
        QList<Track*> playList2_Tracks;
        QList<Track*> seenTracks;
};

DjSession::DjSession()
    :p(new DjSessionPrivate)
{
    p->database  = new CollectionDB();
    p->minCount=10;
    p->currentDj=0;
}

DjSession::~DjSession()
{
    delete p;
}

void DjSession::setCurrentDj(Dj* dj)
{
    // retire current Auto-DJ
    if (p->currentDj)
           disconnect(p->currentDj,SIGNAL(filterChanged(Filter*)),
           this,SLOT(on_dj_filterChanged(Filter*)));
    // hire new DJ
    p->currentDj = dj;
    connect(p->currentDj,SIGNAL(filterChanged(Filter*)),
           this,SLOT(on_dj_filterChanged(Filter*)));
}

Dj* DjSession::currentDj()
{
    return p->currentDj;
}

void DjSession::searchTracks()
{
    // init qrand
    QTime time = QTime::currentTime();
    qsrand((uint)time.msec());

    // how many tracks are needed
    p->mutex1.lock();
    int diffCount1= p->minCount - p->playList1_Tracks.count();
    int diffCount2= p->minCount - p->playList2_Tracks.count();
    int needed = (diffCount1 > 0) ? diffCount1 : 0;
    needed = (diffCount2 > 0) ? needed + diffCount2 : needed;

    qDebug() << __PRETTY_FUNCTION__ << " need "<<diffCount1<<" tracks left and "<<diffCount2<<" tracks right ";
    qDebug() << __PRETTY_FUNCTION__ << " needed together: "<<needed;

    // retrieve new random tracks for both playlists
    QList<Track*> tracks1;
    QList<Track*> tracks2;
    for (int i=0; i<needed ; i++)
    {
        if ( i % 2== 0 ){
            if ( diffCount1 > 0)
                tracks1.append( getRandomTrack() );
            else
                tracks2.append( getRandomTrack() );
        }
        else{
            if ( diffCount2 > 0)
                tracks2.append( getRandomTrack() );
            else
                tracks1.append( getRandomTrack() );
        }

    }

    // new tracks available, trigger fill up of playlists
    qDebug() << __PRETTY_FUNCTION__ << " provide "<<tracks1.count()<<" tracks left and "<<tracks2.count()<<" tracks right ";

    emit foundTracks_Playlist1(tracks1);
    emit foundTracks_Playlist2(tracks2);

    p->mutex1.unlock();
}


/*ToDo:
- Genre: Combobox with distinct all genres
  */
Track* DjSession::getRandomTrack()
{
    Track* track;
    int i=0;
    Filter* f = p->currentDj->requestFilter();
    int maxCount=0;

    do {
        track = new Track( p->database->getRandomEntry(f->path(),f->genre(),f->artist()) );

        if (maxCount==0)
            maxCount=p->database->lastMaxCount();
        i++;
    }
    while ((track->prettyLength()=="?"
           || track->containIn( p->playList1_Tracks )
           || track->containIn( p->playList2_Tracks )
           || track->containIn( p->seenTracks ))
           && i<maxCount*3);
    if (i>=maxCount*3)
        qDebug() << __PRETTY_FUNCTION__ << " no new track found.";
    else
        qDebug() << __PRETTY_FUNCTION__ <<i<<" iterations to found a new track "<<i;

    f->setCount(maxCount);
    f->setLength(p->database->lastLengthSum());
    p->seenTracks.append(track);
    return track;
}

void DjSession::fillPlaylists()
{
    QFuture<void> future = QtConcurrent::run( this, &DjSession::searchTracks);
    //p->watcher1.setFuture(future1);
}


void DjSession::on_dj_filterChanged(Filter* f)
{
    //qDebug() << __PRETTY_FUNCTION__ ;
    int cnt = p->database->getCount(f->path(),f->genre(),f->artist());
    f->setLength(p->database->lastLengthSum());
    f->setCount(cnt);
}

void DjSession::onResetStats()
{
    p->database->resetSongCounter();
}

void DjSession::onTrackFinished(Track *track)
{
    p->database->incSongCounter(track->url().toLocalFile());
}

void DjSession::onTracksChanged_Playlist1(QList<Track*> ts)
{
    p->playList1_Tracks = ts;
}

void DjSession::onTracksChanged_Playlist2(QList<Track*> ts)
{
    p->playList2_Tracks = ts;
}

int DjSession::minCount()
{
    return p->minCount;
}

void DjSession::setMinCount(int value)
{
    p->minCount=value;
}

