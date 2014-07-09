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

#include <QTimerEvent>
#include <QPixmap>
#include <QDropEvent>
#include <Qt>
#include <QAction>
#include <QMenuBar>

class Track;

class PlaylistBrowsertPrivate
{
    public:
    QListWidget *listPlaylists;
    CollectionDB* database;
    QList<QStringList> selectedTags;

};


PlaylistBrowser::PlaylistBrowser(QWidget *parent) :
    QWidget(parent)
{
    p = new PlaylistBrowsertPrivate;

//    QPushButton *pushRandom =new QPushButton();
//    QPixmap pixmap2(":shuffle.png");
//    pushRandom->setIcon(QIcon(pixmap2));
//    pushRandom->setIconSize(QSize(32,32));
//    pushRandom->setStyleSheet("QPushButton { border: none; padding: 0px; margin-left: 8px;max-height: 20px; margin-right: 8px;}");
//    pushRandom->setToolTip(tr( "Random Tracks" ));

    QVBoxLayout *mainLayout = new QVBoxLayout;
    QWidget *headWidget = new QWidget(this);
    headWidget->setMaximumHeight(38);

    QHBoxLayout *headWidgetLayout = new QHBoxLayout;
    headWidgetLayout->setMargin(0);
    headWidgetLayout->setSpacing(1);

//    headWidgetLayout->addWidget(pushRandom);
    headWidget->setLayout(headWidgetLayout);

    p->listPlaylists = new QListWidget();

    p->database  = new CollectionDB();
    p->database->executeSql( "PRAGMA synchronous = OFF;" );

    fillList();

    headWidget->raise();
    mainLayout->addWidget(headWidget);
    mainLayout->addWidget(p->listPlaylists);

    mainLayout->setMargin(0);
    mainLayout->setSpacing(0);

    //setFocusProxy( p->collectiontree ); //default object to get focus
    setMaximumWidth(400);

    // Read config values
//    QSettings settings;

    setLayout(mainLayout);

}

PlaylistBrowser::~PlaylistBrowser()
{
    QSettings settings;
    //settings.setValue("TreeMode",p->collectiontree->treeMode);
    delete p;
}

void PlaylistBrowser::fillList()
{
    // Insert dynamic lists
    PlaylistWidget* list;
    QListWidgetItem* itm;

    list = new PlaylistWidget(p->listPlaylists);
    list->setName(tr("Top Tracks"));
    list->setObjectName("TopTracks");
    list->setDescription(tr("Most played tracks"));
    connect(list,SIGNAL(activated()),this,SLOT(loadPlaylist()));
    connect(list,SIGNAL(started()),this,SLOT(playPlaylist()));

    itm = new QListWidgetItem(p->listPlaylists);

    itm->setSizeHint(QSize(0,75));
    p->listPlaylists->addItem(itm);
    p->listPlaylists->setItemWidget(itm,list);

    list = new PlaylistWidget(p->listPlaylists);
    list->setName(tr("Last Tracks"));
    list->setObjectName("LastTracks");
    list->setDescription(tr("Recently played tracks"));
    connect(list,SIGNAL(activated()),this,SLOT(loadPlaylist()));
    connect(list,SIGNAL(started()),this,SLOT(playPlaylist()));

    itm = new QListWidgetItem(p->listPlaylists);

    itm->setSizeHint(QSize(0,75));
    p->listPlaylists->addItem(itm);
    p->listPlaylists->setItemWidget(itm,list);
}

void PlaylistBrowser::playPlaylist()
{
    qDebug() << __PRETTY_FUNCTION__ ;

    if(PlaylistWidget* item = qobject_cast<PlaylistWidget*>(QObject::sender())){
        onSelectionChanged(item);

        //Retrieve songs from database
        p->selectedTags = p->database->selectHotTracks();

        emit selectionStarted(selectedTracks());
    }
}

void PlaylistBrowser::loadPlaylist()
{
    qDebug() << __PRETTY_FUNCTION__ ;

    if(PlaylistWidget* item = qobject_cast<PlaylistWidget*>(QObject::sender())){

        onSelectionChanged(item);

        QString senderName = item->objectName();

        //Retrieve songs from database
        if (senderName == "TopTracks")
            p->selectedTags = p->database->selectHotTracks();
        else
            p->selectedTags = p->database->selectLastTracks();

        //ToDo: Add some more

        emit selectionChanged(selectedTracks());
    }
}

QList<Track*> PlaylistBrowser::selectedTracks()
{
    QList<Track*> tracks;

    tracks.clear();

    qDebug() << __FUNCTION__ << "Song count: " << p->selectedTags.count();

    //add tags to this track list
    foreach ( QStringList tag, p->selectedTags) {
        tracks.append( new Track(tag));
    }

    return tracks ;
}

void PlaylistBrowser::onSelectionChanged(PlaylistWidget* item)
{

    for (int d=0;d<p->listPlaylists->count();d++)
        ((PlaylistWidget*)p->listPlaylists->itemWidget(p->listPlaylists->item(d)))->deactivate();

    item->activate();
}
