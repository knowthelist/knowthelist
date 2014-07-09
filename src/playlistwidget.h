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

Q_SIGNALS:
   void activated();
   void started();

private:
    Ui::PlaylistWidget *ui;

    class PlaylistWidgetPrivate *p;

private slots:
    void on_lblName_linkActivated(const QString &link);
    void on_butPlay_pressed();
};

#endif // PLAYLISTWIDGET_H
