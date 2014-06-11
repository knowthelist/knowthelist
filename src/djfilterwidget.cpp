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


struct DjFilterWidget::Private
{

        Filter* filter;

};

DjFilterWidget::DjFilterWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DjFilterWidget)
    ,p(new Private)

{
    ui->setupUi(this);
    ui->ledActive->setLook(QLed::Flat);
    ui->ledActive->setShape(QLed::Rectangular);
    ui->ledActive->setColor(QColor(35,119,246));
    ui->ledActive->off();
    ui->txtPath->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->txtGenre->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->txtArtist->setAttribute(Qt::WA_MacShowFocusRect, false);


    ui->stackDisplay->setBarColor(QColor( 196,196,210));


    timer = new QTimer( this );
    timer->stop();
}



DjFilterWidget::~DjFilterWidget()
{
    delete ui;
    //delete timer;
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

void DjFilterWidget::slotSetFilter()
{
    qDebug() << __PRETTY_FUNCTION__ ;
    timer->stop();
    p->filter->setPath(ui->txtPath->text());
    p->filter->setGenre(ui->txtGenre->text());
    p->filter->setArtist(ui->txtArtist->text());
    p->filter->update();
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
    ui->txtGenre->setText(p->filter->genre());
    ui->txtArtist->setText(p->filter->artist());
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
    if ( timer->isActive() ) timer->stop();
        timer->singleShot(500,this, SLOT( slotSetFilter() ));
}

void DjFilterWidget::on_txtGenre_textChanged(QString )
{
    if ( timer->isActive() ) timer->stop();
        timer->singleShot(500,this, SLOT( slotSetFilter() ));
}

void DjFilterWidget::on_txtArtist_textChanged(QString )
{
    if ( timer->isActive() ) timer->stop();
        timer->singleShot(500,this, SLOT( slotSetFilter() ));
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
