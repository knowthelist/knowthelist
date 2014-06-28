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
    QString urlString() const { return text(Column_Url ); }
    QString title() const { return text( Column_Title ); }
    QString artist() const { return text( Column_Artist ); }
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
      enum Column  { Column_Url = 0,
                      Column_No = 1,
                      Column_Played = 2,
                      Column_Artist = 3,
                      Column_Title = 4,
                      Column_Album = 5,
                      Column_Year = 6,
                      Column_Genre = 7,
                      Column_Tracknumber = 8,
                      Column_Length = 9};
                      
    private:
        //static QString trackUrl( const QUrl &u ) { return u.protocol() == "file" ? u.fileName() : u.toString(); }
        Track *m_track;
        QColor m_foreColor;
};

#endif
