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

#ifndef PLAYLISTITEM_H
#define PLAYLISTITEM_H

 #include <QTreeWidgetItem>
#include <QColorGroup>
#include <qurl.h>
#include "track.h"
#include "playlistitem.h"
#include "playlist.h"

class PlaylistItem : public QTreeWidgetItem
{
public:
    PlaylistItem( Playlist *pl, QTreeWidgetItem *lvi );
    ~PlaylistItem();
    QString urlString() const { return text(Url ); }
    QString title() const { return text( Title ); }
    QString artist() const { return text( Artist ); }
    Playlist *listView() const { return (Playlist*)treeWidget(); }
    PlaylistItem *nextSibling() const { return (PlaylistItem*)treeWidget()->itemBelow(this); }

    Track *track() { return m_track ; }
    void setTrack( Track*);

    QColor foreColor() { return m_foreColor ; }
    void setForeColor( QColor c ) {m_foreColor=c; update();}

    void update();


    void setTexts( Track* );
    void setText( int c, QString  Text);
    QString exactText( int col ) const { return text( col ); }
    QString seconds() const;
      enum Column  { Url = 0,
                      No = 1,
                      Artist = 2,
                      Title = 3,
                      Album = 4,
                      Year = 5,
                      Comment = 6,
                      Genre = 7,
                      Directory = 8,
                      Tracknumber = 9,
                      Length = 10,
                      Bitrate = 11 };
                      
    private:
        //static QString trackUrl( const QUrl &u ) { return u.protocol() == "file" ? u.fileName() : u.toString(); }
        Track *m_track;
        QColor m_foreColor;
};

#endif
