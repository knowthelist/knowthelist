#ifndef PLAYLISTWIDGET_H
#define PLAYLISTWIDGET_H

#include <QFrame>

namespace Ui {
class PlaylistWidget;
}

class PlaylistWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit PlaylistWidget(QWidget *parent = 0);
    ~PlaylistWidget();

    QString name();
    void setName(QString value);
    QString description();
    void setDescription(QString value);
    void updateView();
    void activate();
    void deactivate();
    void slideCloseWidget(bool open);
    bool isRemovable();
    void setRemovable(bool value);

Q_SIGNALS:
   void activated();
   void started();
   void deleted();

private:
    Ui::PlaylistWidget *ui;

    void mousePressEvent(QMouseEvent* event);
    void keyPressEvent(QKeyEvent *e);
    class PlaylistWidgetPrivate *p;

private slots:
    void on_lblName_linkActivated(const QString &link);
    void on_butPlayWidget_pressed();
    void timerSlide_timeOut();
    void on_pushClose_clicked();
};

#endif // PLAYLISTWIDGET_H
