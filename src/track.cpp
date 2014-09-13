/*
    Copyright (C) 2004 Max Howell <max.howell@methylblue.com>
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



#include "track.h"
#include "playlistitem.h"

#include <QFileInfo>
#include <QPainter>
#include <qdebug.h>
    
#include <taglib/tag.h>
#include <taglib/fileref.h>
#include <taglib/tstring.h>
#include <taglib/audioproperties.h>

#include <taglib/mpegfile.h>
#include <taglib/id3v2framefactory.h>
#include <taglib/id3v2frame.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/tbytevector.h>
#include <taglib/id3v2tag.h>

QStringList Track::tagNameList = QStringList() << "location"
                                                     << "creator"
                                                     << "title"
                                                     << "album"
                                                     << "year"
                                                     << "genre"
                                                     << "trackNum"
                                                     << "duration"
                                                     << "counter"
                                                     << "rate";

struct TrackPrivate
{
    QUrl    url;
    QString title;
    QString artist;
    QString album;
    QString year;
    QString comment;
    QString genre;
    QString tracknumber;
    int counter;
    int length;
    int rate;
    Track::Options flags;
};

Track::Track()
   :p(new TrackPrivate)
{
    p->counter=-1;
    p->rate=0;
}

Track::~Track()
{
    delete p;
}

Track::Track( const QUrl &u )
    :p(new TrackPrivate)
{
    p->url=u;
    readTags();
}

Track::Track( const QStringList& list )
    :p(new TrackPrivate)
{
    if (list.count()>9){
        p->url = QUrl::fromLocalFile( list.at(0));
        p->artist = list.at(1);
        p->title  = list.at(2);
        p->album  = list.at(3);
        p->year   = list.at(4);
        p->genre  = list.at(5);
        p->tracknumber = list.at(6);
        p->length= QString(list.at(7)).toInt();
        p->counter = QString(list.at(8)).toInt();
        p->rate = QString(list.at(9)).toInt();
    }
    if (list.count()>10)
        p->flags = QFlag(list.at(10).toInt());
}


Track::Track( const PlaylistItem *item )
    :p(new TrackPrivate)
{
  p->url = QUrl::fromLocalFile(item->urlString()) ;
  p->title  = item->title();
  p->artist = item->exactText( 2 );
  p->album  = item->exactText( 4 );
  p->year   = item->exactText( 5 );
  p->comment= item->exactText( 6 );
  p->genre  = item->exactText( 7 );
  p->tracknumber = item->exactText( 8 );
  p->counter = item->exactText( 9 ).toInt();
 // p->rate = item->rate();

}

void Track::readTags()
{
    QByteArray fileName = QFile::encodeName( p->url.toLocalFile() );
    const char * encodedName = fileName.constData();
    TagLib::FileRef fileref = TagLib::FileRef( encodedName, true, TagLib::AudioProperties::Fast);

   if ( !fileref.isNull() )
   {
        if( fileref.tag() )
        {
            TagLib::Tag *tag = fileref.tag();

            p->title   = !tag->title().isNull() ? TStringToQString( tag->title() ).trimmed() : QObject::tr("Unknown");
            p->artist  = !tag->artist().isNull() ? TStringToQString( tag->artist() ).trimmed() : QObject::tr("Unknown");
            p->album   = !tag->album().isNull() ? TStringToQString( tag->album() ).trimmed() : QObject::tr("Unknown");
            p->comment = TStringToQString( tag->comment() ).trimmed();
            p->genre   = !tag->genre().isNull() ? TStringToQString( tag->genre() ).trimmed() : QObject::tr("Unknown");
            p->year    = tag->year() ? QString::number( tag->year() ) : QString::null;
            p->tracknumber   = tag->track() ? QString::number( tag->track() ) : QString::null;
            p->length     = fileref.audioProperties()->length();
            p->counter = 0;
            p->rate = 0;

            //polish up empty tags
            if( p->title == QObject::tr("Unknown") ) {
                QFileInfo fileInfo(p->url.toLocalFile());
                p->title = fileInfo.fileName().replace( '_', ' ' ).replace('.' + fileInfo.suffix(),"") ;
            }
        }
    }

}

bool  Track::operator==(Track *track) {

    if (!isValid())
        return false;
    return p->artist == track->artist()
            && p->title == track->title();
}

bool Track::containIn( QList<Track*> list )
{
   foreach( Track* i, list ) {
      if( *i == this ){
         return true;
      }
   }
   return false;
}

QImage Track::coverImage()
{
      if (!p->url.isValid())
          return QImage();

      qDebug()<<"image url:"<<p->url;
      if (p->url.path()=="")
          return QImage();

      QByteArray fileName = QFile::encodeName( p->url.toLocalFile() );
      const char * encodedName = fileName.constData();
      TagLib::MPEG::File fileref( encodedName);
	
       if (!fileref.isOpen())
          return QImage();

    TagLib::ID3v2::Tag *tag = fileref.ID3v2Tag();

       if ( tag )
       {
           TagLib::ID3v2::FrameList l = fileref.ID3v2Tag()->frameListMap()[ "APIC" ];
           if ( !l.isEmpty() )
           {

               //qDebug() << "Found APIC frame(s)" << endl;
               TagLib::ID3v2::Frame *f = l.front();
               TagLib::ID3v2::AttachedPictureFrame *ap = (TagLib::ID3v2::AttachedPictureFrame*)f;

               const TagLib::ByteVector bytes = ap->picture();
               QImage image = QImage::fromData(reinterpret_cast<const uchar*>(bytes.data()), bytes.size());

               if (! image.isNull() )
               {
                   return image;
               } 
           }
       }
       return defaultImage();
}

/*
 *  Create a default cover image: a gray record with diagonal record name as text
 */
QImage Track::defaultImage()
{
    QImage img(120, 120, QImage::Format_RGB888);
    img.fill(QColor(Qt::white));

    QPainter painter(&img);

    painter.setPen(QPen(Qt::lightGray));
    painter.setBrush(Qt::lightGray);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.drawEllipse(QPoint(60,60),45,45);

    painter.setBrush(Qt::white);
    painter.drawEllipse(QPoint(60,60),15,15);

    painter.setPen(QPen(QColor(60,60,60)));
    painter.setFont(QFont("Monospace"));
    painter.translate(QPoint(-26,62));
    painter.rotate(-45);
    painter.drawText( img.rect(), Qt::AlignCenter , p->artist + '\n' + p->title);

    return img;
}

QString Track::prettyTitle() const
{

    QString s = QString::null;
    if( p->artist != QObject::tr("Unknown") )
        s += p->artist + " - ";
    s += p->title;

    return s;
}

QString Track::prettyTitle( QString filename ) //static
{
    QString &s = filename;

    s = s.left( s.indexOf( '.' ) );

    return s;
}

QString Track::prettyArtist( int maxlen ) const
{
    return this->rsqueeze(p->artist,maxlen);
}

QString Track::prettyTitle( int maxlen ) const
{
    return this->rsqueeze(p->title,maxlen);
}

QString Track::rsqueeze( const QString & str, int maxlen ) const
{
   if (str.length() > maxlen) {
    int part = maxlen-3;
    return QString(str.left(part) + "...");
  }
   else return str;
}

void Track::setLengthFromPretty(QString s)
{
    int i=-1;
    if ( s.contains(':') ){
        QStringList list=s.split(':');
        i=list.at(0).toInt()*60;
        i+=list.at(1).toInt();
    }
    p->length = i;
}

QString Track::prettyLength( int seconds )
{
    QString s;

    if( seconds > 0 ) s = prettyTime( seconds, false );
    else if( seconds == -1 ) s = '?';

    return s;
}

QString Track::prettyTime( int seconds, bool showHours )
{
    QString s = QChar( ':' );
    s.append( zeroPad( seconds % 60 ) );
    seconds /= 60;

    if( showHours )
    {
        s.clear();
        s.prepend( zeroPad( seconds % 60 ) );
        s.prepend( ':' );
        seconds /= 60;
    }


    s.prepend( QString::number( seconds ) ); 
    return s;
}


bool Track::isValid()
{
    if ( p->url.isEmpty() || !p->url.isValid() )
        return false;

    if (p->url.toString().contains(".mp3",Qt::CaseInsensitive)
            || p->url.toString().contains(".ogg",Qt::CaseInsensitive)
            || p->url.toString().contains(".wav",Qt::CaseInsensitive)
            || p->url.toString().contains(".m4a",Qt::CaseInsensitive)
            || p->url.toString().contains(".m4p",Qt::CaseInsensitive)
            || p->url.toString().contains(".flac",Qt::CaseInsensitive)
            || p->url.toString().contains(".aiff",Qt::CaseInsensitive)
            || p->url.toString().contains(".oga",Qt::CaseInsensitive)
            || p->url.toString().contains(".wma",Qt::CaseInsensitive)
            || p->url.toString().contains(".au",Qt::CaseInsensitive)
            || p->url.toString().contains(".tta",Qt::CaseInsensitive)
            || p->url.toString().contains(".raw",Qt::CaseInsensitive)
    )
        return true;
    else
        return false;
}

QString Track::dirPath()
{
    QString localPath = p->url.toLocalFile();
    QFileInfo fileInfo(localPath);
    return fileInfo.absolutePath();

}

QStringList Track::tagList(){ return (QStringList() << p->url.toLocalFile()
                                                      << p->artist
                                                      << p->title
                                                      << p->album
                                                      << p->year
                                                      << p->genre
                                                      << p->tracknumber
                                                      << QString().setNum(p->length)
                                                      << QString().setNum(p->counter)
                                                      << QString().setNum(p->rate)); }

int Track::length()     { return p->length > 0 ? p->length : 0; }
QUrl   Track::url()     { return p->url; }
QString Track::title()  { return p->title; }
QString Track::artist() { return p->artist; }
QString Track::album()   { return p->album; }
int Track::rate()   { return p->rate; }
QString Track::year()     { return p->year; }
QString Track::comment()  { return p->comment; }
QString Track::genre()    { return p->genre; }
QString Track::tracknumber() { return p->tracknumber > 0 ? p->tracknumber : "0"; }
int Track::counter() { return p->counter; }
QString Track::prettyLength()  { return prettyLength( p->length ); }
Track::Options Track::flags()  { return p->flags; }

void Track::setUrl(QUrl url) { p->url = url; }
void Track::setTitle(QString s) { p->title = s; }
void Track::setArtist(QString s) { p->artist = s; }
void Track::setAlbum(QString s)  { p->album = s; }
void Track::setRate(int s)  { p->rate = s; }
void Track::setYear(QString s)   { p->year = s; }
void Track::setComment(QString s) { p->comment = s; }
void Track::setGenre(QString s)   { p->genre = s; }
void Track::setTracknumber(QString s) { p->tracknumber = s; }
void Track::setLength(QString s)  { p->length = s.toInt();}
void Track::setCounter(QString s)  { p->counter = s.toInt();}
void Track::setFlags(Track::Options flags)  { p->flags = flags;}
