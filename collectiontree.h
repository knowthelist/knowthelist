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

#ifndef COLLVIEW_H
#define COLLVIEW_H

#include <QTreeWidget>
#include "collectiondb.h"
#include "collectiontreeitem.h"
#include "track.h"

class QMouseEvent;

class CollectionTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit CollectionTree(QWidget *parent = 0);
    ~CollectionTree();

    QString filter();
    enum mode { MODENONE, MODEYEAR, MODEGENRE };
    mode treeMode;
    
Q_SIGNALS:
    void  selectionChanged(QList<Track*>);
    void  wantLoad(QTreeWidgetItem*, QString);
    
public slots:
void on_currentItemChanged( QTreeWidgetItem* item );
void on_itemExpanded( QTreeWidgetItem* item );
void showContextMenu(QTreeWidgetItem *&, int );
void on_itemClicked(QTreeWidgetItem*,int);
void triggerRandomSelection();
void setFilter( QString filter );
    void createTrunk();


private:
    class CollectionTreePrivate * p;

    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void resizeEvent(QResizeEvent *);
    void performDrag();
    QPoint startPos;
    bool openContext;
    bool m_dragLocked;
    void showTrackInfo( Track* mb );

};

#endif // COLLVIEW_H
