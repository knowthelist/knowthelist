#include "playlistwidget.h"
#include "ui_playlistwidget.h"

#include <qdebug.h>

class PlaylistWidgetPrivate
{
    public:
    QString name;
    QString description;
    bool isActive;
};

PlaylistWidget::PlaylistWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PlaylistWidget)
{
    ui->setupUi(this);
    p = new PlaylistWidgetPrivate;
    p->isActive=false;
    QFont font = ui->lblName->font();
    int newSize = font.pointSize()-1;
    font.setPixelSize(newSize);
    ui->lblDesciption->setFont(font);
}

PlaylistWidget::~PlaylistWidget()
{
    delete ui;
    delete p;
}

// auto connect slot
void PlaylistWidget::on_lblName_linkActivated(const QString &link)
{
    qDebug() << __PRETTY_FUNCTION__ ;
    Q_EMIT activated();
}

QString PlaylistWidget::name()
{
    return p->name;
}

void PlaylistWidget::setName(QString value)
{
    p->name=value;
    updateView();
}

QString PlaylistWidget::description()
{
    return p->description;
}

void PlaylistWidget::setDescription(QString value)
{
    p->description=value;
    updateView();
}

void PlaylistWidget::activate()
{
    p->isActive = true;
    updateView();
}

void PlaylistWidget::deactivate()
{
    p->isActive = false;
    updateView();
}

void PlaylistWidget::updateView()
{
    // update Labels
    ui->lblDesciption->setText(p->description);

    // active/passive look differentiation
    QString activeStyle;
    if (p->isActive){
       activeStyle = "color:#ff6464;'>";
       ui->framePlaylistWidget->setStyleSheet("#framePlaylistWidget {	background: qlineargradient("
                           "x1:0, y1:0, x2:0, y2:1,"
                           "stop: 0.01 #202020,"
                            "stop:0.11 #505050,"
                           "stop:1 #505050"
                           ");}");
    }
    else
    {
       activeStyle = "color:#eeeeee;'>";
       ui->framePlaylistWidget->setStyleSheet("#framePlaylistWidget {		background: qlineargradient("
                           "x1:0, y1:0, x2:0, y2:1,"
                           "stop: 0.01 #202020,"
                            "stop:0.11 #404040,"
                           "stop:1 #404040"
                           ");}");
        }

    ui->lblName->setText("<!DOCTYPE HTML PUBLIC '-//W3C//DTD HTML 4.0//EN' 'http://www.w3.org/TR/REC-html40/strict.dtd'>"
                         "<html><head><meta name='qrichtext' content='1' /><style type='text/css'>p, li { white-space: pre-wrap; }"
                         "</style></head><body style='font-size:8.25pt; font-weight:400; font-style:normal;'><p style=' margin-top:0px; "
                         "margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'>"
                         "<a href='fdadfaf'><span style=' font-size:12pt;font-style:normal;font-weight:bold; text-decoration: underline; "
                         + activeStyle
                         + p->name +
                         "</span></a></p></body></html>");
}

void PlaylistWidget::on_butPlay_pressed()
{
    Q_EMIT started();
}
