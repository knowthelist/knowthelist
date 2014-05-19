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

struct DjWidget::Private
{
        Dj* dj;
        bool isActive;
};

DjWidget::DjWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DjWidget)
   ,p(new Private)
{
    ui->setupUi(this);
    connect(ui->lblName,SIGNAL(linkActivated(QString)),this,SLOT(on_lblName_linkActivated(QString)));
}

DjWidget::~DjWidget()
{
    delete ui;
}

void DjWidget::changeEvent(QEvent *e)
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

void DjWidget::setDj(Dj* dj)
{
    qDebug() << __PRETTY_FUNCTION__ ;
    p->dj = dj;

    connect(p->dj,SIGNAL(countChanged(Filter*)),this,SLOT(updateView()));
    deactivateDJ();

}

Dj* DjWidget::dj()
{
    return p->dj;
}


void DjWidget::on_lblName_linkActivated(const QString &link)
{
    qDebug() << __PRETTY_FUNCTION__ ;
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

    // retrieve values
    QString res;
    int sum = 0;
    long length = 0;
    for (int i=0;i<p->dj->filters().count();i++)
    {
        Filter* f=p->dj->filters().at(i);
        res+=f->description();
        sum+=f->count();
        length+=f->length();
        qDebug() << __PRETTY_FUNCTION__ <<f->description() << ":"<<f->count()<< ":"<<f->length();
    }

    // update Labels
    ui->lblDesciption->setText(res );
    ui->lblCount->setText(QString::number(sum) + " tracks");
    ui->lblLength->setText(Track::prettyTime(length,true) + " hours");

    // active/passive look differentiation
    QString activeStyle;
    if (p->isActive)
       activeStyle = "color:#ff6464;'>";
    else
       activeStyle = "color:#eeeeee;'>";

    ui->lblName->setText("<!DOCTYPE HTML PUBLIC '-//W3C//DTD HTML 4.0//EN' 'http://www.w3.org/TR/REC-html40/strict.dtd'>"
                         "<html><head><meta name='qrichtext' content='1' /><style type='text/css'>p, li { white-space: pre-wrap; }"
                         "</style></head><body style='font-size:8.25pt; font-weight:400; font-style:normal;'><p style=' margin-top:0px; "
                         "margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'>"
                         "<a href='fdadfaf'><span style=' font-size:12pt;font-style:normal;font-weight:bold; text-decoration: underline; "
                         + activeStyle
                         + p->dj->name +
                         "</span></a></p></body></html>");
}
