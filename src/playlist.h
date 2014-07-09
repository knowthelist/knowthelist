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


#ifndef PLAYLIST_H
#define PLAYLIST_H

#include "track.h"

#include <QTreeWidget>

class Playlist: public QTreeWidget
{
  Q_OBJECT
  public:
      Playlist(QWidget* parent=0);
      ~Playlist();
      //virtual void keyPressEvent   (   QKeyEvent* e   );
      //virtual bool isExecuteArea( const QPoint& point );
      

      enum Mode  { Tracklist = 0,
                   Playlist_Single = 1,
                   Playlist_Multi = 2
                 };

      /** Add a songs to the plalist*/
      void addTrack( QUrl file, PlaylistItem *after );
      void appendSong( Track *track );
      void addTrack( Track *track, PlaylistItem *after );
      void appendSong( QString songFileName );
      void appendList( QList<QUrl>,  PlaylistItem *after );
      void appendTracks( const QList<Track*> tracks, PlaylistItem* after );

      void saveXML( const QString& ) const;
      void loadXML( const QString& );

      //----------------
      PlaylistItem *firstTrack() const { return firstChild(); }
      PlaylistItem *nextTrack() const { return nextPlaylistItem ; }
      PlaylistItem *currentTrack() const { return currentPlaylistItem; }
      PlaylistItem *dragTrack() const { return (PlaylistItem*)this->currentItem(); }
      PlaylistItem *newTrack() const { return newPlaylistItem ; }
      QList<Track*> allTracks();
      bool isCurrentList() const { return m_isCurrentList; }
      void setIsCurrentList( bool b ) {m_isCurrentList = b; handleChanges();}

      bool isEmpty() const { return this->topLevelItemCount() == 0; }
      bool isFirst() const { return this->topLevelItemCount() == 1; }

      void setPlaylistMode(Mode);
      void setAutoClearOn( bool b ) {autoClearOn = b;}

      QString defaultPlaylistPath();
      static void showTrackInfo( Track* bundle );
      PlaylistItem* lastChild() const {return (PlaylistItem*)topLevelItem(topLevelItemCount()-1);}
      int countTrack() const {return topLevelItemCount();}

      //ToDo: what is for private only? Sort!



  protected:
      void keyPressEvent (QKeyEvent * event);
      void mousePressEvent(QMouseEvent *event);
      void mouseMoveEvent(QMouseEvent *event);
      void mouseReleaseEvent(QMouseEvent *event);
      void mouseDoubleClickEvent(QMouseEvent *event);
      void dragEnterEvent(QDragEnterEvent *event);
      void dropEvent(QDropEvent *event);
      void dragMoveEvent (QDragMoveEvent *event);
      void dragLeaveEvent (QDragLeaveEvent *event);
      void paintEvent ( QPaintEvent* event ); //For better DropTarget



      QMimeData *mimeData;
      QDrag *drag;
      QRect dropSite;
      Qt::DropAction dropAction;
      bool showDropHighlighter;

    public Q_SLOTS:
     void appendList( QList<QUrl> );
     void appendTracks( const QList<Track*> tracks);
     void changeTracks( const QList<Track*> tracks);
     void addCurrentTrack( Track* );
     void addNextTrack( Track* );
     void removeSelectedItems();
     void setPlaying(bool plays) { m_isPlaying = plays; }
     bool isPlaying() { return m_isPlaying; }
     void setAlternateMax(int max) { m_alternateMax = max; fillNoColumn(); }
     void skipForward();
     void skipRewind();


        Q_SIGNALS:
          void  currentTrackChanged(Track*);
          void  trackDoubleClicked( PlaylistItem* );
          void  trackClicked( PlaylistItem* );
          void  trackChanged( PlaylistItem* );
          void  wantSearch(QString);
          void  wantLoad(PlaylistItem*, QString);
          void  countChanged(int);
          void  countChanged(QList<Track*>);
      
   private:
    
          void setCurrentPlaylistItem( PlaylistItem* );
          void setNextPlaylistItem( PlaylistItem* );
          void removePlaylistItem( PlaylistItem* );

          void updateNextPlaylistItem();
          void updateCurrentPlaylistItem();
          void updatePlaylistItems();

      int  m_recursionCount;
      int mDropVisualizerWidth;
      int m_alternateMax;
      int m_currentIndex;
      void fillNoColumn();
      void performDrag();
      QTimer *timer;
      QTimer *timerDragLock;
      bool ignoreNextRelease;
      bool m_dragLocked;

      QPoint startPos;

    bool m_isPlaying;
    bool m_isCurrentList;
    bool m_isInternDrop;

    bool autoClearOn;


    PlaylistItem *nextPlaylistItem ;    //the item to be played after the current track
    PlaylistItem *previousPlaylistItem;
    PlaylistItem *newPlaylistItem ;     //the latest item
    PlaylistItem *currentPlaylistItem; //the item that is playing
    PlaylistItem *m_marker;             //the item that has the drag/drop marker under it

    QColor m_CurrentTrackColor;
    QColor m_NextTrackColor;

    Mode m_PlaylistMode;

      inline PlaylistItem *firstChild() const { return (PlaylistItem*)topLevelItem(0); }
      inline PlaylistItem *lastItem() const { return lastChild(); }

      
    private Q_SLOTS:

        void showContextMenu( PlaylistItem *, int );
        void slotItemClicked(QTreeWidgetItem*,int);
        void slotItemDoubleClicked ( QTreeWidgetItem *item, int column );
        void emitClicked();
        void timeoutDragLock();
        void handleChanges();
        void slotItemChanged( QTreeWidgetItem * current, QTreeWidgetItem * previous );
        void dummySlot();
        
};


      
#endif
