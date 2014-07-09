#ifndef PLAYLISTBROWSER_H
#define PLAYLISTBROWSER_H

#include "playlistwidget.h"

#include <QWidget>
#include <QtGui>

class QPushButton;
class Track;

class PlaylistBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit PlaylistBrowser(QWidget *parent = 0);
    ~PlaylistBrowser();
    void fillList();
    QList<Track*> selectedTracks();
    
signals:
    void selectionStarted(QList<Track*>);
    void selectionChanged(QList<Track*>);
    
public slots:
    void loadPlaylist();
    void playPlaylist();
    void onSelectionChanged(PlaylistWidget* item);

private:

    class PlaylistBrowsertPrivate *p;
    
};

#endif // PLAYLISTBROWSER_H
