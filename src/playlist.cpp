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

#include <qdebug.h>
#include <QMenu>
#include <Qt>

#include <QtXml>
#include <QtGui>
#include <qprogressdialog.h>
#include <qscrollbar.h>

#include "playlist.h"
#include "playlistitem.h"
#include <qfile.h>
#include <QPainter>



Playlist::Playlist(QWidget* parent)
    : QTreeWidget( parent )
    , m_marker( 0 )
    , m_CurrentTrackColor(QColor( 255,100,100 ))
    , m_NextTrackColor(QColor( 200,200,255 ))
    , m_PlaylistMode( Playlist::Playlist_Single )
    , nextPlaylistItem (0)
    , previousPlaylistItem(0)
    , currentPlaylistItem(0)
    , newPlaylistItem(0)
    , m_alternateMax(0)
    , showDropHighlighter(false)
    , autoClearOn(true)
    , m_isPlaying(false)
    , m_isInternDrop(false)
    ,m_dragLocked(false)
{

    setSortingEnabled(false);
    setAcceptDrops( true );
    setDragEnabled( true );
    setAllColumnsShowFocus( false );
    setDropIndicatorShown(true);
    setAcceptDrops(true);
    setDragEnabled(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setAcceptDrops(true);
    setAttribute(Qt::WA_MacShowFocusRect, false);

    QStringList headers;
     headers << tr("Url")<<tr("No")<<tr("Played")<<tr("Artist")<<tr("Title");
     headers <<tr("Album")<<tr("Year")<<tr("Genre")<<tr("Track");
     headers <<tr("Length");

    QTreeWidgetItem *headeritem = new QTreeWidgetItem(headers);
//    headeritem->setTextAlignment(1,Qt::AlignLeft);//No
//    headeritem->setTextAlignment(7,Qt::AlignHCenter);//track
//    headeritem->setTextAlignment(9,Qt::AlignRight);//length
//    headeritem->setTextAlignment(10,Qt::AlignHCenter);//bitrate

    setHeaderItem(headeritem);
    setHeaderLabels(headers);
    setSelectionBehavior(QAbstractItemView::SelectRows);

        header()->setResizeMode(QHeaderView::Interactive);

    header()->hideSection(PlaylistItem::Column_Url);

    // prevent click event if doubleclicked
    ignoreNextRelease = false;
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(emitClicked()));

    timerDragLock = new QTimer(this);
    timerDragLock->setInterval(300);
    connect(timerDragLock, SIGNAL(timeout()), this, SLOT(timeoutDragLock()));

    connect( this,     SIGNAL(itemClicked(QTreeWidgetItem*,int)) ,
             this,       SLOT(slotItemClicked(QTreeWidgetItem*,int)));
    connect( this,     SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)) ,
             this,       SLOT(slotItemDoubleClicked(QTreeWidgetItem*,int)));
    connect( this,     SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)),
             this,       SLOT(slotItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)));

    QSettings settings;
    header()->restoreState( settings.value("playlist_"+objectName()).toByteArray());
}


Playlist::~Playlist()
{
    // save width and sort settings
    QSettings settings;
    settings.setValue("playlist_"+objectName(), header()->saveState());
}


/** Add a song to the playlist */
void Playlist::addTrack( QUrl file, PlaylistItem* after )
{
    Track *track = new Track(file);
    addTrack(track, after);
}

void Playlist::addTrack( Track* track, PlaylistItem* after )
{
    if (!track || !track->isValid())
        return;
    //qDebug() << __PRETTY_FUNCTION__ <<":"<<objectName()<<" url="<<track->url();
    PlaylistItem* item =  new PlaylistItem( this, after );
    item->setTexts( track );
    newPlaylistItem  = item;
    // is it a potential next playlist item?
    //if (after == currentPlaylistItem && currentPlaylistItem && item!=currentPlaylistItem)
    //    setNextPlaylistItem(item);
    //else
        setNormalPlaylistItem(item);
    handleChanges();
}

/** Add a track and set it as current item */
void Playlist::addCurrentTrack( Track* track )
{
    //Add if it comes from outsite only
    bool fromOutsite=true;
    PlaylistItem* currItem = (PlaylistItem*)currentItem();
    if ( currItem )
       if ( track->url() == currItem->track()->url() )
           fromOutsite=false;

    if ( fromOutsite )
        addTrack(track,NULL);
    else
        newPlaylistItem = currItem;

    if (!m_isPlaying){
        //is not playing, direct into player and roll others
        previousPlaylistItem=currentPlaylistItem;
        setCurrentPlaylistItem( newPlaylistItem );
        setNextPlaylistItem( previousPlaylistItem );
    } else {
        //is playing, wait and add as next
        setNextPlaylistItem( newPlaylistItem );
    }
}

void Playlist::addNextTrack( Track* track )
{
    Q_UNUSED(track);
}


void Playlist::appendSong( QString songFileName )
{
    return addTrack( songFileName, (PlaylistItem*)this->lastChild() );
}

void Playlist::appendSong( Track* track )
{
   addTrack( track, lastChild() );
}


void Playlist::appendList(QList<QUrl> list)
{
  appendList(list,(PlaylistItem*)lastChild());
}


void Playlist::appendList( const QList<QUrl> urls, PlaylistItem* after )
{
   qDebug() << __FUNCTION__;

   int droppedUrlCnt = urls.size();
   for(int i = droppedUrlCnt-1; i > -1 ; i--) {
       QString localPath = urls[i].toLocalFile();
       QFileInfo fileInfo(localPath);
       if(fileInfo.isFile()) {
           // file
           addTrack( urls[i], after );
       }
       else
           if(fileInfo.isDir()) {
               // directory
               if(fileInfo.isDir()) {
                   // directory
                   QStringList filters;
                   filters << "*.mp3" << "*.ogg" << "*.wav";

                   QDirIterator it(localPath,
                                   filters,
                                   QDir::Files|QDir::NoSymLinks|QDir::NoDotAndDotDot,
                                   QDirIterator::Subdirectories);
                   while(it.hasNext()) {
                       it.next();
                       addTrack("file:///" + it.filePath(), after );
                   }

               }
           }
           else {
         // none
           }
   }
}

void Playlist::changeTracks( const QList<Track*> tracks )
{
    clear();
    appendTracks( tracks,(PlaylistItem*)lastChild());
}

void Playlist::appendTracks( const QList<Track*> tracks )
{
    appendTracks( tracks,(PlaylistItem*)lastChild());
}

void Playlist::appendTracks( QList<Track*> tracks, PlaylistItem* after )
{
    foreach ( Track* track, tracks) {
        addTrack(new Track(*track),after);
        after = this->newTrack();
    }
}

void Playlist::setPlaylistMode(Mode newMode)
{
  m_PlaylistMode = newMode;


  switch ( m_PlaylistMode )
  {
  case Playlist::Tracklist:
    header()->hideSection( PlaylistItem::Column_No );
    header()->showSection( PlaylistItem::Column_Played );
    header()->showSection( PlaylistItem::Column_Year );
    header()->showSection( PlaylistItem::Column_Genre );
    header()->showSection( PlaylistItem::Column_Tracknumber );
    header()->showSection( PlaylistItem::Column_Album );
    setSortingEnabled(true);
    m_CurrentTrackColor = Qt::white;
    m_NextTrackColor = Qt::white;
    break;
  default :
    header()->showSection( PlaylistItem::Column_No );
    header()->hideSection( PlaylistItem::Column_Played );
    header()->hideSection( PlaylistItem::Column_Year );
    header()->hideSection( PlaylistItem::Column_Genre );
    header()->hideSection( PlaylistItem::Column_Tracknumber );
    header()->hideSection( PlaylistItem::Column_Album );
    setSortingEnabled(false);
    m_CurrentTrackColor = QColor( 255,100,100 );
    m_NextTrackColor = QColor( 200,200,255 );
  }

  handleChanges();
}

/** handle changes after remove or adding tracks to play list */
void Playlist::handleChanges()
{
    if ( m_PlaylistMode==Playlist::Tracklist)
        return;

    //check the need to set current track
    if (!currentPlaylistItem) {
        //current track has been lost, new current will be the next track
        //search for new next track is forced
        //but not for internal move of current track and next track is just new
        if ( nextPlaylistItem && !m_isInternDrop
             && nextPlaylistItem!=newPlaylistItem) {
            setCurrentPlaylistItem( nextPlaylistItem );
            nextPlaylistItem = 0;
        }
        //current track will be the last added track
        else if ( newPlaylistItem )
            setCurrentPlaylistItem( newPlaylistItem );
    }

    //check need to set next track
    if (!nextPlaylistItem  ) {
        if (m_isInternDrop)
            setNextPlaylistItem( newPlaylistItem );
        else
            setNextPlaylistItem( findNextTrack() );
    }

    updateCurrentPlaylistItem();
    updateNextPlaylistItem();

    Q_EMIT countChanged(countTrack());
    Q_EMIT countChanged(allTracks());

    fillNoColumn();
}


void Playlist::setCurrentPlaylistItem( PlaylistItem *item )
{
    //qDebug() << __FUNCTION__ ;
    // repaint "old" CurrentPlaylistItem as normal
    // only if not both are at the same position
    if ( currentPlaylistItem != nextPlaylistItem )
        setNormalPlaylistItem(currentPlaylistItem);

    currentPlaylistItem = item;
    updateCurrentPlaylistItem();

    if (item)
        Q_EMIT currentTrackChanged(currentPlaylistItem->track());
    else
        Q_EMIT currentTrackChanged(NULL);
}

void Playlist::setNormalPlaylistItem( PlaylistItem *item )
{
   if ( item ) {
         item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsDragEnabled|Qt::ItemIsEnabled);
         for(int i = 0; i<=item->columnCount()-1; i++)
              item->setForeground(i,QBrush(Qt::white));
   }
}

void Playlist::setNextPlaylistItem( PlaylistItem *item )
{
    if (currentPlaylistItem==item && item!=0)
        return;

   // repaint "old" NextPlaylistItem as normal
   // only if not both are at the same position
   if ( nextPlaylistItem != currentPlaylistItem )
        setNormalPlaylistItem(nextPlaylistItem);

   nextPlaylistItem  = item;
   updateNextPlaylistItem();

}

void Playlist::updateCurrentPlaylistItem()
{
    //qDebug() << __FUNCTION__ ;
    if (currentPlaylistItem) {

        if (m_isPlaying)
            currentPlaylistItem->setFlags(Qt::ItemIsDragEnabled|Qt::ItemIsEnabled);
        else
            currentPlaylistItem->setFlags(Qt::ItemIsSelectable|Qt::ItemIsDragEnabled|Qt::ItemIsEnabled);

        if (!m_isCurrentList)
            currentPlaylistItem->setForeColor( m_CurrentTrackColor.darker(130));
        else
            currentPlaylistItem->setForeColor( m_CurrentTrackColor);
    }
}

void Playlist::updateNextPlaylistItem()
{
    if (nextPlaylistItem) {

        nextPlaylistItem->setFlags(Qt::ItemIsSelectable|Qt::ItemIsDragEnabled|Qt::ItemIsEnabled);

        if (!m_isCurrentList)
            nextPlaylistItem->setForeColor( m_NextTrackColor.darker(130));
        else
            nextPlaylistItem->setForeColor( m_NextTrackColor);
    }

}

PlaylistItem * Playlist::findNextTrack() const
{

   if ( m_PlaylistMode == Playlist_Single) {
        //Return next Item
       if (  itemBelow(currentPlaylistItem) ) {
            return (PlaylistItem*)itemBelow(currentPlaylistItem);
       } else {
           return NULL;
       }
   }

   if ( countTrack()>0
        && autoClearOn
        && currentPlaylistItem!=this->firstChild()
        && previousPlaylistItem!=this->firstChild()) {
          //Return FirstItem
          return (PlaylistItem*)this->firstChild();
   }

   if ( itemBelow(currentPlaylistItem) ) {
         //Return next Item
          return (PlaylistItem*)itemBelow(currentPlaylistItem);
   }
   else
         if (  itemAbove(currentPlaylistItem)
               && autoClearOn) {
           //Above is still something
          return (PlaylistItem*)itemAbove(currentPlaylistItem);
          } else {
              return NULL;
          }

}

QList<Track*> Playlist::allTracks()
{
    QList<Track*> trackList;
    for(int i=0;i<this->topLevelItemCount();i++){
         PlaylistItem *item = dynamic_cast<PlaylistItem *>(this->topLevelItem(i));
         if ( item->track()->isValid()  )
             trackList.append(item->track());
    }
    return trackList;
}

void Playlist::removePlaylistItem( PlaylistItem *item )
{
   if ( item )  {
     qDebug() << __FUNCTION__ << ":"<<item->track()->url();

     //unset if not available any more
     if ( item == currentPlaylistItem )
         setCurrentPlaylistItem(0);
     if ( item == nextPlaylistItem )
         setNextPlaylistItem(0);
     if ( item == newPlaylistItem )
         newPlaylistItem=0;

     takeTopLevelItem(indexOfTopLevelItem(item));
     delete item;     
   }
}

void Playlist::skipForward()
{
    qDebug() << __PRETTY_FUNCTION__ <<":"<<objectName() ;

    //can skip?
    if ( !nextPlaylistItem && !autoClearOn )
        return;

    //remember current item
    previousPlaylistItem=currentPlaylistItem;

    //skip both items forward
    setCurrentPlaylistItem( nextPlaylistItem );
    setNextPlaylistItem( findNextTrack() );

    //remove previous item at last due to keep playing
    if ( autoClearOn ){
        removePlaylistItem( previousPlaylistItem );
        handleChanges();
    }
}

void Playlist::skipRewind()
{
    if ( itemAbove(currentPlaylistItem) ) {
        setCurrentPlaylistItem( (PlaylistItem*)itemAbove(currentPlaylistItem) );
        setNextPlaylistItem( findNextTrack() );
    }
}


QString Playlist::defaultPlaylistPath() //static
{
    QString pathName = QDesktopServices::storageLocation(QDesktopServices::DataLocation);
    QDir path(pathName);

    if (!path.exists())
        path.mkpath(pathName);

    return  path.absolutePath() + "/" + this->objectName()+".xspf";
}

//ToDo: save as xspf
/*
 *<?xml version="1.0" encoding="UTF-8"?>
<playlist version="1" xmlns="http://xspf.org/ns/0/">
  <trackList>
    <track><location>file:///music/song_1.ogg</location></track>
    <track><location>file:///music/song_2.flac</location></track>
    <track><location>file:///music/song_3.mp3</location></track>
  </trackList>
</playlist>
*/
/*
 *
 * create content as a xspf playlist
 **/

void Playlist::saveXML( const QString &path ) const
{
    qDebug() << __FUNCTION__ << "BEGIN " ;
    QFile file( path );

    if( !file.open(QFile::WriteOnly) ) return;

    QDomDocument newdoc;
    QDomElement playlistElem = newdoc.createElement( "playlist" );
    playlistElem.setAttribute( "version", "1" );
    playlistElem.setAttribute( "xmlns", "http://xspf.org/ns/0/" );
    newdoc.appendChild( playlistElem );

    QDomElement elem = newdoc.createElement( "creator" );
    QDomText t = newdoc.createTextNode( "Knowthelist" );
    elem.appendChild( t );
    playlistElem.appendChild( elem );

    QDomElement listElem = newdoc.createElement( "trackList" );

    for( PlaylistItem *item = firstChild(); item; item = item->nextSibling() )
    {
        if (item)
        {
            QDomElement trackElem = newdoc.createElement("track");

            QDomElement extElem = newdoc.createElement( "extension" );

//            i.setAttribute("url", item->track()->url().toLocalFile());
            if ( currentPlaylistItem )
              if ( item == currentPlaylistItem )
                extElem.setAttribute("current", "1");
            if ( item == nextTrack() )
                extElem.setAttribute("next", "1");


            QStringList tag = item->track()->tagList();

            for( int x = 0; x < tag.count(); ++x )
            {
                if (x==4 ||x==5) {
                    extElem.setAttribute( Track::tagNameList.at(x), tag.at(x) );
                }
                else {
                    QDomElement elem = newdoc.createElement( Track::tagNameList.at(x) );
                    QDomText t = newdoc.createTextNode( tag.at(x) );
                    elem.appendChild( t );
                    trackElem.appendChild( elem );
                }
            }

            trackElem.appendChild( extElem );
            listElem.appendChild( trackElem );
        }
    }

    playlistElem.appendChild( listElem );

    QTextStream stream( &file );
    stream.setCodec( "UTF-8" );
    stream << "<?xml version=\"1.0\" encoding=\"utf8\"?>\n";
    stream << newdoc.toString();
    file.close();
    qDebug() << __FUNCTION__<< "END "  ;
}

//ToDo: load from xspf("spiff")


void Playlist::loadXML( const QString &path )
{
    QFile file( path );

    if( file.open( QFile::ReadOnly ) )
    {
        QTextStream stream( &file );

      stream.setCodec( QTextCodec::codecForName("utf8") );
      QDomDocument d;
      if( !d.setContent(stream.readAll()) ) { qDebug() << "Could not load XML\n"; return; }

      QDomNode n = d.namedItem( "playlist" ).namedItem( "trackList" ).firstChild();

      const QString TRACK( "track" ); //so we don't construct the QStrings all the time
      const QString URL( "url" );
      const QString CURRENT( "current" );
      const QString NEXT( "next" );


      while( !n.isNull() )
      {
           if( n.nodeName() == TRACK ) {
              const QDomElement e = n.toElement();
              if( e.isNull() ) {
                 qDebug() << "Element '" << n.nodeName() << "' is null, skipping.";
                 continue;
              }

              //qDebug() << "Add from xml url='" << e.attribute( URL );
              Track *track = new Track();
              track->setUrl( QUrl::fromLocalFile(e.namedItem("location").firstChild().nodeValue()));
              track->setArtist( e.namedItem("creator").firstChild().nodeValue());
              track->setTitle( e.namedItem("title").firstChild().nodeValue());
              track->setAlbum( e.namedItem("album").firstChild().nodeValue());
              track->setGenre( e.namedItem("extension").toElement().attribute( "genre" ));
              track->setYear( e.namedItem("extension").toElement().attribute( "year" ));
              track->setLength( e.namedItem("duration").firstChild().nodeValue());
              track->setCounter("0");
              appendSong(track);

              if ( e.namedItem("extension").toElement().attribute( CURRENT ) == "1" )
                this->setCurrentPlaylistItem( newPlaylistItem  );
              if ( e.namedItem("extension").toElement().attribute( NEXT ) == "1" )
                this->setNextPlaylistItem( newPlaylistItem  );
            }
          n = n.nextSibling();
      }
    }
    file.close();
    //Q_EMIT countChanged(countTrack());
    //fillNoColumn();
    qDebug() << "End " << __FUNCTION__;
}

void
Playlist::removeSelectedItems()
{

    QListIterator<QTreeWidgetItem *> it(selectedItems());
int z=0;
     while (it.hasNext()){
         PlaylistItem *item = dynamic_cast<PlaylistItem *>(it.next());
         if ( item != currentPlaylistItem || (!m_isPlaying) )
         {
             z++;
             qDebug()<<z;
            removePlaylistItem( item );
         }
    }

    handleChanges();

}

void Playlist::fillNoColumn()
{

  int no = 0;
  int i = 0;

  for (int ii = 0; ii < this->topLevelItemCount(); ii++)
  {
      QTreeWidgetItem *item =  this->topLevelItem ( ii );

    //if this item number is less then then alternateMax increment 2 (alternate) else 1
    //alternateMax is equal to the count of then other player+1
    i++;

    //qDebug() << this->name() << ": markNextTrack(): " << markNextTrack();

    if ( m_PlaylistMode != Playlist::Playlist_Multi )
      no = i;
    else if ( i > m_alternateMax)
      no = i + m_alternateMax;
    else
    {
      if ( m_isCurrentList )
        no = i * 2 - 1;
      else
        no = i * 2;
    }

      //qDebug() << this->name() << ": m_alternateMax: " << m_alternateMax << " i: " << i << " no: " << no;
      item->setText(PlaylistItem::Column_No,QString::number(no));

  }
}


void Playlist::slotItemChanged( QTreeWidgetItem * current, QTreeWidgetItem * previous )
{
    Q_UNUSED(previous);
    if ((selectedItems().count()>1) || (selectedItems().count()==0))
        return;

    PlaylistItem* item = static_cast<PlaylistItem*>(current);

    if (item)
        emit trackChanged(item);
}

void Playlist::slotItemDoubleClicked( QTreeWidgetItem *item, int column )
{
    Q_UNUSED(column);
    emit trackDoubleClicked( static_cast<PlaylistItem*>(item) );
}

void Playlist::slotItemClicked(QTreeWidgetItem *after,int col)
{
    PlaylistItem* item = static_cast<PlaylistItem*>(after);

    if (item)
        emit trackClicked(item);
}

// avoid multiple drops on quick drags
void Playlist::timeoutDragLock()
{
    m_dragLocked=false;
    timerDragLock->stop();
}

// prevent click event if doubleclicked
void Playlist::emitClicked()
{
        emit itemClicked( currentItem(), currentColumn() );
        timer->stop();
}

void Playlist::resizeEvent( QResizeEvent* e )
{
    QTreeWidget::resizeEvent(e);

    double percent = this->size().width()/100.0;

    switch ( m_PlaylistMode )
    {
    case Playlist::Tracklist:

        header()->resizeSection(PlaylistItem::Column_Artist,22*percent);
        header()->resizeSection(PlaylistItem::Column_Title,22*percent);
        header()->resizeSection(PlaylistItem::Column_Album,20*percent);
        header()->resizeSection(PlaylistItem::Column_Length,7*percent);
        header()->resizeSection(PlaylistItem::Column_Genre,10*percent);
        header()->resizeSection(PlaylistItem::Column_Year,8*percent);
        header()->resizeSection(PlaylistItem::Column_Tracknumber,5*percent);
        header()->resizeSection(PlaylistItem::Column_Played,5*percent);

      break;
    default :

      header()->resizeSection(PlaylistItem::Column_No,6*percent);
      header()->resizeSection(PlaylistItem::Column_Artist,40*percent);
      header()->resizeSection(PlaylistItem::Column_Title,40*percent);
      header()->resizeSection(PlaylistItem::Column_Length,10*percent);
    }

}

void Playlist::mouseReleaseEvent(QMouseEvent *event)
{
    if (!ignoreNextRelease) {
        timer->start(QApplication::doubleClickInterval());
        blockSignals(true);
        QTreeWidget::mouseReleaseEvent(event);
        blockSignals(false);
    }
    ignoreNextRelease = false;
}

void Playlist::mouseDoubleClickEvent(QMouseEvent *event)
{
    ignoreNextRelease = true;
    timer->stop();
    QTreeWidget::mouseDoubleClickEvent(event);
}

void Playlist::mousePressEvent( QMouseEvent *e )
{
    if (e->button() == Qt::LeftButton)
        startPos = e->pos();

    QTreeWidget::mousePressEvent(e);

    if (e->button() == Qt::RightButton)
           showContextMenu( dynamic_cast<PlaylistItem *>(currentItem()), currentColumn() );
}

void Playlist::mouseMoveEvent(QMouseEvent *event)
{

    if(event->buttons() & Qt::LeftButton)
        {
            int distance = (event->pos() - startPos).manhattanLength();
            if(distance >= QApplication::startDragDistance() &&
                    !m_dragLocked)
            {
                performDrag();
            }
        }
}

void Playlist::performDrag()
{
    m_dragLocked=true;
    QByteArray itemData;
    QDataStream dataStream(&itemData, QIODevice::WriteOnly);
    QVector<QStringList> tags;
    QPixmap cover;
    int i=0;

    //iterate selected items
    QListIterator<QTreeWidgetItem *> it(selectedItems());
     while (it.hasNext()){
         PlaylistItem *item = dynamic_cast<PlaylistItem *>(it.next());
         if ( (item != currentPlaylistItem || (!m_isPlaying)) && item->track()->isValid()  )
         {
             qDebug() << __PRETTY_FUNCTION__ <<": send Data:"<<item->track()->url();
             QStringList tag = item->track()->tagList();
             tags << tag;
             if (i==0)
                 cover=QPixmap::fromImage( item->track()->coverImage());
             i++;

         }
    }

    dataStream << tags;
     
    QMimeData *mimeData = new QMimeData;
    mimeData->setData("text/playlistitem", itemData);

    QDrag *drag = new QDrag(this);
    drag->setMimeData(mimeData);

    if (!cover.isNull())
        drag->setPixmap(cover.scaled(50,50));

    timerDragLock->start();

    if (drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction) == Qt::MoveAction){
        if ( m_PlaylistMode != Playlist::Tracklist ) {
            //ToDo: before remove, give over current or next Track to newTrack, if within the same playlist
            removeSelectedItems();
        }
    }

}

void Playlist::dragEnterEvent(QDragEnterEvent *event)
{
    if  (m_PlaylistMode == Playlist::Tracklist){
          event->ignore();
          return;
    }
    if (event->mimeData()->hasFormat("text/uri-list") ||
        event->mimeData()->hasFormat("text/playlistitem"))
    {
            event->acceptProposedAction();
    }
}

void Playlist::dropEvent(QDropEvent *event)
{
    m_isInternDrop = false;

    if (event->mimeData()->hasUrls()) {
            QList<QUrl> urlList = event->mimeData()->urls(); // returns list of QUrls
            //ToDo: why ignore?:
            //event->ignore();
            event->accept();
            appendList(urlList,m_marker);

     }
    else if (event->mimeData()->hasFormat("text/playlistitem")) {

        //decode playlistitem
        QByteArray itemData = event->mimeData()->data("text/playlistitem");
        QDataStream stream(&itemData, QIODevice::ReadOnly);
        QVector<QStringList> tags;

        stream >> tags;

        //add Tracks to this playlist
        foreach ( QStringList tag, tags) {
            qDebug() << __PRETTY_FUNCTION__ <<": is playlistitem; tags:"<<tags;
            addTrack(new Track(tag),m_marker);
            m_marker = this->newTrack();
        }

        event->setDropAction(Qt::MoveAction);
        event->accept();

        if (event->source()->objectName()==this->objectName())
            m_isInternDrop=true;
    }
    else
       event->ignore();

    showDropHighlighter=false;
    viewport()->repaint();
}

void Playlist::dragMoveEvent (QDragMoveEvent *event)
{
  dropSite = event->answerRect();
  showDropHighlighter=true;
  viewport()->repaint();
}

void Playlist::dragLeaveEvent (QDragLeaveEvent *event)
{
  Q_UNUSED(event);
  showDropHighlighter=false;
  viewport()->repaint();
}

void Playlist::paintEvent ( QPaintEvent* event )
{
  QTreeView::paintEvent (event);
  if (showDropHighlighter)
  {
      QPainter painter ( viewport() );
      int x, y, w, h;
      dropSite.getRect ( &x, &y, &w, &h );
      QPoint point(x,y);
      QModelIndex modidx = indexAt( point );
      int addHeight=0;

      //Draw drop line after last item
      if (!modidx.isValid())
      {
          modidx =  model()->index(model()->rowCount()-1,0,modidx);
          addHeight=1;
      }
      //bookmark item in case of a drop 
      m_marker = (PlaylistItem*)this->itemFromIndex(modidx);
      if (addHeight==0) {
          if (itemAbove(m_marker)) {
              //one item above
              m_marker = (PlaylistItem*)itemAbove(m_marker);
          }
          else {
              //add new item at first row
              m_marker = 0;
          }
      }

      //draw the drop point hightlighter
      QRect arect = visualRect ( modidx );
      int b = arect.y() + arect.height() * addHeight;
      QBrush brush(Qt::red, Qt::SolidPattern);
      QPen pen;
      pen.setWidth(2);
      pen.setBrush(brush);
      painter.setPen(pen);
      painter.drawLine ( 10, b, width()-10, b);
      painter.drawEllipse(5, b-2,5,5);
      painter.drawEllipse(width()-15, b-2,5,5);
  }
  event->accept();
}


void Playlist::keyPressEvent   (   QKeyEvent* e    )
{
  qDebug() << __FUNCTION__ << "  " << e->key() << "del="<<Qt::Key_Delete;
  if( (e->key() == Qt::Key_Delete) || (e->key() == 0x1000003) ) //also for Mac
        {
             this->removeSelectedItems();
        }
   //else if( e->key() == Qt::Key_Return)
          //Q_EMIT this->clicked( currentItem());
   else if( e->key() == Qt::Key_1)
     Q_EMIT wantLoad( (PlaylistItem*)currentItem(),"Left" );
   else if( e->key() == Qt::Key_2)
     Q_EMIT wantLoad( (PlaylistItem*)currentItem(),"Right" );
   else if( e->key() == Qt::Key_P)
     Q_EMIT trackDoubleClicked( (PlaylistItem*)currentItem() );
   else if( e->key() == Qt::CTRL + Qt::Key_S)
     Q_EMIT wantSearch(QString::null);
   else
      QTreeWidget::keyPressEvent( e );


}


void Playlist::dummySlot(){}

void Playlist::showContextMenu( PlaylistItem *item, int col )
{
    enum Id { LOAD, LOAD_NEXT, VIEW, EDIT, REMOVE, LISTEN, FILTER, LOAD1, LOAD2 };

    if( item == NULL ) return;

    const bool isCurrentPlaylistItem  = (item == currentPlaylistItem);

    QMenu popup( this );

    popup.setTitle( item->track()->prettyTitle( 50 ) );
    if (m_PlaylistMode == Playlist::Tracklist )
    {
        popup.addAction( style()->standardPixmap(QStyle::SP_MediaPlay), tr( "Add to PlayList&1" ),
                         this, SLOT(dummySlot()), Qt::Key_1 );//, LOAD1
        popup.addAction( style()->standardPixmap(QStyle::SP_MediaPlay), tr( "Add to PlayList&2" ),
                         this, SLOT(dummySlot()), Qt::Key_2 );//, LOAD2
    }
    if (!m_isPlaying && !isCurrentPlaylistItem  && m_PlaylistMode != Playlist::Tracklist )
        popup.addAction( style()->standardPixmap(QStyle::SP_MediaPlay), tr( "&Load" ),
                         this, SLOT(dummySlot()), Qt::Key_L );
    if (!isCurrentPlaylistItem  && m_PlaylistMode != Playlist::Tracklist )
        popup.addAction( style()->standardPixmap(QStyle::SP_ArrowRight), tr( "Load as &Next" ),
                         this, SLOT(dummySlot()), Qt::Key_N );
    popup.addSeparator();
    popup.addAction( style()->standardPixmap(QStyle::SP_DriveCDIcon), tr( "&Prelisten Track" ),
                     this, SLOT(dummySlot()), Qt::Key_P );
    popup.addAction( style()->standardPixmap(QStyle::SP_MessageBoxInformation), tr( "&View Tag Information..." ),
                     this, SLOT(dummySlot()),Qt::Key_V );
    popup.addSeparator();
    popup.addAction( style()->standardPixmap(QStyle::SP_ArrowRight), tr( "&Search for: '%1'" ).arg(  item->text(col) ),
                     this, SLOT(dummySlot()),Qt::Key_S );
    popup.addSeparator();
    if (!isCurrentPlaylistItem  && m_PlaylistMode != Playlist::Tracklist )
        popup.addAction(style()->standardPixmap(QStyle::SP_TrashIcon), tr( "&Remove Selected" ), this, SLOT( removeSelectedItems() ), Qt::Key_Delete );

    QAction *a = popup.exec( QCursor::pos());
    if (!a)
        return;
    switch( a->shortcut() )
    {
        case Qt::Key_L:
            nextPlaylistItem  = currentPlaylistItem;
            setCurrentPlaylistItem( item );
            if ( item == nextPlaylistItem  )
                setNextPlaylistItem( findNextTrack() );
            else
                setNextPlaylistItem( nextPlaylistItem  );
            break;

        case Qt::Key_N:
            setNextPlaylistItem( item );
            break;

        case Qt::Key_1:
            Q_EMIT wantLoad(item,"Left" );
            break;

        case Qt::Key_2:
            Q_EMIT wantLoad(item,"Right" );
            break;

        case Qt::Key_P:
            Q_EMIT itemDoubleClicked(item,col);
            break;

        case Qt::Key_S:
            Q_EMIT wantSearch( item->text(col) );
            break;

        case Qt::Key_V:
            showTrackInfo( item->track() );
            break;

        case Qt::Key_F2:
            //rename( item, col );
            break;
        case Qt::Key_Delete:
            item=0;
            break;

    }

}


void Playlist::showTrackInfo( Track* mb )
{
    const QString body = "<tr><td>%1</td><td>%2</td></tr>";

    QString
            str  = "<html><body><table STYLE=\"border-collapse: collapse\"> width=\"100%\" border=\"1\">";
    str += body.arg( tr( "Title" ),      mb->title() );
    str += body.arg( tr( "Artist" ),     mb->artist() );
    str += body.arg( tr( "Album" ),      mb->album() );
    str += body.arg( tr( "Genre" ),      mb->genre() );
    str += body.arg( tr( "Year" ),       mb->year() );
    str += body.arg( tr( "Location" ),   mb->url().toString() );
    str += "</table></body></html>";

    QMessageBox::information( 0, tr( "Meta Information" ),str );
}

