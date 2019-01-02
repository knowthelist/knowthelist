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

#ifndef COLLECTIONTREEITEM_H
#define COLLECTIONTREEITEM_H

#include <QTreeWidgetItem>

class CollectionTreeItem : public QWidget, public QTreeWidgetItem
{
    Q_OBJECT
public:
    explicit CollectionTreeItem(QObject *parent = 0, int type=Type);
     CollectionTreeItem(QTreeWidget *parent = 0, int type=Type);
     CollectionTreeItem(QTreeWidgetItem *parent = 0, int type=Type);
     ~CollectionTreeItem();

     QString artist();
     QString album();
     QString year();
     QString genre();
     void setArtist(QString value);
     void setAlbum(QString value);
     void setYear(QString value);
     void setGenre(QString value);
     void setTextString(QString value);

signals:
    
public slots:
    
private:

    class CollectionTreeItemPrivate* p;
};

#endif // COLLECTIONTREEITEM_H
