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

#include "collectionupdater.h"
#include "collectiondb.h"

#if QT_VERSION >= 0x050000
 #include <QtConcurrent/QtConcurrent>
#else
 #include <QtConcurrentRun>
#endif


class CollectionUpdaterPrivate
{
    public:
        bool doMonitor;
        QStringList dirs;
        bool isStoped;
        QPoint startPos;
        bool openContext;
        bool dragLocked;
        QTimer *timer;
        bool incremental;
        CollectionDB *collectionDB;
};

CollectionUpdater::CollectionUpdater()
{
    p = new CollectionUpdaterPrivate;

    QSettings settings;

        p->doMonitor = settings.value("Monitor").toBool();
        p->dirs=settings.value("Dirs").toStringList();

        p->collectionDB = new CollectionDB();
        if ( !p->collectionDB )
            qWarning() << Q_FUNC_INFO << "Could not open SQLite database\n";

        //optimization for speeding up SQLite
        p->collectionDB->executeSql( "PRAGMA synchronous = OFF;" );

        if ( !p->collectionDB->isDbValid() )
        {
            qDebug() << "Rebuilding database!" << endl;
            p->collectionDB->dropTables();
            p->collectionDB->createTables();
            p->collectionDB->dropStatsTable();
            p->collectionDB->createStatsTable();
            scan();
        }

        p->timer = new QTimer( this );
        p->timer->setInterval(600000); //1000 * 60 * 10 = 10min
        connect(p->timer,SIGNAL(timeout()),this,SLOT(monitor()));
        if ( p->doMonitor)
           p->timer->start();
}


CollectionUpdater::~CollectionUpdater()
{
   delete p;
}

void CollectionUpdater::setDoMonitor(bool value)
{
    p->doMonitor = value;
    if ( p->doMonitor)
       p->timer->start();
    else
       p->timer->stop();
}

void CollectionUpdater::stop()
{
    p->isStoped = true;
}

void CollectionUpdater::setDirectoryList(QStringList dirs, bool force)
{
    if ( p->dirs != dirs || force ){
         p->dirs = dirs;
         scan();
    }
}


void CollectionUpdater::monitor()
{
    qDebug() << Q_FUNC_INFO;

    p->incremental = true;
    p->isStoped = false;

    QStringList folders;

            QList<QStringList> entries = p->collectionDB->selectSql( "SELECT dir, changedate FROM directories;" );

            foreach ( QStringList entry, entries) {
                QString dir(entry[0]);
                QString changedate(entry[1]);
                QFileInfo fi(dir);

                if ( fi.exists() )
                {
                    if ( QString::number(fi.lastModified().toTime_t()) != changedate )
                    {
                        folders << dir;
                        qDebug() << "Collection dir changed: " << dir;
                    }
                }
                else
                {
                    // this folder has been removed
                    folders << dir;
                    qDebug() << "Collection dir removed: " << dir;
                }
            }

            if ( !folders.isEmpty() )
                QFuture<void> future = QtConcurrent::run( this, &CollectionUpdater::asynchronScan, folders);
}

void CollectionUpdater::scan()
{
    p->incremental = false;
    p->isStoped = false;
    QFuture<void> future = QtConcurrent::run( this, &CollectionUpdater::asynchronScan, p->dirs);
}

void CollectionUpdater::asynchronScan(QStringList dirs)
{
    qDebug() << Q_FUNC_INFO << dirs.count() << "dirs" << endl;

    // avoid multiple runs
    p->timer->stop();

    Q_EMIT progressChanged(1);

    if ( !p->incremental )
        p->collectionDB->purgeDirCache();

    QStringList entries;
    int dirCount = dirs.count();

    //iterate over all folders
    for ( int i = 0; i < dirCount; i++ ) {
        Q_EMIT progressChanged(((i+1)*10)/dirCount);
        readDir( dirs[ i ], entries );
    }

    if ( !entries.empty() ) {
        Q_EMIT progressChanged(10);
        readTags( entries );
    }
    Q_EMIT progressChanged(100);

    if (!entries.empty())
        Q_EMIT changesDone();

    // Re-start timer to monitor dirs
    if ( p->doMonitor)
       p->timer->start();
}

void CollectionUpdater::readDir( const QString& dir, QStringList& entries )
{
    //update dir statistics for rescanning purposes
    QFileInfo fi( dir );

    if ( fi.exists() )
        p->collectionDB->updateDirStats( dir, ( long ) fi.lastModified().toTime_t() );
    else
    {
        if ( p->incremental )
        {
            p->collectionDB->removeSongsInDir( dir );
            p->collectionDB->removeDirFromCollection( dir );
        }
        return;
    }


    QDir rDir( dir );
    rDir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotDot | QDir::NoDot | QDir::NoSymLinks | QDir::Readable);
    QFileInfoList list = rDir.entryInfoList();

    Q_FOREACH (const QFileInfo fi, list) {
            if ( fi.isDir() ) {
                if ( !p->incremental || !p->collectionDB->isDirInCollection( fi.absoluteFilePath() ) )
                    readDir( fi.absoluteFilePath(), entries );
            } else if ( fi.isFile() )
                entries << fi.absoluteFilePath();

    }
}

void CollectionUpdater::readTags( const QStringList& entries )
{
    qDebug() << Q_FUNC_INFO << " Start";

    QUrl url;
    p->collectionDB->createTables( true );

    int entriesCount = entries.count();
    for ( int i = 0; i < entriesCount; i++ ) {
        if ( !( i % 20 ) )
            Q_EMIT progressChanged(((i*90)/entriesCount)+10);

        url = QUrl::fromLocalFile( entries[ i ] );

        Track track( url );


         if ( track.isValid() ) {

            QString command = QString("INSERT INTO tags_temp "
                              "( url, dir, artist, title, album, genre, year, length, track ) "
                              "VALUES('%1','%2',%3,'%4',%5,%6,%7,%8,%9);")
                    .arg(p->collectionDB->escapeString( track.url().toLocalFile() ))
                    .arg(p->collectionDB->escapeString( track.dirPath() ))
                    .arg(p->collectionDB->escapeString( QString::number( p->collectionDB->getValueID( "artist", track.artist(), true, !p->incremental ) ) ))
                    .arg(p->collectionDB->escapeString( track.title() ))
                    .arg(p->collectionDB->escapeString( QString::number( p->collectionDB->getValueID( "album", track.album(), true, !p->incremental ) )) )
                    .arg(p->collectionDB->escapeString( QString::number( p->collectionDB->getValueID( "genre", track.genre(), true, !p->incremental ) ) ))
                    .arg(p->collectionDB->escapeString( QString::number( p->collectionDB->getValueID( "year", track.year(), true, !p->incremental ) ) ))
                    .arg(p->collectionDB->escapeString( QString::number( track.length() )))
                    .arg(p->collectionDB->escapeString( track.tracknumber() ));


            p->collectionDB->executeSql( command );

            //stop the process?
            if ( p->isStoped ) i = entries.count();
          }
    }

    qDebug() << Q_FUNC_INFO << " Insert finish";

    //update database only if not stoped
    if ( !p->isStoped )
    {
      // let's lock the database (will block other threads)
      p->collectionDB->executeSql( "BEGIN TRANSACTION;" );

      // remove tables and recreate them (quicker than DELETE FROM)
      if ( !p->incremental ) {
          p->collectionDB->dropTables();
          p->collectionDB->createTables();
      } else {
          // remove old entries from database, only
          for ( int i = 0; i < p->dirs.count(); i++ )
              p->collectionDB->removeSongsInDir( p->dirs[ i ] );
      }

      // rename tables
      p->collectionDB->moveTempTables();

      // remove temp tables and unlock database
      p->collectionDB->dropTables( true );
      p->collectionDB->executeSql( "END TRANSACTION;" );
    }
    else
    {

      qDebug() << Q_FUNC_INFO << " Stop";
    }

    qDebug() << Q_FUNC_INFO << " End";
}







