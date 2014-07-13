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

#ifndef COLLECTIONDB_H
#define COLLECTIONDB_H

#include <qobject.h>
#include <qstringlist.h>
#include <QCustomEvent>
#include <qdir.h>
#include <QtSql>
#include "progressbar.h"

//class sqlite;

class CollectionDB : public QObject
{
    Q_OBJECT
    
    public:
      CollectionDB();
        ~CollectionDB();

        bool isDbValid();
        bool isEmpty();
        
        void incSongCounter( const QString url );
        void updateDirStats( QString path, const long datetime );
        void removeSongsInDir( QString path );
        bool isDirInCollection( QString path );
        void removeDirFromCollection( QString path );
        void setFilterString( QString string );

        bool executeSql(const QString& statement);
        QList<QStringList> selectSql( const QString& statement);
        long selectSqlNumber( const QString& statement );

        int sqlInsertID();
        QString escapeString( QString string );

        ulong getValueID( QString name, QString value, bool autocreate = true, bool useTempTables = false );
        ulong getCount();
        uint getCount(QString path, QString genre, QString artist);
        QPair<int,int> getCount(QStringList paths, QStringList genres, QStringList artists);
        long lastLengthSum();
        uint lastMaxCount();

        QList<QStringList> selectRandomEntry( QString rownum, QString path="", QString genre="", QString artist="");
        QStringList getRandomEntry();
        QStringList getRandomEntry(QString path, QString genre,QString artist);


        void createTables( const bool temporary = false );
        void dropTables( const bool temporary = false );
        void moveTempTables();
        void createStatsTable();
        void dropStatsTable();
        void resetSongCounter();

        void purgeDirCache();
        void scanModifiedDirs( bool recursively );
        void scan( const QStringList& folders, bool recursively );

        QList<QStringList> selectTracks(QString year, QString genre, QString artist, QString album );
        QList<QStringList> selectAlbums(QString year, QString genre, QString artist);
        QList<QStringList> selectArtists(QString year, QString genre);
        QList<QStringList> selectYears();
        QList<QStringList> selectGenres();
        QList<QStringList> selectHotTracks();
        QList<QStringList> selectLastTracks();

    signals:
        void scanDone( bool changed );

    private slots:

    private:
        struct CollectionDbPrivate * p;
        QSqlDatabase db;
        ProgressBar* m_progress;
        bool m_monitor;
        int m_lastInsertId;
};


#endif /* COLLECTIONDB_H */
