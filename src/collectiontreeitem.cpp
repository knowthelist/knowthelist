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

#include "collectiontreeitem.h"

struct CollectionTreeItemPrivate
{
    QString artist;
    QString album;
    QString year;
    QString genre;
};

CollectionTreeItem::CollectionTreeItem(QTreeWidget* parent, int type) :
    QTreeWidgetItem(parent,type)
    ,p(new CollectionTreeItemPrivate)
{
    setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
}

CollectionTreeItem::CollectionTreeItem(QTreeWidgetItem* parent, int type) :
    QTreeWidgetItem(parent,type)
      ,p(new CollectionTreeItemPrivate)
{
}

CollectionTreeItem::~CollectionTreeItem()
{
    delete p;
}

QString CollectionTreeItem::artist()
{
    return p->artist;
}

QString CollectionTreeItem::album()
{
    return p->album;
}

QString CollectionTreeItem::year()
{
    return p->year;
}

QString CollectionTreeItem::genre()
{
    return p->genre;
}

void CollectionTreeItem::setArtist(QString value)
{
    p->artist=value;
    //if (p->album.isEmpty())
        setText(0,value);
    QTreeWidgetItem::setIcon( 0, QIcon( style()->standardIcon(QStyle::SP_DirHomeIcon).pixmap(12)) );
}

void CollectionTreeItem::setAlbum(QString value)
{
    p->album=value;
    setText(0,value);
    QTreeWidgetItem::setIcon( 0, QIcon(style()->standardIcon(QStyle::SP_DriveCDIcon).pixmap(12)) );
}

void CollectionTreeItem::setYear(QString value)
{
    p->year=value;
    setText(0,value);
    QTreeWidgetItem::setIcon( 0, QIcon(style()->standardIcon(QStyle::SP_FileIcon).pixmap(12)) );
}

void CollectionTreeItem::setGenre(QString value)
{
    p->genre=value;
    setText(0,value);
    QTreeWidgetItem::setIcon( 0, QIcon(style()->standardIcon(QStyle::SP_DirIcon).pixmap(12)) );
}
