#include "playlistwidget.h"
#include "ui_playlistwidget.h"

#include <qdebug.h>
#include <QTimer>
#include <QMouseEvent>

class PlaylistWidgetPrivate
{
    public:
    QString name;
    QString description;
    bool isActive;
    QTimer* timerSlide;
    int targetWidth;
    bool isRemovable;
};

PlaylistWidget::PlaylistWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PlaylistWidget)
{
    ui->setupUi(this);
    ui->lblDesciption->setText( QString::null );
    ui->widgetClose->setMinimumWidth(0);
    ui->widgetClose->setMaximumWidth(0);

    setFocusPolicy(Qt::ClickFocus);

    p = new PlaylistWidgetPrivate;
    p->isActive=false;
    p->isRemovable=true;

    QFont font = ui->lblDesciption->font();
#if defined(Q_OS_DARWIN)
    int newSize = font.pointSize()-4;
#else
    int newSize = font.pointSize()-1;
#endif
    font.setPointSize(newSize);
    ui->lblDesciption->setFont(font);

    p->timerSlide = new QTimer(this);
    p->timerSlide->setInterval(10);
    connect( p->timerSlide, SIGNAL(timeout()), SLOT(timerSlide_timeOut()) );
}

PlaylistWidget::~PlaylistWidget()
{
    delete ui;
    delete p;
}

// auto connect slot
void PlaylistWidget::on_lblName_linkActivated(const QString &link)
{
    Q_UNUSED(link);
    Q_EMIT activated();
}

bool PlaylistWidget::isRemovable()
{
    return p->isRemovable;
}

void PlaylistWidget::setRemovable(bool value)
{
    p->isRemovable=value;
    ui->pushClose->setVisible(value);
   // if (!value)
     //r   ui->horizontalSpacer->changeSize(42,0);
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

void PlaylistWidget::mousePressEvent(QMouseEvent* event)
{
    if(ui->widgetClose->geometry().contains(event->pos()))
    {
        Q_EMIT deleted();
    }
   else{
       slideCloseWidget(false);
       Q_EMIT activated();
    }
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

void PlaylistWidget::on_butPlayWidget_pressed()
{
    Q_EMIT started();
}

// esc key for exit close
void PlaylistWidget::keyPressEvent(QKeyEvent *e)
{
  if( e->key() == Qt::Key_Escape )
      slideCloseWidget(false);
   else
      QWidget::keyPressEvent( e );
}

void PlaylistWidget::on_pushClose_clicked()
{
    slideCloseWidget( (ui->widgetClose->minimumWidth()<50) );
}

void PlaylistWidget::slideCloseWidget(bool open)
{
    p->targetWidth = (open) ? 70 : 0;
    p->timerSlide->start();
}

void PlaylistWidget::timerSlide_timeOut()
{
    int mWidth = ui->widgetClose->minimumWidth();
    if ( p->targetWidth > mWidth ){
        ui->widgetClose->setMinimumWidth(mWidth+5);
        ui->widgetClose->setMaximumWidth(mWidth+5);
    }
    else if ( p->targetWidth < mWidth ){
        ui->widgetClose->setMinimumWidth(mWidth-5);
        ui->widgetClose->setMaximumWidth(mWidth-5);
    }
    else{
        p->timerSlide->stop();
    }
}
