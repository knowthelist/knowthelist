/*
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

#include "knowthelist.h"

#include <QApplication>
#include <QMessageBox>
#include <QTranslator>
#include <QtSql>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Global debug level: 0 = errors only, 1-5 increasingly verbose.
static int g_debugLevel = 0;

static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    // 0: Critical+Fatal only
    // 1: Warning+
    // 2: Info+
    // 3+: Debug+
    switch (type) {
    case QtDebugMsg:    if (g_debugLevel < 3) return; break;
    case QtInfoMsg:     if (g_debugLevel < 2) return; break;
    case QtWarningMsg:  if (g_debugLevel < 1) return; break;
    case QtCriticalMsg: break;
    case QtFatalMsg:    break;
    }

    QString prefix;
    if (g_debugLevel >= 5 && ctx.file) {
        prefix = QString("[%1:%2] ").arg(ctx.file).arg(ctx.line);
    } else if (g_debugLevel >= 4 && ctx.function) {
        prefix = QString("[%1] ").arg(ctx.function);
    }

    FILE* out = (type == QtCriticalMsg || type == QtFatalMsg) ? stderr : stdout;
    fprintf(out, "%s%s\n", prefix.toUtf8().constData(), msg.toUtf8().constData());

    if (type == QtFatalMsg)
        abort();
}

int main(int argc, char* argv[])
{
    // Parse -d <level> before QApplication so logging is set up from the start.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-d") == 0) {
            if (i + 1 < argc) {
                char* end = nullptr;
                long level = std::strtol(argv[i + 1], &end, 10);
                if (end != argv[i + 1] && level >= 1 && level <= 5) {
                    g_debugLevel = static_cast<int>(level);
                    ++i;
                } else {
                    g_debugLevel = 3; // -d without a valid number → full debug
                }
            } else {
                g_debugLevel = 3;
            }
            break;
        }
    }
    qInstallMessageHandler(messageHandler);

    QApplication a(argc, argv);

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
    (void)qtTranslator.load("qt_" + lang);
    a.installTranslator(&qtTranslator);

    QTranslator localization;
    bool result = localization.load(":knowthelist_" + lang + ".qm");
    qDebug() << "localization load "
             << ":knowthelist_" + lang + ".qm result:" << result;
    a.installTranslator(&localization);

    if (!QSqlDatabase::drivers().contains("QSQLITE")) {
        QMessageBox::critical(nullptr, QObject::tr("Unable to load database"),
            QObject::tr("This application needs the Qt SQLITE "
                        "driver (libqt6-sql-sqlite)"));
        return 1;
    }

    QString pathName = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).at(0);
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
