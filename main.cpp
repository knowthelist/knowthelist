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

#include <QtGui/QApplication>
#include "knowthelist.h"

#include <QtSql>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // init rand
    srand(QDateTime::currentDateTime().toUTC().toTime_t());
    // init qrand
    QTime time = QTime::currentTime();
    qsrand((uint)time.msec());


    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    a.setQuitOnLastWindowClosed(true);

    QCoreApplication::setOrganizationName("StephanM");
    QCoreApplication::setOrganizationDomain("");
    QCoreApplication::setApplicationName("Knowthelist");
    QCoreApplication::setApplicationVersion("2.0");

// load application settings

if (!QSqlDatabase::drivers().contains("QSQLITE")) {
    QMessageBox::critical(0, QObject::tr("Unable to load database"),
                          QObject::tr("This application needs the QT4 SQLITE driver (libqt4-sql-sqlite)"));
    return 1;
}

QString pathName = QDesktopServices::storageLocation(QDesktopServices::DataLocation);
QDir path(pathName);

if (!path.exists())
    path.mkpath(pathName);

QSqlDatabase db  = QSqlDatabase::addDatabase("QSQLITE");
db.setDatabaseName( path.absolutePath() + "/collection.db");

if ( !db.open() ) {
    QMessageBox::critical(0, "fatal database error", db.lastError().text());
    return 1;
}

    Knowthelist w;
    w.show();


    return a.exec();
}
