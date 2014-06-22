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

#ifndef COLLECTIONWIDGET_H
#define COLLECTIONWIDGET_H

#include "collectiondb.h"
#include "collectiontree.h"
#include "modeselector.h"
#include "playlist.h"
#include "progressbar.h"


#include <QtGui>
#include <QPixmap>
#include <QDropEvent>
#include <QCustomEvent>
#include <QTimerEvent>


//class CollectionUpdater;
class QCustomEvent;
class QPixmap;
class QPoint;
class QPushButton;


class SearchEdit: public QLineEdit{
    Q_OBJECT
    public:
        SearchEdit(  QWidget* parent=0);
        ~SearchEdit();
        void dropEvent(QDropEvent* event);
        void dragEnterEvent(QDragEnterEvent*);
        signals: 
         void  trackDropped(QString);

           protected:
              void resizeEvent(QResizeEvent *);
              void keyPressEvent (QKeyEvent* e);

          private slots:
              void updateCloseButton(const QString &text);

          private:
             QToolButton *clearButton;
};

class CollectionWidget: public QWidget
{
    Q_OBJECT

    public:
        CollectionWidget(  QWidget* parent );
        ~CollectionWidget();
        Track* getRandomSong(QString genre);
        QString filterText();
        bool hasItems();
        void setTracklist(Playlist* pl);

  public slots:
       void setFilterText( QString strFilter );
       void loadSettings();
       void scan();

       signals:
         void trackDropped(QString);
         void randomClicked();
         void selectionChanged(QList<Track*>);
         void wantLoad (QList<Track*>,QString);
         void filterChanged(QString);
         void setupDirs();
    

    private slots:
        void onSetFilterTimeout();
        void onSetClicked();
        void onSetFilter();
        void onModeSelected(ModeSelector::modeType value);

    private:

        void resizeEvent(QResizeEvent *);
        class CollectionWidgetPrivate *p;
        
};


#endif /* COLLECTIONWIDGET_H */
