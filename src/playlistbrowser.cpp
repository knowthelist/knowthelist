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


#include "playlistbrowser.h"
#include "playlistwidget.h"
#include "collectiondb.h"
#include "track.h"

#include <QtXml>
#include <Qt>

class Track;

class PlaylistBrowsertPrivate
{
    public:
    QListWidget* listPlaylists;
    PlaylistWidget* currentPlaylist;
    CollectionDB* database;
    QString directory;

};


PlaylistBrowser::PlaylistBrowser(QWidget *parent) :
    QWidget(parent)
{
    p = new PlaylistBrowsertPrivate;

    QPushButton* pushSave =new QPushButton();
    pushSave->setGeometry(QRect(1,1,60,25));
    pushSave->setMaximumWidth(60);
    pushSave->setMinimumWidth(60);
    pushSave->setText("+");
    QFont pushFont = pushSave->font();
    pushFont.setBold(true);
    pushFont.setPointSize(pushFont.pointSize()+4);
    pushSave->setFont(pushFont);

    pushSave->setStyleSheet("QPushButton { border: none; padding-top: -3px; margin-left: 8px;max-height: 20px; margin-right: 28px;}");
    pushSave->setToolTip(tr( "Save loaded player lists into a file" ));
    connect( pushSave,SIGNAL(clicked()),this, SLOT(onPushSave()));

    QVBoxLayout *mainLayout = new QVBoxLayout;
    QWidget *headWidget = new QWidget(this);
    headWidget->setMaximumHeight(35);
    headWidget->setMinimumHeight(25);

    QHBoxLayout *headWidgetLayout = new QHBoxLayout;
    headWidgetLayout->setMargin(0);
    headWidgetLayout->setSpacing(1);
    headWidgetLayout->setAlignment(Qt::AlignRight);

    headWidgetLayout->addWidget(pushSave);
    headWidget->setLayout(headWidgetLayout);

    p->listPlaylists = new QListWidget();
    p->listPlaylists->setAttribute(Qt::WA_MacShowFocusRect, false);

    p->database  = new CollectionDB();
    p->database->executeSql( "PRAGMA synchronous = OFF;" );

    updateLists();

    headWidget->raise();
    mainLayout->addWidget(headWidget);
    mainLayout->addWidget(p->listPlaylists);

    mainLayout->setMargin(0);
    mainLayout->setSpacing(0);

    setMaximumWidth(400);

    p->directory = "";

    setLayout(mainLayout);

}

PlaylistBrowser::~PlaylistBrowser()
{
    QSettings settings;
    //settings.setValue("",p->);
    delete p;
}

void PlaylistBrowser::updateLists()
{
    // Read config values
    QSettings settings;
    p->directory = settings.value("editPlaylistRoot","").toString();

    // Insert dynamic lists
    PlaylistWidget* list;
    QListWidgetItem* itm;
    p->listPlaylists->clear();

    //Top Tracks List
    list = new PlaylistWidget(p->listPlaylists);
    list->setName(tr("Top Tracks"));
    list->setObjectName("TopTracks");
    list->setDescription(tr("Most played tracks"));
    list->setRemovable(false);
    connect(list,SIGNAL(activated()),this,SLOT(loadDatabaseList()));
    connect(list,SIGNAL(started()),this,SLOT(playDatabaseList()));

    itm = new QListWidgetItem(p->listPlaylists);

    itm->setSizeHint(QSize(0,70));
    p->listPlaylists->addItem(itm);
    p->listPlaylists->setItemWidget(itm,list);

    //Last Tracks List
    list = new PlaylistWidget(p->listPlaylists);
    list->setName(tr("Last Tracks"));
    list->setObjectName("LastTracks");
    list->setDescription(tr("Recently played tracks"));
    list->setRemovable(false);
    connect(list,SIGNAL(activated()),this,SLOT(loadDatabaseList()));
    connect(list,SIGNAL(started()),this,SLOT(playDatabaseList()));

    itm = new QListWidgetItem(p->listPlaylists);

    itm->setSizeHint(QSize(0,70));
    p->listPlaylists->addItem(itm);
    p->listPlaylists->setItemWidget(itm,list);

    //Favorites Tracks List
    list = new PlaylistWidget(p->listPlaylists);
    list->setName(tr("Favorites Tracks"));
    list->setObjectName("FavoritesTracks");
    list->setDescription(tr("Favorites tracks"));
    list->setRemovable(false);
    connect(list,SIGNAL(activated()),this,SLOT(loadDatabaseList()));
    connect(list,SIGNAL(started()),this,SLOT(playDatabaseList()));

    itm = new QListWidgetItem(p->listPlaylists);

    itm->setSizeHint(QSize(0,70));
    p->listPlaylists->addItem(itm);
    p->listPlaylists->setItemWidget(itm,list);

    // read stored lists
    QList<QStringList> listData = p->database->selectPlaylistData();
    foreach ( QStringList data, listData) {
        QString name = data[0];
        int count = data[1].toInt();
        int sum = data[2].toInt();
        QDateTime date(QDateTime::fromTime_t( data[3].toInt() ));

        qDebug() << __PRETTY_FUNCTION__ << "add playlist: " << name;
        list = new PlaylistWidget(p->listPlaylists);
        list->setName( name );
        list->setObjectName( name );
        list->setDescription( date.toString("yyyy-MM-dd") + "    "
                              + QString::number(count) + " " + tr("tracks") + "    "
                              + Track::prettyTime( sum ,true) + " " + tr("hours"));
        connect(list,SIGNAL(activated()),this,SLOT(loadDatabaseList()));
        connect(list,SIGNAL(started()),this,SLOT(playDatabaseList()));
        connect(list,SIGNAL(deleted()),this,SLOT(removeDatabaseList()));

        itm = new QListWidgetItem(p->listPlaylists);

        itm->setSizeHint(QSize(0,70));
        p->listPlaylists->addItem(itm);
        p->listPlaylists->setItemWidget(itm,list);
    }

    //// read saved lists
    //QDir rDir( p->directory );
    //rDir.setFilter(QDir::Files | QDir::NoDotDot | QDir::NoDot | QDir::Readable);
    //QStringList filters;
    //     filters << "*.xspf";
    //rDir.setNameFilters(filters);
    //QFileInfoList filelist = rDir.entryInfoList();

    //Q_FOREACH (const QFileInfo fi, filelist) {
    //        if ( fi.isFile() ) {
    //            qDebug() << __PRETTY_FUNCTION__ << "add playlist: " << fi.fileName();
    //            list = new PlaylistWidget(p->listPlaylists);
    //            list->setName(fi.fileName().replace(".xspf",""));
    //            list->setObjectName(fi.fileName());

    //            QPair<int,int> count = readFileValues( p->directory+"/"+fi.fileName());
    //            list->setDescription( fi.lastModified().toString("yyyy-MM-dd") + "    "
    //                                  + QString::number(count.first) + " " + tr("tracks") + "    "
    //                                  + Track::prettyTime( count.second ,true) + " " + tr("hours"));
    //            connect(list,SIGNAL(activated()),this,SLOT(loadFileList()));
    //            connect(list,SIGNAL(started()),this,SLOT(playFileList()));
    //            connect(list,SIGNAL(deleted()),this,SLOT(removeFileList()));

    //            itm = new QListWidgetItem(p->listPlaylists);

    //            itm->setSizeHint(QSize(0,70));
    //            p->listPlaylists->addItem(itm);
    //            p->listPlaylists->setItemWidget(itm,list);
    //        }

    //}


}

void PlaylistBrowser::playDatabaseList()
{
    qDebug() << __PRETTY_FUNCTION__ ;

    if(PlaylistWidget* item = qobject_cast<PlaylistWidget*>(QObject::sender())){

        onSelectionChanged(item);

        emit selectionStarted(selectedTracks());
    }
}

void PlaylistBrowser::loadDatabaseList()
{
    qDebug() << __PRETTY_FUNCTION__ ;

    if(PlaylistWidget* item = qobject_cast<PlaylistWidget*>(QObject::sender())){
        onSelectionChanged(item);

        emit selectionChanged(selectedTracks());
    }
}


void PlaylistBrowser::removeDatabaseList()
{
    qDebug() << __PRETTY_FUNCTION__ ;

    if(PlaylistWidget* item = qobject_cast<PlaylistWidget*>(QObject::sender())){

        QString senderName = item->objectName();

        p->database->removePlaylist(senderName);
        updateLists();
    }
}

QList<Track*> PlaylistBrowser::selectedTracks()
{
    QString senderName = p->currentPlaylist->objectName();
    QList<QStringList> selectedTags;

    //Retrieve songs from database
    if (senderName == "TopTracks")
        selectedTags = p->database->selectHotTracks();
    else if (senderName == "FavoritesTracks")
        selectedTags = p->database->selectFavoritesTracks();
    else if (senderName == "LastTracks")
        selectedTags = p->database->selectLastTracks();
    else
        selectedTags = p->database->selectPlaylistTracks(senderName);

    QList<Track*> tracks;

    tracks.clear();

    qDebug() << __FUNCTION__ << "Song count: " << selectedTags.count();

    //add tags to this track list
    foreach ( QStringList tag, selectedTags) {
        tracks.append( new Track(tag));
    }

    return tracks ;
}

void PlaylistBrowser::onSelectionChanged(PlaylistWidget* item)
{
    p->currentPlaylist = item;
    for (int d=0;d<p->listPlaylists->count();d++)
        ((PlaylistWidget*)p->listPlaylists->itemWidget(p->listPlaylists->item(d)))->deactivate();

    item->activate();
}

void PlaylistBrowser::onPushSave()
{

//    QFileDialog dialog(this);
//    dialog.setDefaultSuffix("xspf");
//    QString fileName = dialog.getSaveFileName(this,
//             tr("Save Play List"), p->directory,
//             tr("Playlists (*.xspf);;All Files (*)"));

//    // Bad workaround for Linux (Mac and Windows work with defaultSuffix)
//    if (!fileName.endsWith(".xspf"))
//        fileName += ".xspf";

//    emit savePlaylists(fileName);

    QString listName = QInputDialog::getText(this,tr("Save Play List"),tr("Ente name of the new List"));
    if ( !listName.isEmpty() )
        emit storePlaylists(listName);
}

// obsolate: save list into files

QList<Track*> PlaylistBrowser::readFileList(QString filename)
{
    QFile file( filename );
    QList<Track*> tracks;

    tracks.clear();

    if( file.open( QFile::ReadOnly ) )
    {
        QTextStream stream( &file );

      stream.setCodec( QTextCodec::codecForName("utf8") );
      QDomDocument d;
      if( !d.setContent(stream.readAll()) ) { qDebug() << "Could not load XML\n"; return tracks; }

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
              track->setTracknumber( e.namedItem("trackNum").firstChild().nodeValue());
              track->setGenre( e.namedItem("extension").toElement().attribute( "genre" ));
              track->setYear( e.namedItem("extension").toElement().attribute( "year" ));
              track->setLength( e.namedItem("duration").firstChild().nodeValue());
              track->setCounter("0");
              if ( e.namedItem("extension").toElement().attribute( "isAutoDjSelection" ) =="1" )
                track->setFlags( track->flags() | Track::isAutoDjSelection );
              if ( e.namedItem("extension").toElement().attribute( "isOnFirstPlayer" ) =="1" )
                track->setFlags( track->flags() | Track::isOnFirstPlayer );
              if ( e.namedItem("extension").toElement().attribute( "isOnSecondPlayer" ) =="1" )
                track->setFlags( track->flags() | Track::isOnSecondPlayer );
              track->setRate( e.namedItem("extension").toElement().attribute("Rating").toInt() );

              tracks.append(track);

            }
          n = n.nextSibling();
      }
    }
    file.close();

    qDebug() << "End " << __FUNCTION__;

    return tracks;
}

QPair<int,int> PlaylistBrowser::readFileValues(QString filename)
{
    QFile file( filename );
    QPair<int,int> pair;

    int duration=0;
    int count=0;

    if( file.open( QFile::ReadOnly ) )
    {
      QTextStream stream( &file );

      stream.setCodec( QTextCodec::codecForName("utf8") );
      QDomDocument d;
      if( !d.setContent(stream.readAll()) ) { qDebug() << "Could not load XML\n"; return pair; }

      QDomNode n = d.namedItem( "playlist" ).namedItem( "trackList" ).firstChild();

      const QString TRACK( "track" );


      while( !n.isNull() )
      {
           if( n.nodeName() == TRACK ) {
              const QDomElement e = n.toElement();
              if( e.isNull() ) {
                 qDebug() << "Element '" << n.nodeName() << "' is null, skipping.";
                 continue;
              }

              duration+= e.namedItem("duration").firstChild().nodeValue().toInt();
              count++;

            }
          n = n.nextSibling();
      }
    }
    file.close();

    pair.first = count;
    pair.second = duration;

    qDebug() << "End " << __FUNCTION__;

    return pair;
}


void PlaylistBrowser::playFileList()
{
    qDebug() << __PRETTY_FUNCTION__ ;

    if(PlaylistWidget* item = qobject_cast<PlaylistWidget*>(QObject::sender())){

        onSelectionChanged(item);

        QString senderName = item->objectName();

        emit selectionStarted( readFileList( p->directory+"/"+senderName) );
    }
}

void PlaylistBrowser::loadFileList()
{
    qDebug() << __PRETTY_FUNCTION__ ;

    if(PlaylistWidget* item = qobject_cast<PlaylistWidget*>(QObject::sender())){

        onSelectionChanged(item);

        QString senderName = item->objectName();

        //Retrieve songs from file
        emit selectionChanged( readFileList( p->directory+"/"+senderName) );
    }
}

void PlaylistBrowser::removeFileList()
{
    qDebug() << __PRETTY_FUNCTION__ ;

    if(PlaylistWidget* item = qobject_cast<PlaylistWidget*>(QObject::sender())){

        QString senderName = item->objectName();

        QFile::remove( p->directory+"/"+senderName );
        updateLists();
    }
}
