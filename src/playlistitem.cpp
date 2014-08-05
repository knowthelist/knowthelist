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

 #include "playlistitem.h"
 #include "playlist.h"
 #include "ratingwidget.h"

 #include <qdebug.h>
 #include <qlistview.h>
 #include <QColorGroup>
 #include <QObject>
 
PlaylistItem::PlaylistItem( Playlist* parent, QTreeWidgetItem *lvi )
      : QTreeWidgetItem( parent, lvi )
      , m_track(new Track())
      , m_parent(parent)
{
        //qDebug() << u << " 2."<<m_url;
}


PlaylistItem::~PlaylistItem()
{
    if (m_track)
        delete m_track;
}

void PlaylistItem::setTrack( Track *track )
{
    m_track = track;
    setTexts(m_track);
}

void PlaylistItem::setTexts( Track *track )
{
    //qDebug()<<track->url();

    setText( Column_Url,   track->url().toString() );
    setText( Column_Artist,  ( track->artist() != "" ) ? track->artist() : QObject::tr("Unknown")  );
    setText( Column_Title,   ( track->title() != "" ) ? track->title() : QObject::tr("Unknown") );
    setText( Column_Album,   track->album() );
    setText( Column_Year,    track->year() );
    setText( Column_Genre,   track->genre() );
    setText( Column_Length,  track->prettyLength() );
    setText( Column_Tracknumber,  track->tracknumber() );
    setText( Column_Played,   QString::number(track->counter()) );

    const QString body = "<tr><td>%1</td><td>%2</td></tr>";

    QString
            str  = "<html><body><table STYLE=\"border-collapse: collapse\"> width=\"100%\" border=\"1\">";
    str += body.arg( QObject::tr( "Title" ),      track->title() );
    str += body.arg( QObject::tr( "Artist" ),     track->artist() );
    str += body.arg( QObject::tr( "Album" ),      track->album() );
    str += body.arg( QObject::tr( "Genre" ),      track->genre() );
    str += body.arg( QObject::tr( "Year" ),       track->year() );
    str += body.arg( QObject::tr( "Location" ),   track->url().toString() );
    str += "</table></body></html>";

    setToolTip(Column_Artist,str);

    m_track = track;
}

void PlaylistItem::setText( int c, QString text)
{
    QTreeWidgetItem::setText( c, ( text != "" ) ? text : QObject::tr("Unknown") );
    switch (c) {
    case Column_Url:
        m_track->setUrl(QUrl::fromLocalFile(text));
        break;
    case Column_Artist:
        m_track->setArtist(text);
        break;
    case Column_Title:
        m_track->setTitle(text);
        break;
    case Column_Album:
        m_track->setAlbum(text);
        break;
    case Column_Played:
        m_track->setCounter(text);
        break;
    case Column_Genre:
        m_track->setGenre(text);
        break;
    case Column_Length:
        m_track->setLength(text);
        break;
    }
}

void PlaylistItem::update()
{
    // paint  item in special color
    for(int i = 0; i<=columnCount()-1; i++)
        setForeground(i,QBrush(m_foreColor));
}


int PlaylistItem::rate() {
    return ((RatingWidget*)m_parent->itemWidget(this,Column_Rate))->rating()*0.1;
}

bool PlaylistItem::operator< (const QTreeWidgetItem &other) const
{
    bool ok;
    int sortCol = treeWidget()->sortColumn();
    QString otherText = other.text(sortCol).replace(QObject::tr("Unknown"),"0");

    if ( sortCol == Column_Length){
        int otherLength=-1;
        if ( otherText.contains(':') ){
            QStringList list=otherText.split(':');
            otherLength=list.at(0).toInt(&ok)*60;
            otherLength+=list.at(1).toInt(&ok);
        if (ok)
            return m_track->length() < otherLength;
        }
    }
    else {
        int value = text(sortCol).replace(QObject::tr("Unknown"),"0").toInt(&ok);
        int otherValue = otherText.toInt(&ok);
        if (ok)
            return value < otherValue;
    }

    return QTreeWidgetItem::operator <(other);
}
