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

#include "knowthelist.h"

#include <QApplication>
#include <QMessageBox>
#include <QTextCodec>
#include <QTranslator>
#include <QtSql>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);

    // init rand
    qsrand(QDateTime::currentMSecsSinceEpoch() / 1000);

    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    a.setQuitOnLastWindowClosed(true);

    QCoreApplication::setOrganizationName("knowthelist-org");
    QCoreApplication::setOrganizationDomain("");
    QCoreApplication::setApplicationName("knowthelist");
    QCoreApplication::setApplicationVersion("2.3.1");

    QSettings settings;
    QStringList languages;
    languages << ""
              << "en"
              << "de"
              << "cs"
              << "hu"
              << "fr";
    QString lang = languages[settings.value("language", 0).toInt()];
    if (lang.isEmpty())
        lang = QLocale::system().name();

    QTranslator qtTranslator;
    qtTranslator.load("qt_" + lang,
        QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    a.installTranslator(&qtTranslator);

    QTranslator localization;
    bool result = localization.load(":knowthelist_" + lang + ".qm");
    qDebug() << "localization load "
             << ":knowthelist_" + lang + ".qm result:" << result;
    a.installTranslator(&localization);

    if (!QSqlDatabase::drivers().contains("QSQLITE")) {
#if QT_VERSION >= 0x050000
        QMessageBox::critical(nullptr, QObject::tr("Unable to load database"),
            QObject::tr("This application needs the QT5 SQLITE "
                        "driver (libqt5-sql-sqlite)"));
#else
        QMessageBox::critical(0, QObject::tr("Unable to load database"),
            QObject::tr("This application needs the QT4 SQLITE "
                        "driver (libqt4-sql-sqlite)"));
#endif

        return 1;
    }

#if QT_VERSION >= 0x050000
    QString pathName = QStandardPaths::standardLocations(QStandardPaths::DataLocation).at(0);
#else
    QString pathName = QDesktopServices::storageLocation(QDesktopServices::DataLocation);
#endif
    QDir path(pathName);

    if (!path.exists())
        path.mkpath(pathName);

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(path.absolutePath() + "/collection.db");

    if (!db.open()) {
        QMessageBox::critical(nullptr, "fatal database error",
            db.lastError().text());
        return 1;
    }
    qDebug() << "load database: " << db.databaseName();
    Knowthelist w;
    w.show();

    return a.exec();
}
