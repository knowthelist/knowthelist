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

#include "djwidget.h"
#include "ui_djwidget.h"

#include <qdebug.h>
#include <QSettings>;
#include <QTimer>;
#include <QMouseEvent>;

struct DjWidgetPrivate
{
        Dj* dj;
        bool isActive;
        QTimer* timerSlide;
        int targetWidth;
};

DjWidget::DjWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DjWidget)
   ,p(new DjWidgetPrivate)
{
    setFocusPolicy(Qt::ClickFocus);
    ui->setupUi(this);
    ui->lblDesciption->setText( QString::null );
    ui->widgetClose->setMinimumWidth(0);
    ui->widgetClose->setMaximumWidth(0);

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

DjWidget::~DjWidget()
{
    delete ui;
    delete p;
}

void DjWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    switch (event->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}


void DjWidget::mousePressEvent(QMouseEvent *event)
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

void DjWidget::setDj(Dj* dj)
{
    qDebug() << __PRETTY_FUNCTION__ ;
    p->dj = dj;

    connect(p->dj,SIGNAL(countChanged()),this,SLOT(updateView()));
    deactivateDJ();

}

Dj* DjWidget::dj()
{
    return p->dj;
}

// auto connect slot
void DjWidget::on_lblName_linkActivated(const QString &link)
{
    Q_UNUSED(link);
    Q_EMIT activated();
}

void DjWidget::clicked()
{
    Q_EMIT activated();
}

void DjWidget::activateDJ()
{
    p->isActive = true;
    updateView();
}

void DjWidget::deactivateDJ()
{
    p->isActive = false;
    updateView();
}

void DjWidget::updateView()
{
    // Filter description and count update

    // update Labels
    setToolTip( tr("This Dj plays: ") + p->dj->description() );
    QString strCase = (p->dj->filters().count() > 1) ? tr("cases") : tr("case");

    ui->lblDesciption->setText( QString::number( p->dj->filters().count() ) + " " + strCase + "    "
                          + QString::number( p->dj->countTracks() ) + " " + tr("tracks") + "    "
                          + Track::prettyTime( p->dj->lengthTracks() ,true) + " " + tr("hours"));



    // active/passive look differentiation
    QString activeStyle;
    if (p->isActive){
       activeStyle = "color:#ff6464;'>";
       this->setStyleSheet("#frameDj{	background: qlineargradient("
                           "x1:0, y1:0, x2:0, y2:1,"
                           "stop: 0.01 #202020,"
                            "stop:0.11 #505050,"
                           "stop:1 #505050"
                           ");}");
    }
    else
    {
       activeStyle = "color:#eeeeee;'>";
       this->setStyleSheet("#frameDj{		background: qlineargradient("
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
                         + p->dj->name +
                         "</span></a></p></body></html>");
}

// esc key for exit close
void DjWidget::keyPressEvent(QKeyEvent *e)
{
  if( e->key() == Qt::Key_Escape )
      slideCloseWidget(false);
   else
      QWidget::keyPressEvent( e );
}

void DjWidget::on_butPlayWidget_pressed()
{
    Q_EMIT started();
}

void DjWidget::on_pushClose_clicked()
{
    slideCloseWidget( (ui->widgetClose->minimumWidth()<50) );
}

void DjWidget::slideCloseWidget(bool open)
{
    p->targetWidth = (open) ? 70 : 0;
    p->timerSlide->start();
}

void DjWidget::timerSlide_timeOut()
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
