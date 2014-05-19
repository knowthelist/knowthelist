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

#ifndef COLLECTIONUPDATER_H
#define COLLECTIONUPDATER_H

#include "collectionwidget.h"
#include "collectionupdater.h"

#include <qstringlist.h>
#include "collectiondb.h"


class CollectionUpdater : public QObject
{
    Q_OBJECT

    public:

        CollectionUpdater();
        ~CollectionUpdater();
        void setDoMonitor(bool);
        void setDirectoryList(QStringList dirs);

        QStringList getRandomEntry(QString);


    public slots:

        void scan();
        void monitor();
        void asynchronScan(QStringList dirs);
        void stop();

    signals:
        void changesDone();
        void progressChanged(int percent);


    private:
        void readDir( const QString& dir, QStringList& entries );
        void readTags( const QStringList& entries );

        class CollectionUpdaterPrivate *p;

};


#endif // COLLECTIONUPDATER_H
