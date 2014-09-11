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

#include "djfilterwidget.h"
#include "ui_djfilterwidget.h"
#include <qfiledialog.h>

#include <qdebug.h>
#include <QTimer>
#include <QMouseEvent>

struct DjFilterWidgetPrivate
{
        Filter* filter;
        QTimer* timerSlide;
        int targetWidth;
};

DjFilterWidget::DjFilterWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DjFilterWidget)
    ,p(new DjFilterWidgetPrivate)

{
    setFocusPolicy(Qt::ClickFocus);
    ui->setupUi(this);
    ui->ledActive->setLook(QLed::Flat);
    ui->ledActive->setShape(QLed::Rectangular);
    ui->ledActive->setColor(QColor(35,119,246));
    ui->ledActive->off();
    ui->txtPath->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->cmbGenres->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->cmbArtists->setAttribute(Qt::WA_MacShowFocusRect, false);
#if defined(Q_OS_DARWIN)
    ui->cmbArtists->setStyleSheet("QComboBox { margin: 0 3 0 3;}");
    ui->cmbGenres->setStyleSheet("QComboBox { margin: 0 3 0 3;}");
    ui->fraFilterTextBoxes->layout()->setContentsMargins(0,13,13,14);
    ui->fraFilterTextBoxes->layout()->setSpacing(-1);
#endif


    ui->lblFilterValue->setText( QString::null );
    ui->sliFilterValue->setValue( 0 );
    ui->stackDisplay->setCount( 0 );

    ui->stackDisplay->setBarColor(QColor( 196,196,210));
    ui->widgetClose->setMinimumWidth(0);
    ui->widgetClose->setMaximumWidth(0);

    timer = new QTimer( this );
    timer->stop();
    timer->setInterval(300);
    timer->setSingleShot(true);
    connect( timer,SIGNAL(timeout()),this,SLOT( slotSetFilter() ));

    p->timerSlide = new QTimer(this);
    p->timerSlide->setInterval(10);
    connect( p->timerSlide, SIGNAL(timeout()), SLOT(timerSlide_timeOut()) );
}


DjFilterWidget::~DjFilterWidget()
{
    delete ui;
    delete p;
}

void DjFilterWidget::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

void DjFilterWidget::mousePressEvent(QMouseEvent *event)
{

     if(ui->widgetClose->geometry().contains(event->pos()))
     {
         Q_EMIT deleted();
     }
    else{
        slideCloseWidget(false);
     }
}
void DjFilterWidget::slotSetFilter()
{
    qDebug() << Q_FUNC_INFO ;
    timer->stop();
    p->filter->setPath(ui->txtPath->text());
    p->filter->setGenre(ui->cmbGenres->currentText());
    p->filter->setArtist(ui->cmbArtists->currentText());
    p->filter->update();
}

void DjFilterWidget::setAllArtists(QStringList values)
{
    ui->cmbArtists->addItems(values );
}

void DjFilterWidget::setAllGenres(QStringList& values)
{
    ui->cmbGenres->addItems(values );
}

void DjFilterWidget::setID(QString value)
{
    ui->lblNumber->setText(value);
}

void DjFilterWidget::setFilter(Filter* filter)
{
    timer->stop();

    p->filter = filter;
    onFilterCountChanged();
    onFilterMaxUsageChanged();
    ui->txtPath->setText(p->filter->path());
    ui->cmbGenres->setEditText(p->filter->genre());
    ui->cmbArtists->setEditText(p->filter->artist());
    connect(p->filter,SIGNAL(statusChanged(bool)),
            this,SLOT(onFilterStatusChanged(bool)));
    connect(p->filter,SIGNAL(countChanged()),
            this,SLOT(onFilterCountChanged()));
    connect(p->filter,SIGNAL(usageChanged()),
            this,SLOT(onFilterUsageChanged()));
    connect(p->filter,SIGNAL(maxUsageChanged()),
            this,SLOT(onFilterMaxUsageChanged()));
}

Filter* DjFilterWidget::filter()
{
    return p->filter;
}

void DjFilterWidget::on_sliFilterValue_valueChanged(int value)
{
    ui->lblFilterValue->setText(QString("%1 %2").arg( value).arg(QString(tr("of"))));
    p->filter->setMaxUsage(ui->sliFilterValue->value());
}

void DjFilterWidget::on_txtPath_textChanged(QString )
{
    if ( timer->isActive() )
        timer->stop();
    timer->start();
}

void DjFilterWidget::on_cmbGenres_editTextChanged(QString )
{
    if ( timer->isActive() )
        timer->stop();
    timer->start();
}

void DjFilterWidget::on_cmbArtists_editTextChanged(QString )
{
    if ( timer->isActive() )
        timer->stop();
    timer->start();
}
void DjFilterWidget::on_pushActivate_clicked()
{
    p->filter->setActive(true);
}

void DjFilterWidget::onFilterStatusChanged(bool b)
{
    if (b)
        ui->ledActive->on();
    else {
        ui->ledActive->off();
        ui->stackDisplay->setSelected(-1);
    }
}

void DjFilterWidget::onFilterCountChanged()
{
    ui->lblCount->setText( QString("%1").arg( p->filter->count()));
}

void DjFilterWidget::onFilterMaxUsageChanged()
{
    ui->lblFilterValue->setText(QString("%1 %2").arg( p->filter->maxUsage()).arg(tr("of")));
    ui->sliFilterValue->setValue(p->filter->maxUsage());
    ui->stackDisplay->setCount(p->filter->maxUsage());
}

void DjFilterWidget::onFilterUsageChanged()
{
    ui->stackDisplay->setCount( p->filter->maxUsage());
    ui->stackDisplay->setSelected(p->filter->usage());
}


void DjFilterWidget::on_lbl1_linkActivated(const QString &link)
{
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::DirectoryOnly);
    if (dialog.exec())
         ui->txtPath->setText(dialog.selectedFiles().first());
}

// esc key for exit close
void DjFilterWidget::keyPressEvent(QKeyEvent *e)
{
  if( e->key() == Qt::Key_Escape )
      slideCloseWidget(false);
   else
      QWidget::keyPressEvent( e );
}

void DjFilterWidget::on_pushClose_clicked()
{
    slideCloseWidget( (ui->widgetClose->minimumWidth()<50) );
}

void DjFilterWidget::slideCloseWidget(bool open)
{
    p->targetWidth = (open) ? 70 : 0;
    p->timerSlide->start();
}

void DjFilterWidget::timerSlide_timeOut()
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
