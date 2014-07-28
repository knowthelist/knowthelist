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

#include "djbrowser.h"
#include "dj.h"
#include "djwidget.h"
#include "djfilterwidget.h"

#include <Qt>

class DjBrowserPrivate
{
    public:
    QListWidget* listDjs;
    QListWidget* listDjFilters;


};

DjBrowser::DjBrowser(QWidget *parent) :
    QWidget(parent)
{
    p = new DjBrowserPrivate;

    QPushButton* pushAddDj =new QPushButton();
    pushAddDj->setGeometry(QRect(1,1,60,25));
    pushAddDj->setMaximumWidth(60);
    pushAddDj->setMinimumWidth(60);
    pushAddDj->setText("+");
    QFont pushFont = pushAddDj->font();
    pushFont.setBold(true);
    pushFont.setPointSize(pushFont.pointSize()+4);
    pushAddDj->setFont(pushFont);

    pushAddDj->setStyleSheet("QPushButton { border: none; padding-top: -3px; margin-left:8px;max-height: 20px; margin-right: 28px;}");
    pushAddDj->setToolTip(tr( "Add a new AutoDj" ));
    connect( pushAddDj,SIGNAL(clicked()),this, SLOT(addDj()));

    QPushButton* pushAddFilter =new QPushButton();
    pushAddFilter->setGeometry(QRect(1,1,60,25));
    pushAddFilter->setMaximumWidth(60);
    pushAddFilter->setMinimumWidth(60);
    pushAddFilter->setText("+");
    pushAddFilter->setFont(pushFont);

    pushAddFilter->setStyleSheet("QPushButton { border: none; padding-top: -3px; margin-left: 8px;max-height: 20px; margin-right: 28px;}");
    pushAddFilter->setToolTip(tr( "Add a new record case for current AutoDj" ));
    connect( pushAddFilter,SIGNAL(clicked()),this, SLOT(pushAddFilter()));

    QVBoxLayout *mainLayout = new QVBoxLayout;
    QWidget *headWidget = new QWidget(this);
    headWidget->setMaximumHeight(35);
    headWidget->setMinimumHeight(25);

    QHBoxLayout *headWidgetLayout = new QHBoxLayout;
    headWidgetLayout->setMargin(0);
    headWidgetLayout->setSpacing(1);
    headWidgetLayout->setAlignment(Qt::AlignLeft);


    headWidgetLayout->addSpacing(width()*.47);
    headWidgetLayout->addWidget(pushAddDj);
    headWidgetLayout->addSpacing(width()*.95);
//    headWidgetLayout->addWidget(pushAddFilter);


    headWidget->setLayout(headWidgetLayout);

    p->listDjFilters = new QListWidget();
    p->listDjs = new QListWidget();
    p->listDjFilters->setAttribute(Qt::WA_MacShowFocusRect, false);
    p->listDjs->setAttribute(Qt::WA_MacShowFocusRect, false);
    p->listDjs->setMaximumWidth(width()*.22);
    p->listDjs->setItemSelected(p->listDjs->currentItem(),false);

    QWidget *djBox = new QWidget(this);
    QHBoxLayout *layout = new QHBoxLayout;
    layout->setMargin(0);
    layout->setSpacing(1);
    p->listDjs->setMaximumWidth(350);
    layout->addWidget(p->listDjs);
    layout->addWidget(p->listDjFilters);
    djBox->setLayout(layout);

    headWidget->raise();

    mainLayout->addWidget(headWidget);
    mainLayout->addWidget(djBox);


    mainLayout->setMargin(0);
    mainLayout->setSpacing(0);

    setLayout(mainLayout);
}

DjBrowser::~DjBrowser()
{
    saveSettings();

    delete p;
}

void DjBrowser::saveSettings()
{
    QSettings settings;
    settings.setValue("countDJ",p->listDjs->count());
    settings.remove("AutoDJ");
    settings.beginGroup("AutoDJ");
    for (int d=0;d<p->listDjs->count();d++)
    {
        settings.beginGroup(QString::number(d));
        Dj* dj = ((DjWidget*)p->listDjs->itemWidget(p->listDjs->item(d)))->dj();
        settings.setValue("Name", dj->name );
        settings.setValue("FilterCount", dj->filters().count() );

        QList<Filter*> f=dj->filters();

        settings.beginGroup("Filter");
        for (int i=0; i<f.count();i++)
        {
             settings.beginGroup(QString::number(i));
             settings.setValue("Path",f.at(i)->path());
             settings.setValue("Genre",f.at(i)->genre());
             settings.setValue("Artist",f.at(i)->artist());
             settings.setValue("Value",QString::number(f.at(i)->maxUsage()));
             settings.endGroup();
        }
        settings.endGroup();
        settings.endGroup();
    }
    settings.endGroup();

}

void DjBrowser::updateList()
{
    QSettings settings;

    //Dj Widget List
    p->listDjs->clear();
    p->listDjs->setSelectionMode(QAbstractItemView::ExtendedSelection);


    DjWidget* djw;
    QListWidgetItem* itm;
    Dj* dj;
    int maxDj=settings.value("countDJ","3").toInt();
    if (maxDj==0) maxDj=1;

    settings.beginGroup("AutoDJ");

            // Filters
            for (int d=0;d<maxDj;d++)
            {
                settings.beginGroup(QString::number(d));
                    dj = new Dj();
                    dj->name = settings.value("Name","Dj%1").toString().arg(d+1);

                    djw  = new DjWidget(p->listDjs);

                    connect(djw,SIGNAL(activated()),this,SLOT(loadDj()));
                    connect(djw,SIGNAL(deleted()),this,SLOT(removeDj()));
                    connect(djw,SIGNAL(started()),this,SLOT(startDj()));

                     itm = new QListWidgetItem(p->listDjs);
                     itm->setSizeHint(QSize(0,75));
                     p->listDjs->addItem(itm);
                     p->listDjs->setItemWidget(itm,djw);



                     // Filters
                     int countFilter = settings.value("FilterCount","2").toInt();
                     settings.beginGroup("Filter");
                     for (int i=0;i<countFilter;i++)
                     {
                         settings.beginGroup(QString::number(i));
                           Filter* f = new Filter();
                           dj->addFilter(f);

                           f->setPath(settings.value("Path","").toString());
                           f->setGenre(settings.value("Genre","").toString());
                           f->setArtist(settings.value("Artist","").toString());
                           f->setMaxUsage(settings.value("Value","2").toInt());
                         settings.endGroup();

                         f->setUsage(0);
                     }

                    djw->setDj(dj);
                    // force sum update
                    djw->clicked();

                    settings.endGroup();
                    dj->setActiveFilterIdx(settings.value("currentDjActiveFilter","0").toInt());
                    settings.endGroup();
            }
            settings.endGroup();
    p->listDjs->setCurrentRow(0);

    DjWidget* djWidget = (DjWidget*)p->listDjs->itemWidget(p->listDjs->currentItem());
    djWidget->activateDJ();
    djWidget->clicked();
    p->listDjs->setItemSelected(p->listDjs->currentItem(),false);
    p->listDjs->setSelectionMode(QAbstractItemView::NoSelection);

}

void DjBrowser::addDj()
{
    QSettings settings;
    settings.setValue("countDJ",p->listDjs->count()+1);
    updateList();
}

void DjBrowser::removeDj()
{
    if(DjWidget* djWidget = qobject_cast<DjWidget*>(QObject::sender())){

        // search the Dj which to remove
        for (int d=0;d<p->listDjs->count();d++){
            if ((DjWidget*)p->listDjs->itemWidget(p->listDjs->item(d)) == djWidget){
                p->listDjs->removeItemWidget(p->listDjs->item(d));
                delete p->listDjs->item(d);
                //delete djWidget;
            }
        }
        saveSettings();
        updateList();
        Dj* dj=((DjWidget*)p->listDjs->itemWidget(p->listDjs->item(0)))->dj();
        Q_EMIT selectionChanged(dj);
    }
}

void DjBrowser::startDj()
{
    qDebug() << __PRETTY_FUNCTION__ ;
    loadDj();
    Q_EMIT selectionStarted();

}

void DjBrowser::loadDj()
{
    //Fill Filter Widget List
    qDebug() << __PRETTY_FUNCTION__ ;

    p->listDjFilters->clear();

    DjFilterWidget *djfw;
    QListWidgetItem * itm;

    // deactivate all Djs
    for (int d=0;d<p->listDjs->count();d++)
        ((DjWidget*)p->listDjs->itemWidget(p->listDjs->item(d)))->deactivateDJ();


    // Activate current selected DJ
    if(DjWidget* djWidget = qobject_cast<DjWidget*>(QObject::sender())){

        djWidget->activateDJ();
        Dj* dj = djWidget->dj();
        Q_EMIT selectionChanged(dj);

         // Filters
        qDebug() << __PRETTY_FUNCTION__<< "name="<<dj->name << "filters="<<dj->filters().count() ;
        for (int i=0;i<dj->filters().count();i++)
        {

                           djfw  = new DjFilterWidget(p->listDjFilters);
                           djfw->setFilter( dj->filters().at(i) );
                           djfw->setID(QString::number(i+1));
                           itm = new QListWidgetItem(p->listDjFilters);
                           itm->setSizeHint(QSize(0,75));
                           p->listDjFilters->addItem(itm);
                           p->listDjFilters->setItemWidget(itm,djfw);
                           dj->filters().at(i)->update();
        }

        dj->setActiveFilterIdx(0);

    }

}
