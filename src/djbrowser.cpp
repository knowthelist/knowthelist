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
#include "collectiondb.h"

#include <QSplitter>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

class DjBrowserPrivate
{
    public:
    QSplitter* splitter;
    QListWidget* listDjs;
    QListWidget* listDjFilters;
    Dj* currentDj;
    DjWidget* currentDjw;
    QStringList allGenres;
    QStringList allArtists;
    CollectionDB* database;
};

DjBrowser::DjBrowser(QWidget *parent) :
    QWidget(parent)
{
    p = new DjBrowserPrivate;
    p->database  = new CollectionDB();

    QList<QStringList> tags = p->database->selectArtists();
    p->allArtists.append( QString::null );
    foreach ( QStringList tag, tags)
        p->allArtists.append( tag[0] );

    tags = p->database->selectGenres();
    p->allGenres.append( QString::null );
    foreach ( QStringList tag, tags)
        p->allGenres.append( tag[0] );

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
    connect( pushAddFilter,SIGNAL(clicked()),this, SLOT(addFilter()));

    p->listDjFilters = new QListWidget();
    p->listDjs = new QListWidget();
    p->listDjFilters->setAttribute(Qt::WA_MacShowFocusRect, false);
    p->listDjs->setAttribute(Qt::WA_MacShowFocusRect, false);
    p->listDjs->setItemSelected(p->listDjs->currentItem(),false);

    QVBoxLayout *mainLayout = new QVBoxLayout;

    QVBoxLayout *headWidgetLeftLayout = new QVBoxLayout;
    headWidgetLeftLayout->setMargin(0);
    headWidgetLeftLayout->setSpacing(1);
    headWidgetLeftLayout->setAlignment(Qt::AlignRight);
    headWidgetLeftLayout->addWidget(pushAddDj);

    QWidget *headWidgetLeft = new QWidget(this);
    headWidgetLeft->setMaximumHeight(20);
    headWidgetLeft->setMinimumHeight(20);
    headWidgetLeft->setLayout(headWidgetLeftLayout);

    QVBoxLayout *headWidgetRightLayout = new QVBoxLayout;
    headWidgetRightLayout->setMargin(0);
    headWidgetRightLayout->setSpacing(1);
    headWidgetRightLayout->setAlignment(Qt::AlignRight);
    headWidgetRightLayout->addWidget(pushAddFilter);

    QWidget *headWidgetRight = new QWidget(this);
    headWidgetRight->setMaximumHeight(20);
    headWidgetRight->setMinimumHeight(20);
    headWidgetRight->setLayout(headWidgetRightLayout);

    QVBoxLayout *widgetLeftLayout = new QVBoxLayout;
    widgetLeftLayout->setMargin(0);
    widgetLeftLayout->setSpacing(1);
    widgetLeftLayout->setAlignment(Qt::AlignRight);
    widgetLeftLayout->addWidget(headWidgetLeft);
    widgetLeftLayout->addWidget(p->listDjs);

    QVBoxLayout *widgetRightLayout = new QVBoxLayout;
    widgetRightLayout->setMargin(0);
    widgetRightLayout->setSpacing(1);
    widgetRightLayout->setAlignment(Qt::AlignRight);
    widgetRightLayout->addWidget(headWidgetRight);
    widgetRightLayout->addWidget(p->listDjFilters);

    QWidget *widgetLeft = new QWidget(this);
    widgetLeft->setLayout(widgetLeftLayout);

    QWidget *widgetRight = new QWidget(this);
    widgetRight->setLayout(widgetRightLayout);

    p->splitter = new QSplitter();
    p->splitter->addWidget(widgetLeft);
    p->splitter->addWidget(widgetRight);

    mainLayout->addWidget(p->splitter);

    mainLayout->setMargin(0);
    mainLayout->setSpacing(0);

    setLayout(mainLayout);

    QSettings settings;
    p->splitter->setStretchFactor(0, 5);
    p->splitter->setStretchFactor(1, 9);
    p->splitter->restoreState(settings.value("SplitterDjBrowser").toByteArray());

    //ToDo: restore last DJ from settings
}

DjBrowser::~DjBrowser()
{
    saveSettings();

    delete p;
}

void DjBrowser::saveSettings()
{
    QSettings settings;
    settings.setValue("SplitterDjBrowser",p->splitter->saveState());

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

            // DJs
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
                     if (countFilter==0) countFilter=1;
                     settings.beginGroup("Filter");
                     for (int i=0;i<countFilter;i++)
                     {
                         settings.beginGroup(QString::number(i));
                           Filter* f = new Filter();
                           dj->addFilter(f);

                           f->setPath(settings.value("Path","").toString());
                           f->setGenre(settings.value("Genre","").toString());
                           f->setArtist(settings.value("Artist","").toString());
                           f->setMaxUsage(settings.value("Value","4").toInt());
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
    qDebug() << Q_FUNC_INFO ;
    loadDj();
    Q_EMIT selectionStarted();

}

void DjBrowser::loadDj()
{
    //Fill Filter Widget List
    qDebug() << Q_FUNC_INFO ;

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
        if (dj->filters().count()==0) {
            //last filter has been removed
            Filter* f = new Filter();
            f->setMaxUsage(4);
            dj->addFilter(f);
        }
        qDebug() << Q_FUNC_INFO<< "name="<<dj->name << "filters="<< dj->filters().count();
        for (int i=0;i<dj->filters().count();i++)
        {

                           djfw  = new DjFilterWidget(p->listDjFilters);
                           djfw->setID(QString::number(i+1));
                           djfw->setAllGenres( p->allGenres );
                           djfw->setAllArtists( p->allArtists );
                           djfw->setFilter( dj->filters().at(i) );
                           itm = new QListWidgetItem(p->listDjFilters);
                           itm->setSizeHint(QSize(0,75));
                           p->listDjFilters->addItem(itm);
                           p->listDjFilters->setItemWidget(itm,djfw);
                           dj->filters().at(i)->update();
                           connect(djfw,SIGNAL(deleted()),this,SLOT(removeFilter()));
        }

        dj->setActiveFilterIdx(0);
        p->currentDj=dj;
        p->currentDjw=djWidget;

    }
    // give a chance to update the labels
    qApp->processEvents();
}

void DjBrowser::addFilter()
{
    Filter* f = new Filter();
    f->setMaxUsage(4);
    p->currentDj->addFilter(f);
    p->currentDjw->clicked();
}

void DjBrowser::removeFilter()
{
    if( DjFilterWidget* fw = qobject_cast<DjFilterWidget*>(QObject::sender()) ){

        // search the Filter which to remove
        for (int d=0;d<p->listDjFilters->count();d++){
            if ((DjFilterWidget*)p->listDjFilters->itemWidget(p->listDjFilters->item(d)) == fw ){

                p->currentDj->removeFilter(fw->filter());

                p->listDjFilters->removeItemWidget(p->listDjFilters->item(d));
                delete p->listDjFilters->item(d);
            }
        }
        saveSettings();
        p->currentDjw->clicked();
    }
}
