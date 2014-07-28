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
    QList<Track*> readFileList(QString filename);
    QPair<int, int> readFileValues(QString filename);
    QList<Track*> selectedTracks();
    
signals:
    void selectionStarted(QList<Track*>);
    void selectionChanged(QList<Track*>);
    void savePlaylists(QString);
    
public slots:
    void loadDatabaseList();
    void loadFileList();
    void playDatabaseList();
    void playFileList();
    void removeFileList();
    void onSelectionChanged(PlaylistWidget* item);
    void onPushSave();
    void updateLists();

private:
    class PlaylistBrowsertPrivate *p;
    
};

#endif // PLAYLISTBROWSER_H
