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


    ui->barRepeat->setOrientation( Qt::Vertical );
    ui->barRepeat->LevelColorNormal.setRgb( 35,119,246 );
    ui->barRepeat->LevelColorHigh.setRgb( 35,119,246 );
    ui->barRepeat->LevelColorOff.setRgb( 31,45,65 );
    //ui->barRepeat->setBackgroundColor( QColor(33,24,41) );
    ui->barRepeat->setSpacesBetweenSecments(0);
    ui->barRepeat->setLinesPerSecment(1);
    ui->barRepeat->setLinesPerPeak(1);
    ui->barRepeat->setSpacesInSecments(0);
    ui->barRepeat->setSpacesInPeak(0);
    ui->barRepeat->setMargin(0);

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

void DjFilterWidget::setFilter(Filter* filter)
{
    timer->stop();

    p->filter = filter;
    on_filter_countChanged();
    on_filter_maxUsageChanged();
    ui->txtPath->setText(p->filter->path());
    ui->txtGenre->setText(p->filter->genre());
    ui->txtArtist->setText(p->filter->artist());
    connect(p->filter,SIGNAL(statusChanged(bool)),
            this,SLOT(on_filter_statusChanged(bool)));
    connect(p->filter,SIGNAL(countChanged()),
            this,SLOT(on_filter_countChanged()));
    connect(p->filter,SIGNAL(usageChanged()),
            this,SLOT(on_filter_usageChanged()));
    connect(p->filter,SIGNAL(maxUsageChanged()),
            this,SLOT(on_filter_maxUsageChanged()));


}

Filter* DjFilterWidget::filter()
{
    return p->filter;
}

void DjFilterWidget::on_sliFilterValue_valueChanged(int value)
{
    ui->lblFilterValue->setText(QString("%1").arg( value));
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

void DjFilterWidget::on_filter_statusChanged(bool b)
{
    if (b)
        ui->ledActive->on();
    else {
        ui->ledActive->off();
        ui->barRepeat->setPercentage(0.f);
    }
}

void DjFilterWidget::on_filter_countChanged()
{
    ui->lblCount->setText( QString("%1").arg( p->filter->count()));
}

void DjFilterWidget::on_filter_maxUsageChanged()
{
    ui->lblFilterValue->setText(QString("%1").arg( p->filter->maxUsage()));
    ui->sliFilterValue->setValue(p->filter->maxUsage());
}

void DjFilterWidget::on_filter_usageChanged()
{
    ui->barRepeat->setPercentage((p->filter->usage() *1.0f) /p->filter->maxUsage());
}

void DjFilterWidget::on_toolButton_clicked()
{
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::DirectoryOnly);
    if (dialog.exec())
         ui->txtPath->setText(dialog.selectedFiles().first());
}
