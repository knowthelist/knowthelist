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


#include "playlist.h"
#include "playlistitem.h"

#include <qdebug.h>
#include <QMenu>
#include <Qt>

#include <QtXml>
#include <QtGui>
#include <qprogressdialog.h>
#include <qscrollbar.h>

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
    , autoClearOn(false)
    , m_isPlaying(false)
    , m_isInternDrop(false)
    , m_dragLocked(false)
    , isChangeSignalEnabled(true)
{

    setSortingEnabled( false );
    setAcceptDrops( true );
    setDragEnabled( true );
    setAllColumnsShowFocus( false );
    setDropIndicatorShown(true);
    setAcceptDrops(true);
    setDragEnabled(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragDropMode(QAbstractItemView::InternalMove);
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setUniformRowHeights(true);

    QStringList headers;
     headers << tr("Url")<<tr("No")<<tr("Played")<<tr("Artist")<<tr("Title");
     headers <<tr("Album")<<tr("Year")<<tr("Genre")<<tr("Track");
     headers <<tr("Length")<<tr("Rate");

    QTreeWidgetItem *headeritem = new QTreeWidgetItem(headers);
    setHeaderItem(headeritem);
    setHeaderLabels(headers);

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
    RatingWidget* rating= new RatingWidget();
    rating->setRating( track->rate() * 0.1 );
    QObject::connect(rating,SIGNAL(RatingChanged(float)),SLOT(onRatingChanged(float)));
    setItemWidget(item, PlaylistItem::Column_Rate, rating);
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
        setCurrentPlaylistItem( newPlaylistItem );
    } else {
        //is playing, wait and add as next
        setNextPlaylistItem( newPlaylistItem );
    }

    handleChanges();
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
   checkCurrentItem();
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
   checkCurrentItem();
}

void Playlist::changeTracks( const QList<Track*> tracks )
{
    clear();
    appendTracks( tracks);
}

void Playlist::appendTracks( const QList<Track*> tracks )
{
    // a week attempt to speed up the setItemWidget time issue
    setUpdatesEnabled(false);
    bool doSort = isSortingEnabled();
    setSortingEnabled(false);
    hide();

    appendTracks( tracks,(PlaylistItem*)lastChild());

    setSortingEnabled(doSort);
    setUpdatesEnabled(true);
    show();
}

void Playlist::appendTracks( QList<Track*> tracks, PlaylistItem* after )
{
    foreach ( Track* track, tracks) {
        addTrack(new Track(*track),after);
        after = this->newTrack();
    }
    checkCurrentItem();
}

void Playlist::setPlaylistMode(Mode newMode)
{
  m_PlaylistMode = newMode;

  double percent = this->size().width()/100.0;

  switch ( m_PlaylistMode )
  {
  case Playlist::Tracklist:
    header()->hideSection( PlaylistItem::Column_No );
    header()->showSection( PlaylistItem::Column_Played );
    header()->showSection( PlaylistItem::Column_Year );
    header()->showSection( PlaylistItem::Column_Genre );
    header()->showSection( PlaylistItem::Column_Tracknumber );
    header()->showSection( PlaylistItem::Column_Album );
    header()->showSection( PlaylistItem::Column_Rate );
    header()->resizeSection(PlaylistItem::Column_Artist,22*percent);
    header()->resizeSection(PlaylistItem::Column_Title,22*percent);
    header()->resizeSection(PlaylistItem::Column_Album,20*percent);
    header()->resizeSection(PlaylistItem::Column_Length,7*percent);
    header()->resizeSection(PlaylistItem::Column_Genre,10*percent);
    header()->resizeSection(PlaylistItem::Column_Year,8*percent);
    header()->resizeSection(PlaylistItem::Column_Tracknumber,5*percent);
    header()->resizeSection(PlaylistItem::Column_Played,5*percent);
    header()->resizeSection(PlaylistItem::Column_Rate,75);
    setSortingEnabled(true);
    sortByColumn(PlaylistItem::Column_Played,Qt::DescendingOrder);
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
    header()->hideSection( PlaylistItem::Column_Rate );
    header()->resizeSection(PlaylistItem::Column_No,6*percent);
    header()->resizeSection(PlaylistItem::Column_Artist,40*percent);
    header()->resizeSection(PlaylistItem::Column_Title,40*percent);
    header()->resizeSection(PlaylistItem::Column_Length,10*percent);
    header()->resizeSection(PlaylistItem::Column_Rate,0);
    setSortingEnabled(false);
    m_CurrentTrackColor = QColor( 255,100,100 );
    m_NextTrackColor = QColor( 200,200,255 );
  }

  QSettings settings;
  if ( settings.contains("playlist_"+objectName()) )
    header()->restoreState( settings.value("playlist_"+objectName()).toByteArray());

  handleChanges();
}

void Playlist::checkCurrentItem()
{
    if ( autoClearOn && newPlaylistItem == firstChild() )
        setCurrentPlaylistItem(newPlaylistItem);
    if ( !currentPlaylistItem )
        setCurrentPlaylistItem(firstChild());

    handleChanges();
}

/** handle changes after remove or adding tracks to play list */
void Playlist::handleChanges()
{
    if ( m_PlaylistMode==Playlist::Tracklist)
        return;

      if ( itemBelow(currentPlaylistItem) ) {
            nextPlaylistItem = (PlaylistItem*)itemBelow(currentPlaylistItem);
       } else {
           nextPlaylistItem = NULL;
       }


    updatePlaylistItems();

    Q_EMIT countChanged(countTrack());
    Q_EMIT countChanged(allTracks());

    fillNoColumn();

    isChangeSignalEnabled = true;
}

void Playlist::setCurrentPlaylistItem( PlaylistItem* item )
{
    currentPlaylistItem = item;

    if ( autoClearOn ){
        // Only for the mode, where current item is always on top of the list
        if ( currentPlaylistItem != firstChild() ){
            // Move nextPlaylistItem at first row
            QTreeWidgetItem* child = takeTopLevelItem(indexOfTopLevelItem(item));
            this->insertTopLevelItem(0,child);
            currentPlaylistItem=(PlaylistItem*)child;
        }
    }

    if (currentPlaylistItem)
        Q_EMIT currentTrackChanged(currentPlaylistItem->track());
    else
        Q_EMIT currentTrackChanged(NULL);
}

void Playlist::setNextPlaylistItem( PlaylistItem* item )
{
    if ( autoClearOn ){
        // Move nextPlaylistItem to second row
        // Only for the mode, where current item is always on top of the list
        QTreeWidgetItem* child = takeTopLevelItem(indexOfTopLevelItem(item));
        this->insertTopLevelItem(1,child);
        handleChanges();
    }
}

void Playlist::updatePlaylistItems()
{
    // Set all items to normal
    for( PlaylistItem *item = firstChild(); item; item = item->nextSibling() )
    {
        if (item)
        {
            item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsDragEnabled|Qt::ItemIsEnabled);
            item->setForeColor( Qt::white );
        }
    }


    if (currentPlaylistItem) {

        if (m_isPlaying)
            currentPlaylistItem->setFlags(Qt::ItemIsEnabled);
        else
            currentPlaylistItem->setFlags(Qt::ItemIsSelectable|Qt::ItemIsDragEnabled|Qt::ItemIsEnabled);

        if (!m_isCurrentList)
            currentPlaylistItem->setForeColor( m_CurrentTrackColor.darker(130));
        else
            currentPlaylistItem->setForeColor( m_CurrentTrackColor);
    }

    if (nextPlaylistItem) {

        nextPlaylistItem->setFlags(Qt::ItemIsSelectable|Qt::ItemIsDragEnabled|Qt::ItemIsEnabled);

        if (!m_isCurrentList)
            nextPlaylistItem->setForeColor( m_NextTrackColor.darker(130));
        else
            nextPlaylistItem->setForeColor( m_NextTrackColor);
    }
}

QList<Track*> Playlist::allTracks()
{
    QList<Track*> trackList;
    for(int i=0;i<this->topLevelItemCount();i++){
         if ( PlaylistItem *item = dynamic_cast<PlaylistItem *>(this->topLevelItem(i))){
            if ( item->track()->isValid()  )
              trackList.append(item->track());
         }
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

     newPlaylistItem=0;

     takeTopLevelItem(indexOfTopLevelItem(item));
     delete item;     
   }
}

void Playlist::skipForward()
{
    qDebug() << __PRETTY_FUNCTION__ <<":"<<objectName() ;

    //remove previous item at last due to keep playing
    if ( autoClearOn ){
        previousPlaylistItem = currentPlaylistItem;
        setCurrentPlaylistItem((PlaylistItem*)itemBelow(currentPlaylistItem));
        removePlaylistItem( previousPlaylistItem );
    }
    else
        setCurrentPlaylistItem((PlaylistItem*)itemBelow(currentPlaylistItem));

    handleChanges();
}

void Playlist::skipRewind()
{
    if ( itemAbove(currentPlaylistItem) ) {
        setCurrentPlaylistItem((PlaylistItem*)itemAbove(currentPlaylistItem));
    }
    handleChanges();
}

QString Playlist::defaultPlaylistPath()
{
    QString pathName = QDesktopServices::storageLocation(QDesktopServices::DataLocation);
    QDir path(pathName);

    if (!path.exists())
        path.mkpath(pathName);

    return  path.absolutePath() + "/" + this->objectName()+".xspf";
}

// Export content as a xspf playlist
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
              if ( item->track()->flags().testFlag(Track::isAutoDjSelection ))
                extElem.setAttribute("isAutoDjSelection", "1");

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
              if ( e.namedItem("extension").toElement().attribute( "isAutoDjSelection" ) =="1" )
                track->setFlags( track->flags() | Track::isAutoDjSelection );

              appendSong(track);

              if ( e.namedItem("extension").toElement().attribute( CURRENT ) == "1" )
                this->setCurrentPlaylistItem( newPlaylistItem  );
              if ( e.namedItem("extension").toElement().attribute( NEXT ) == "1" )
                this->setNextPlaylistItem( newPlaylistItem );
            }
          n = n.nextSibling();
      }
    }
    file.close();

    qDebug() << "End " << __FUNCTION__;
}

void Playlist::removeSelectedItems()
{
    if ( m_PlaylistMode==Playlist::Tracklist)
        return;

    QListIterator<QTreeWidgetItem *> it(selectedItems());

     while (it.hasNext()){
         PlaylistItem *item = dynamic_cast<PlaylistItem *>(it.next());
         if ( item != currentPlaylistItem || (!m_isPlaying) )
            removePlaylistItem( item );
    }

    checkCurrentItem();
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


void Playlist::onRatingChanged(float rate)
{
    if(RatingWidget* rateWidget = dynamic_cast<RatingWidget*>(QObject::sender())){

        QModelIndex modidx = indexAt( rateWidget->pos() );
        (PlaylistItem*)this->itemFromIndex(modidx);
        if(PlaylistItem* item = (PlaylistItem*)this->itemFromIndex(modidx)){
            Track* track = item->track();
            if (track){
                track->setRate(rate * 10);
                qDebug() << __FUNCTION__<<item->track()->url();
                emit trackPropertyChanged(track);
            }
        }
    }
}

void Playlist::slotItemChanged( QTreeWidgetItem * current, QTreeWidgetItem * previous )
{
    Q_UNUSED(previous);
    if ((selectedItems().count()>1) || (selectedItems().count()==0))
        return;

    PlaylistItem* item = static_cast<PlaylistItem*>(current);

    if (item && isChangeSignalEnabled )
        emit trackSelected(item->track());
}

void Playlist::slotItemDoubleClicked( QTreeWidgetItem *sender, int column )
{
    Q_UNUSED(column);
    PlaylistItem* item = static_cast<PlaylistItem*>(sender);

    if (item)
        emit trackDoubleClicked( item->track() );
}

void Playlist::slotItemClicked(QTreeWidgetItem *after,int col)
{
    isChangeSignalEnabled = true;

    PlaylistItem* item = static_cast<PlaylistItem*>(after);

    if (item)
        emit trackSelected( item->track() );
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
    if (e->button() == Qt::LeftButton){
        startPos = e->pos();

    }
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
             if (i==0){
                 cover=QPixmap::fromImage( item->track()->coverImage());
                 emit trackSelected(item->track());
             }
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
            isChangeSignalEnabled = false;
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
        checkCurrentItem();

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

      //if is playing mark as next only
      if (isPlaying() && modidx.row()==0){
          modidx =  model()->index(1,1);
      }

      //Draw drop line after last item
      if (!modidx.isValid())
      {
          modidx =  model()->index(model()->rowCount()-1,1,modidx);
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

     // qDebug() << __PRETTY_FUNCTION__ <<": modidx:"<<modidx;

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

  PlaylistItem* item = static_cast<PlaylistItem*>(currentItem());

  if( (e->key() == Qt::Key_Delete) || (e->key() == 0x1000003) ) //also for Mac
        {
             this->removeSelectedItems();
        }
   else if( e->key() == Qt::Key_L){
      setCurrentPlaylistItem( item );
      handleChanges();
    }
   else if( e->key() == Qt::Key_N){
      setNextPlaylistItem( item );
    }
   else if( e->key() == Qt::Key_1)
     Q_EMIT wantLoad( item->track(),"Left" );
   else if( e->key() == Qt::Key_2)
     Q_EMIT wantLoad( item->track(),"Right" );
   else if( e->key() == Qt::Key_P)
     Q_EMIT trackDoubleClicked( item->track() );
   else if( e->key() == Qt::CTRL + Qt::Key_S)
     Q_EMIT wantSearch(QString::null);
   else
      QTreeWidget::keyPressEvent( e );


}

// needed for showContextMenu actions
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
    popup.addSeparator();
    popup.addAction( style()->standardPixmap(QStyle::SP_ArrowRight), tr( "&Search for: '%1'" ).arg(  item->text(col) ),
                     this, SLOT(dummySlot()),Qt::Key_S );
    popup.addSeparator();
    if (!isCurrentPlaylistItem  && m_PlaylistMode != Playlist::Tracklist )
        popup.addAction(style()->standardPixmap(QStyle::SP_TrashIcon), tr( "&Remove Selected" ), this, SLOT( removeSelectedItems() ), Qt::Key_Delete );
    popup.addSeparator();
    popup.addAction( style()->standardPixmap(QStyle::SP_DirOpenIcon), tr( "&Open File Location" ),
                     this, SLOT(dummySlot()),Qt::Key_O );
    popup.addAction( style()->standardPixmap(QStyle::SP_MessageBoxInformation), tr( "&View Tag Information" ),
                     this, SLOT(dummySlot()),Qt::Key_V );

    QAction *a = popup.exec( QCursor::pos());
    if (!a)
        return;
    switch( a->shortcut() )
    {
        case Qt::Key_L:
            setCurrentPlaylistItem( item );
            handleChanges();
            break;

        case Qt::Key_N:
            setNextPlaylistItem( item );
            break;

        case Qt::Key_1:
            Q_EMIT wantLoad(item->track(),"Left" );
            break;

        case Qt::Key_2:
            Q_EMIT wantLoad(item->track(),"Right" );
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

        case Qt::Key_O:
            if ( item->track())
                QDesktopServices::openUrl( QUrl(QString("file://%1").arg(item->track()->dirPath())));
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

