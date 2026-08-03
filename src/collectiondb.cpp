/*
    Copyright (c) 2004 Mark Kretschmann <markey@web.de>
    Copyright (c) 2004 Christian Muehlhaeuser <chris@chris.de>
    Copyright (C) 2005-2026 Mario Stephan <mstephan@shared-files.de>

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

#include "collectiondb.h"

#include <QtSql>

#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QMutex>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <qimage.h>

struct CollectionDbPrivate {
public:
    uint genreCount;
    QString lastPath;
    QString lastArtist;
    QString lastGenre;
    QString lastFilterString;
    QString filterString;
    ulong resultCount;
    ulong resultLength;
    QString sqlQuickFilter;
    QString sqlFromString;
    QString sqlFromStringPL;
    QMutex mutex;

    QString selectionFilter(QString year = "", QString genre = "", QString artist = "", QString album = "")
    {
        QString ret = "";
        if (!year.isEmpty())
            ret += "AND year.name = '" + year.replace("'", "''") + "' ";
        if (!genre.isEmpty())
            ret += "AND genre.name = '" + genre.replace("'", "''") + "' ";
        if (!artist.isEmpty())
            ret += "AND artist.name = '" + artist.replace("'", "''") + "' ";
        if (!album.isEmpty())
            ret += "AND album.name = '" + album.replace("'", "''") + "' ";

        // Handle case-insensitive search - add this to keep the existing behavior
        if (ret.isEmpty())
            ret += " 1=1) ";
        else
            ret += ") ";

        return ret;
    }
};

static constexpr const char* kDbConnName = "CollectionDB";

static QString collectionDbPath()
{
    const QString pathName = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).at(0);
    QDir dir(pathName);
    if (!dir.exists()) {
        dir.mkpath(pathName);
    }
    return QFileInfo(dir.absolutePath(), "collection.db").absoluteFilePath();
}

static QString currentThreadConnectionName()
{
    thread_local const QString connName = QStringLiteral("%1_worker_%2")
                                              .arg(QString::fromLatin1(kDbConnName))
                                              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    return connName;
}

static QSqlDatabase currentThreadCollectionDb()
{
    if (QCoreApplication::instance() != nullptr
        && QThread::currentThread() == QCoreApplication::instance()->thread()
        && QSqlDatabase::contains(kDbConnName)) {
        QSqlDatabase db = QSqlDatabase::database(kDbConnName);
        if (db.isValid() && !db.isOpen()) {
            db.open();
        }
        return db;
    }

    const QString connName = currentThreadConnectionName();

    if (!QSqlDatabase::contains(connName)) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(collectionDbPath());
        if (db.open()) {
            QSqlQuery pragma(QStringLiteral("PRAGMA journal_mode=WAL"), db);
            (void)pragma.next();
            QSqlQuery timeout(QStringLiteral("PRAGMA busy_timeout=5000"), db);
            (void)timeout.exec();
        }
    }

    QSqlDatabase db = QSqlDatabase::database(connName);
    if (db.isValid() && !db.isOpen()) {
        db.open();
    }
    return db;
}

CollectionDB::CollectionDB()
{
    p = new CollectionDbPrivate;

    p->genreCount = 0;
    p->resultCount = 0;
    p->sqlQuickFilter = QString("");

    p->sqlFromString = "FROM tags "
                       " INNER JOIN artist ON tags.artist = artist.id "
                       " INNER JOIN album ON tags.album = album.id "
                       " INNER JOIN year ON tags.year = year.id "
                       " INNER JOIN genre ON tags.genre = genre.id "
                       " LEFT OUTER JOIN statistics ON tags.url = statistics.url "
                       " LEFT OUTER JOIN analysis_cache ON tags.url = analysis_cache.url "
                       " LEFT OUTER JOIN favorites ON tags.url = favorites.url WHERE 1=1 ";

    p->sqlFromStringPL = "FROM tags "
                         " INNER JOIN artist ON tags.artist = artist.id "
                         " INNER JOIN album ON tags.album = album.id "
                         " INNER JOIN year ON tags.year = year.id "
                         " INNER JOIN genre ON tags.genre = genre.id "
                         " INNER JOIN playlists ON tags.url = playlists.url "
                         " LEFT OUTER JOIN statistics ON tags.url = statistics.url "
                         " LEFT OUTER JOIN analysis_cache ON tags.url = analysis_cache.url "
                         " LEFT OUTER JOIN favorites ON tags.url = favorites.url WHERE 1=1 ";

    createTables(false);
    createStatsTable();
    repairNullMetadata();
}

void CollectionDB::repairNullMetadata()
{
    const QStringList tables = { "artist", "album", "year", "genre" };
    for (const QString& table : tables) {
        executeSql("INSERT OR IGNORE INTO " + table + " (name) VALUES ('');");
        executeSql("UPDATE tags SET " + table + " = (SELECT id FROM " + table
                   + " WHERE name = '' LIMIT 1) WHERE " + table + " IS NULL;");
    }
}

CollectionDB::~CollectionDB()
{
    delete p;
    p = nullptr;
}

bool CollectionDB::isDbValid()
{
    const QSqlDatabase db = currentThreadCollectionDb();
    return db.isValid() && db.isOpen();
}

bool CollectionDB::isEmpty()
{
    return selectSqlNumber("SELECT COUNT(*) FROM tags") == 0;
}

void CollectionDB::incSongCounter(const QString url)
{
    QSqlQuery query(currentThreadCollectionDb());
    query.prepare("UPDATE statistics SET playcounter = playcounter + 1 WHERE url = ?");
    query.addBindValue(url);
    query.exec();
}

void CollectionDB::setSongRate(const QString url, int rate)
{
    QSqlQuery query(currentThreadCollectionDb());
    query.prepare("UPDATE statistics SET rating = ? WHERE url = ?");
    query.addBindValue(rate);
    query.addBindValue(url);
    query.exec();
}

void CollectionDB::setSongBpm(const QString url, int bpm)
{
    executeSql(QString("UPDATE analysis_cache SET bpm = %1 WHERE url = '%2'").arg(bpm).arg(url));
}

void CollectionDB::updateDirStats(QString path, const long datetime)
{
    QSqlQuery query(currentThreadCollectionDb());
    query.prepare("INSERT OR REPLACE INTO directories (dir, changedate) VALUES (?, ?)");
    query.addBindValue(path);
    query.addBindValue(static_cast<qlonglong>(datetime));
    query.exec();
}

void CollectionDB::removeSongsInDir(QString path)
{
    QSqlQuery query(currentThreadCollectionDb());
    query.prepare("DELETE FROM tags WHERE url LIKE ?");
    query.addBindValue(path + "%");
    query.exec();
    
    // Remove from other related tables as well
    query.prepare("DELETE FROM statistics WHERE url LIKE ?");
    query.addBindValue(path + "%");
    query.exec();
    
    query.prepare("DELETE FROM analysis_cache WHERE url LIKE ?");
    query.addBindValue(path + "%");
    query.exec();

    // Update counts and clean up
    executeSql("VACUUM");
}

bool CollectionDB::isDirInCollection(QString path)
{
    return selectSqlNumber("SELECT COUNT(*) FROM tags WHERE url LIKE '" + path + "%'") > 0;
}

void CollectionDB::removeDirFromCollection(QString path)
{
    removeSongsInDir(path);
}

void CollectionDB::removePlaylist(QString name)
{
    QSqlQuery query(currentThreadCollectionDb());
    query.prepare("DELETE FROM playlists WHERE name = ?");
    query.addBindValue(name);
    query.exec();
    
    // Delete all entries for this playlist
    query.prepare("DELETE FROM playlisttracks WHERE playlist = ?");
    query.addBindValue(name);
    query.exec();
}

void CollectionDB::setFilterString(QString string)
{
    string = escapeString(string);
    p->filterString = string;
    p->sqlQuickFilter = "";

    const QStringList tokens = string.split(' ', Qt::SkipEmptyParts);
    for (const QString& token : tokens) {
        p->sqlQuickFilter += QString(" AND ( lower(artist.name) LIKE lower('%%1%') OR "
                                     "lower(album.name) LIKE lower('%%1%') OR "
                                     "lower(tags.title) LIKE lower('%%1%') OR "
                                     "lower(genre.name) LIKE lower('%%1%') OR "
                                     "lower(year.name) LIKE lower('%%1%') OR "
                                     "lower(tags.url) LIKE lower('%%1%') )")
                                 .arg(token);
    }
}

bool CollectionDB::executeSql(const QString& statement)
{
    QSqlQuery query(currentThreadCollectionDb());
    query.prepare(statement);
    return query.exec();
}

QList<QStringList> CollectionDB::selectSql(const QString& statement)
{
    QList<QStringList> result;
    
    QSqlQuery query(currentThreadCollectionDb());
    if (!query.prepare(statement)) {
        qWarning() << Q_FUNC_INFO << "prepare failed:" << query.lastError().text()
                   << statement;
        return result;
    }
    
    if (query.exec()) {
        while (query.next()) {
            QStringList row;
            for (int i = 0; i < query.record().count(); ++i) {
                row << query.value(i).toString();
            }
            result << row;
        }
    } else {
        qWarning() << Q_FUNC_INFO << "exec failed:" << query.lastError().text()
               << statement;
    }
    
    return result;
}

long CollectionDB::selectSqlNumber(const QString& statement)
{
    QSqlQuery query(currentThreadCollectionDb());
    query.prepare(statement);
    
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    
    return 0;
}

int CollectionDB::sqlInsertID()
{
    QSqlQuery query(currentThreadCollectionDb());
    query.prepare("SELECT last_insert_rowid()");
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

QString CollectionDB::escapeString(QString string)
{
    string.replace("'", "''");
    return string;
}

ulong CollectionDB::getValueID(QString name, QString value, bool autocreate, bool useTempTables)
{
    QSqlQuery query(currentThreadCollectionDb());
    if (value.isNull())
        value = QString();
    QString table = name.simplified();
    if (useTempTables)
        table += "_temp";
    
    // Create the table if necessary
    if (autocreate) {
        executeSql("CREATE TABLE IF NOT EXISTS " + table + " (id INTEGER PRIMARY KEY, name VARCHAR(255))");
    }
    
    // Check if value exists
    query.prepare("SELECT id FROM " + table + " WHERE name = ?");
    query.addBindValue(value);
    
    if (query.exec() && query.next()) {
        return query.value(0).toULongLong();
    } else if (autocreate) {
        // Create new entry 
        query.prepare("INSERT INTO " + table + " (name) VALUES (?)");
        query.addBindValue(value);
        query.exec();
        
        return sqlInsertID();
    }
    
    return 0;
}

ulong CollectionDB::getCount()
{
    return selectSqlNumber("SELECT COUNT(*) FROM tags");
}

uint CollectionDB::getCount(QString path, QString genre, QString artist)
{
    QString sql = "SELECT COUNT(*) FROM tags "
                  " INNER JOIN artist ON tags.artist = artist.id "
                  " INNER JOIN album ON tags.album = album.id "
                  " INNER JOIN year ON tags.year = year.id "
                  " INNER JOIN genre ON tags.genre = genre.id WHERE 1=1 ";
    
    if (!path.isEmpty()) {
        sql += " AND tags.url LIKE '" + path.replace("'", "''") + "%'";
    }
    if (!genre.isEmpty()) {
        sql += " AND genre.name = '" + genre.replace("'", "''") + "'";
    }
    if (!artist.isEmpty()) {
        sql += " AND artist.name = '" + artist.replace("'", "''") + "'";
    }
    
    return static_cast<uint>(selectSqlNumber(sql));
}

QPair<int, int> CollectionDB::getCount(QStringList paths, QStringList genres, QStringList artists)
{
    Q_UNUSED(paths);
    Q_UNUSED(genres);
    Q_UNUSED(artists);
    
    // Placeholder implementation
    return QPair<int, int>(0, 0);
}

long CollectionDB::lastLengthSum()
{
    return selectSqlNumber("SELECT SUM(length) FROM tags");
}

uint CollectionDB::lastMaxCount()
{
    return static_cast<uint>(selectSqlNumber("SELECT MAX(playcounter) FROM statistics"));
}

QList<QStringList> CollectionDB::selectRandomEntry(QString rownum, QString path, QString genre, QString artist)
{
    bool ok = false;
    int limit = rownum.toInt(&ok);
    if (!ok || limit <= 0)
        limit = 1;

    QString sql = "SELECT tags.url, artist.name, tags.title, album.name, year.name, genre.name, "
                  "tags.track, tags.length, COALESCE(statistics.playcounter, 0), "
                  "COALESCE(analysis_cache.bpm, 0), COALESCE(statistics.rating, 0) "
                  "FROM tags "
                  " LEFT JOIN artist ON tags.artist = artist.id "
                  " LEFT JOIN album ON tags.album = album.id "
                  " LEFT JOIN year ON tags.year = year.id "
                  " LEFT JOIN genre ON tags.genre = genre.id "
                  " LEFT OUTER JOIN statistics ON tags.url = statistics.url "
                  " LEFT OUTER JOIN analysis_cache ON tags.url = analysis_cache.url "
                  " WHERE 1=1 ";

    if (!path.isEmpty())
        sql += " AND tags.url LIKE '" + escapeString(path) + "%'";
    if (!genre.isEmpty())
        sql += " AND genre.name = '" + escapeString(genre) + "'";
    if (!artist.isEmpty())
        sql += " AND artist.name = '" + escapeString(artist) + "'";

    sql += QString(" ORDER BY RANDOM() LIMIT %1").arg(limit);
    return selectSql(sql);
}

QStringList CollectionDB::getRandomEntry()
{
    return getRandomEntry("", "", "");
}

QStringList CollectionDB::getRandomEntry(QString path, QString genre, QString artist)
{
    const QList<QStringList> rows = selectRandomEntry("1", path, genre, artist);
    if (rows.isEmpty())
        return QStringList();
    return rows.first();
}

void CollectionDB::createTables(const bool temporary)
{
    if (temporary) {
        executeSql("DROP TABLE IF EXISTS tags_temp;");
        executeSql("DROP TABLE IF EXISTS artist_temp;");
        executeSql("DROP TABLE IF EXISTS album_temp;");
        executeSql("DROP TABLE IF EXISTS year_temp;");
        executeSql("DROP TABLE IF EXISTS genre_temp;");

        executeSql("CREATE TABLE IF NOT EXISTS tags_temp ("
                   "url VARCHAR(1024) PRIMARY KEY, "
                   "dir VARCHAR(1024), "
                   "artist INTEGER, "
                   "title VARCHAR(1024), "
                   "album INTEGER, "
                   "genre INTEGER, "
                   "year INTEGER, "
                   "length INTEGER, "
                   "track VARCHAR(64));");
        executeSql("CREATE TABLE IF NOT EXISTS artist_temp (id INTEGER PRIMARY KEY AUTOINCREMENT, name VARCHAR(1024) UNIQUE);");
        executeSql("CREATE TABLE IF NOT EXISTS album_temp (id INTEGER PRIMARY KEY AUTOINCREMENT, name VARCHAR(1024) UNIQUE);");
        executeSql("CREATE TABLE IF NOT EXISTS year_temp (id INTEGER PRIMARY KEY AUTOINCREMENT, name VARCHAR(64) UNIQUE);");
        executeSql("CREATE TABLE IF NOT EXISTS genre_temp (id INTEGER PRIMARY KEY AUTOINCREMENT, name VARCHAR(255) UNIQUE);");
        return;
    }

    executeSql("CREATE TABLE IF NOT EXISTS tags ("
               "url VARCHAR(1024) PRIMARY KEY, "
               "dir VARCHAR(1024), "
               "artist INTEGER, "
               "title VARCHAR(1024), "
               "album INTEGER, "
               "genre INTEGER, "
               "year INTEGER, "
               "length INTEGER, "
               "track VARCHAR(64));");
    executeSql("CREATE TABLE IF NOT EXISTS artist (id INTEGER PRIMARY KEY AUTOINCREMENT, name VARCHAR(1024) UNIQUE);");
    executeSql("CREATE TABLE IF NOT EXISTS album (id INTEGER PRIMARY KEY AUTOINCREMENT, name VARCHAR(1024) UNIQUE);");
    executeSql("CREATE TABLE IF NOT EXISTS year (id INTEGER PRIMARY KEY AUTOINCREMENT, name VARCHAR(64) UNIQUE);");
    executeSql("CREATE TABLE IF NOT EXISTS genre (id INTEGER PRIMARY KEY AUTOINCREMENT, name VARCHAR(255) UNIQUE);");
    executeSql("CREATE TABLE IF NOT EXISTS directories (dir VARCHAR(1024) PRIMARY KEY, changedate INTEGER);");
    executeSql("CREATE TABLE IF NOT EXISTS statistics (url VARCHAR(1024) PRIMARY KEY, playcounter INTEGER DEFAULT 0, rating INTEGER DEFAULT 0, lastplayed INTEGER DEFAULT 0);");
    executeSql("CREATE TABLE IF NOT EXISTS favorites (url VARCHAR(1024) PRIMARY KEY);");
    executeSql("CREATE TABLE IF NOT EXISTS analysis_cache (url VARCHAR(1024) PRIMARY KEY, bpm INTEGER DEFAULT 0, beat_offset_ms INTEGER DEFAULT 0, changedate INTEGER DEFAULT 0, analysis_version INTEGER DEFAULT 0, envelope_version INTEGER DEFAULT 0, envelope_data BLOB, envelope_duration_ms INTEGER DEFAULT 0, start_position_ms INTEGER DEFAULT 0, end_position_ms INTEGER DEFAULT 0, beat_start_position_ms INTEGER DEFAULT 0, beat_end_position_ms INTEGER DEFAULT 0, analysis_cache_key VARCHAR(360) DEFAULT '');");
    executeSql("CREATE TABLE IF NOT EXISTS playlists (url VARCHAR(1024), name VARCHAR(255), length INTEGER DEFAULT 0, flags INTEGER DEFAULT 0, norder INTEGER DEFAULT 0, changedate INTEGER DEFAULT 0);");
    executeSql("CREATE TABLE IF NOT EXISTS playlisttracks (id INTEGER PRIMARY KEY AUTOINCREMENT, playlist VARCHAR(255), url VARCHAR(1024));");
}

void CollectionDB::dropTables(const bool temporary)
{
    if (temporary) {
        executeSql("DROP TABLE IF EXISTS tags_temp;");
        executeSql("DROP TABLE IF EXISTS artist_temp;");
        executeSql("DROP TABLE IF EXISTS album_temp;");
        executeSql("DROP TABLE IF EXISTS year_temp;");
        executeSql("DROP TABLE IF EXISTS genre_temp;");
        return;
    }

    executeSql("DROP TABLE IF EXISTS tags;");
    executeSql("DROP TABLE IF EXISTS artist;");
    executeSql("DROP TABLE IF EXISTS album;");
    executeSql("DROP TABLE IF EXISTS year;");
    executeSql("DROP TABLE IF EXISTS genre;");
    executeSql("DROP TABLE IF EXISTS directories;");
    executeSql("DROP TABLE IF EXISTS favorites;");
    executeSql("DROP TABLE IF EXISTS playlists;");
    executeSql("DROP TABLE IF EXISTS playlisttracks;");
}

void CollectionDB::moveTempTables()
{
    executeSql("INSERT OR IGNORE INTO artist(name) SELECT name FROM artist_temp;");
    executeSql("INSERT OR IGNORE INTO album(name) SELECT name FROM album_temp;");
    executeSql("INSERT OR IGNORE INTO year(name) SELECT name FROM year_temp;");
    executeSql("INSERT OR IGNORE INTO genre(name) SELECT name FROM genre_temp;");

    executeSql("INSERT OR REPLACE INTO tags(url, dir, artist, title, album, genre, year, length, track) "
               "SELECT tt.url, tt.dir, a.id, tt.title, al.id, g.id, y.id, tt.length, tt.track "
               "FROM tags_temp tt "
               "LEFT JOIN artist_temp at ON at.id = tt.artist "
               "LEFT JOIN album_temp alt ON alt.id = tt.album "
               "LEFT JOIN year_temp yt ON yt.id = tt.year "
               "LEFT JOIN genre_temp gt ON gt.id = tt.genre "
               "LEFT JOIN artist a ON a.name IS at.name "
               "LEFT JOIN album al ON al.name IS alt.name "
               "LEFT JOIN year y ON y.name IS yt.name "
               "LEFT JOIN genre g ON g.name IS gt.name;");
}

void CollectionDB::createStatsTable()
{
    executeSql("CREATE TABLE IF NOT EXISTS statistics (url VARCHAR(255) PRIMARY KEY, playcounter INTEGER, rating INTEGER, lastplayed INTEGER);");

    const QList<QPair<QString, QString>> columns = {
        { "playcounter", "INTEGER DEFAULT 0" },
        { "rating", "INTEGER DEFAULT 0" },
        { "lastplayed", "INTEGER DEFAULT 0" }
    };
    const QList<QStringList> existingColumns = selectSql("PRAGMA table_info(statistics)");
    for (const auto& column : columns) {
        bool exists = false;
        for (const QStringList& existingColumn : existingColumns) {
            if (existingColumn.value(1) == column.first) {
                exists = true;
                break;
            }
        }
        if (!exists)
            executeSql("ALTER TABLE statistics ADD COLUMN " + column.first + " " + column.second);
    }
}

void CollectionDB::dropStatsTable()
{
    // Placeholder implementation
    executeSql("DROP TABLE IF EXISTS statistics");
}

void CollectionDB::resetSongCounter()
{
    QSqlQuery query(currentThreadCollectionDb());
    query.prepare("UPDATE statistics SET playcounter = 0 WHERE playcounter > 0");
    query.exec();
}

void CollectionDB::purgeDirCache()
{
    executeSql("DELETE FROM directories;");
}

void CollectionDB::scanModifiedDirs(bool recursively)
{
    Q_UNUSED(recursively);
    // Placeholder implementation - scan logic needs to be implemented based on project design
}

void CollectionDB::scan(const QStringList& folders, bool recursively)
{
    Q_UNUSED(folders);
    Q_UNUSED(recursively);
    // Placeholder implementation - scan logic needs to be implemented based on project design
}

QList<QStringList> CollectionDB::selectTracks(QString year, QString genre, QString artist, QString album)
{
    QString sql = "SELECT tags.url, artist.name, tags.title, album.name, year.name, genre.name, "
                  "tags.track, tags.length, COALESCE(statistics.playcounter, 0), "
                  "COALESCE(analysis_cache.bpm, 0), COALESCE(statistics.rating, 0) "
                  "FROM tags "
                  " LEFT JOIN artist ON tags.artist = artist.id "
                  " LEFT JOIN album ON tags.album = album.id "
                  " LEFT JOIN year ON tags.year = year.id "
                  " LEFT JOIN genre ON tags.genre = genre.id "
                  " LEFT OUTER JOIN statistics ON tags.url = statistics.url "
                  " LEFT OUTER JOIN analysis_cache ON tags.url = analysis_cache.url WHERE 1=1 ";
    sql += p->sqlQuickFilter;
    
    if (!year.isEmpty()) {
        sql += " AND year.name = '" + year.replace("'", "''") + "'";
    }
    if (!genre.isEmpty()) {
        sql += " AND genre.name = '" + genre.replace("'", "''") + "'";
    }
    if (!artist.isEmpty()) {
        sql += " AND artist.name = '" + artist.replace("'", "''") + "'";
    }
    if (!album.isEmpty()) {
        sql += " AND album.name = '" + album.replace("'", "''") + "'";
    }
    
    return selectSql(sql);
}

QList<QStringList> CollectionDB::selectAlbums(QString year, QString genre, QString artist)
{
    QString sql = "SELECT DISTINCT album.name, album.id "
                  "FROM tags "
                  " INNER JOIN album ON tags.album = album.id "
                  " INNER JOIN artist ON tags.artist = artist.id "
                  " INNER JOIN year ON tags.year = year.id "
                  " INNER JOIN genre ON tags.genre = genre.id WHERE 1=1 ";
    sql += p->sqlQuickFilter;
    
    if (!year.isEmpty()) {
        sql += " AND year.name = '" + year.replace("'", "''") + "'";
    }
    if (!genre.isEmpty()) {
        sql += " AND genre.name = '" + genre.replace("'", "''") + "'";
    }
    if (!artist.isEmpty()) {
        sql += " AND artist.name = '" + artist.replace("'", "''") + "'";
    }
    
    return selectSql(sql);
}

QList<QStringList> CollectionDB::selectArtists(QString year, QString genre)
{
    QString sql = "SELECT DISTINCT artist.name, artist.id "
                  "FROM tags "
                  " INNER JOIN artist ON tags.artist = artist.id "
                  " INNER JOIN album ON tags.album = album.id "
                  " INNER JOIN year ON tags.year = year.id "
                  " INNER JOIN genre ON tags.genre = genre.id WHERE 1=1 ";
    sql += p->sqlQuickFilter;
    
    if (!year.isEmpty()) {
        sql += " AND year.name = '" + year.replace("'", "''") + "'";
    }
    if (!genre.isEmpty()) {
        sql += " AND genre.name = '" + genre.replace("'", "''") + "'";
    }
    sql += " ORDER BY artist.name COLLATE NOCASE ASC";
    
    return selectSql(sql);
}

QList<QStringList> CollectionDB::selectYears()
{
    QString sql = "SELECT DISTINCT year.name, year.id FROM tags "
                  " INNER JOIN artist ON tags.artist = artist.id "
                  " INNER JOIN album ON tags.album = album.id "
                  " INNER JOIN year ON tags.year = year.id "
                  " INNER JOIN genre ON tags.genre = genre.id WHERE 1=1 ";
    sql += p->sqlQuickFilter;
    
    return selectSql(sql);
}

QList<QStringList> CollectionDB::selectGenres()
{
    QString sql = "SELECT DISTINCT genre.name, genre.id "
                  "FROM tags "
                  " INNER JOIN artist ON tags.artist = artist.id "
                  " INNER JOIN album ON tags.album = album.id "
                  " INNER JOIN year ON tags.year = year.id "
                  " INNER JOIN genre ON tags.genre = genre.id WHERE 1=1 ";
    sql += p->sqlQuickFilter;
    
    return selectSql(sql);
}

QList<QStringList> CollectionDB::selectHotTracks()
{
    QString sql = "SELECT tags.url, artist.name, tags.title, album.name, year.name, genre.name, "
                  "tags.track, tags.length, COALESCE(statistics.playcounter, 0), "
                  "COALESCE(analysis_cache.bpm, 0), COALESCE(statistics.rating, 0) "
                  "FROM tags "
                  " INNER JOIN artist ON tags.artist = artist.id "
                  " INNER JOIN album ON tags.album = album.id "
                  " INNER JOIN year ON tags.year = year.id "
                  " INNER JOIN genre ON tags.genre = genre.id "
                  " INNER JOIN statistics ON tags.url = statistics.url "
                  " LEFT OUTER JOIN analysis_cache ON tags.url = analysis_cache.url "
                  " WHERE statistics.playcounter > 0 ORDER BY statistics.playcounter DESC LIMIT 10";
    
    return selectSql(sql);
}

QList<QStringList> CollectionDB::selectLastTracks()
{
    QString sql = "SELECT tags.url, artist.name, tags.title, album.name, year.name, genre.name, "
                  "tags.track, tags.length, COALESCE(statistics.playcounter, 0), "
                  "COALESCE(analysis_cache.bpm, 0), COALESCE(statistics.rating, 0) "
                  "FROM tags "
                  " INNER JOIN artist ON tags.artist = artist.id "
                  " INNER JOIN album ON tags.album = album.id "
                  " INNER JOIN year ON tags.year = year.id "
                  " INNER JOIN genre ON tags.genre = genre.id "
                  " INNER JOIN statistics ON tags.url = statistics.url "
                  " LEFT OUTER JOIN analysis_cache ON tags.url = analysis_cache.url "
                  " WHERE statistics.lastplayed > 0 ORDER BY statistics.lastplayed DESC LIMIT 10";
    
    return selectSql(sql);
}

QList<QStringList> CollectionDB::selectFavoritesTracks()
{
    QString sql = "SELECT tags.url, artist.name, tags.title, album.name, year.name, genre.name, "
                  "tags.track, tags.length, COALESCE(statistics.playcounter, 0), "
                  "COALESCE(analysis_cache.bpm, 0), COALESCE(statistics.rating, 0) "
                  "FROM tags "
                  " INNER JOIN artist ON tags.artist = artist.id "
                  " INNER JOIN album ON tags.album = album.id "
                  " INNER JOIN year ON tags.year = year.id "
                  " INNER JOIN genre ON tags.genre = genre.id "
                  " INNER JOIN favorites ON tags.url = favorites.url "
                  " LEFT OUTER JOIN statistics ON tags.url = statistics.url "
                  " LEFT OUTER JOIN analysis_cache ON tags.url = analysis_cache.url";
    
    return selectSql(sql);
}

QList<QStringList> CollectionDB::selectPlaylistData()
{
    QString sql = "SELECT playlists.name, "
                  "COUNT(playlists.url), "
                  "COALESCE(SUM(playlists.length), 0), "
                  "COALESCE(MAX(statistics.lastplayed), 0) "
                  "FROM playlists "
                  "LEFT JOIN statistics ON statistics.url = playlists.url "
                  "GROUP BY playlists.name";
    return selectSql(sql);
}

QList<QStringList> CollectionDB::selectPlaylistTracks(QString name)
{
    QString sql = "SELECT tags.url, artist.name, tags.title, album.name, year.name, genre.name, "
                  "tags.track, tags.length, COALESCE(statistics.playcounter, 0), "
                  "COALESCE(analysis_cache.bpm, 0), COALESCE(statistics.rating, 0), "
                  "COALESCE(playlists.flags, 0) "
                  "FROM tags "
                  " INNER JOIN artist ON tags.artist = artist.id "
                  " INNER JOIN album ON tags.album = album.id "
                  " INNER JOIN year ON tags.year = year.id "
                  " INNER JOIN genre ON tags.genre = genre.id "
                  " INNER JOIN playlists ON tags.url = playlists.url "
                  " LEFT OUTER JOIN statistics ON tags.url = statistics.url "
                  " LEFT OUTER JOIN analysis_cache ON tags.url = analysis_cache.url "
                  " WHERE playlists.name = '" + name.replace("'", "''") + "' "
                  " ORDER BY playlists.norder ASC";
    
    return selectSql(sql);
}

bool CollectionDB::ensureCollectionDatabase()
{
    // Since the connection setup is already handled in main.cpp, we just 
    // need to make sure it's there. In QtSql, if a named connection exists,
    // then QSqlDatabase::database(name) will return it.
    
    // We don't actually need to re-create connections from here since
    // they are set up in main.cpp - this function mainly exists for 
    // backward compatibility purposes
    
    return QSqlDatabase::contains(kDbConnName);
}
