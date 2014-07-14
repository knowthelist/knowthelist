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

#ifndef TRACK_H
#define TRACK_H


#include <QUrl>
#include <QImage>
#include <QStringList>


class PlaylistItem;


namespace TagLib { class AudioProperties; class Tag; }


class Track
{

public:

    Track();
    Track( const QUrl &u);
    Track( const QStringList& list );
    Track( const PlaylistItem *item );
    ~Track();

    static QStringList tagNameList;
    
    QImage coverImage();
    QImage defaultImage();

    bool operator==(Track* track);
    bool containIn(QList<Track*> list );

    QUrl   url();
    QString title();
    QString artist();
    QString album();
    QString year();
    QString comment();
    QString genre();
    int length();
    int counter();
    int target();
    QString tracknumber();
    QString prettyLength();
    QStringList tagList();
    QString dirPath();

    bool isValid();

    void setUrl(QUrl);
    void setTitle(QString);
    void setArtist(QString);
    void setAlbum(QString);
    void setYear(QString);
    void setComment(QString);
    void setGenre(QString);
    void setTracknumber(QString);
    void setLength(QString);
    void setLengthFromPretty(QString);
    void setCounter(QString);
    void setTarget(int);

    
    QString prettyTitle() const;
    QString prettyArtist(int) const;
    QString prettyTitle(int) const;

    static QString prettyLength( int );
    static QString prettyTime( int, bool showHours = true );
    static QString zeroPad( uint i ) { return ( i < 10 ) ? QString( "0%1" ).arg( i ) : QString::number( i ); }
    static QString prettyTitle( QString );

protected:

    QString rsqueeze( const QString&, int ) const;
    void readTags();

private:

    struct TrackPrivate *p;

};
#endif
