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

#include "dj.h"
#include "filter.h"
#include <qdebug.h>
#include <qwaitcondition.h>
#include <qmutex.h>

struct DjPrivate
{
        int rotation;
        QList<Filter*> filters;
        Filter* filter;
        long countTracks;
        long lengthTracks;
        QString description;
};

Dj::Dj()
    :p(new DjPrivate)
{
    p->rotation=0;
    p->filter=0;
    p->description=QString::null;
}

Dj::~Dj()
{
    delete p;
}

void Dj::addFilter(Filter* filter)
{
    p->filters.append(filter);
    connect(filter,SIGNAL(activated()),
            this,SLOT(on_filter_activated()));
    connect(filter,SIGNAL(filterChanged()),
            this,SLOT(on_filter_filterChanged()));
//    connect(filter,SIGNAL(countChanged()),
//            this,SLOT(on_filter_countChanged()));
    connect(filter,SIGNAL(maxUsageChanged()),
            this,SLOT(on_filter_maxUsageChanged()));
}

QList<Filter*> Dj::filters()
{
    return p->filters;
}

void Dj::setActiveFilterIdx(int idx)
{
    p->rotation = idx;
    if ( p->rotation >= p->filters.count() )
        p->rotation = 0;
    p->filters[p->rotation]->setActive(true);
}

int Dj::activeFilterIdx()
{
    return p->rotation;
}

void Dj::on_filter_filterChanged()
{
    //qDebug() << __PRETTY_FUNCTION__ ;
    Filter* f = qobject_cast<Filter*>(QObject::sender());
    Q_EMIT filterChanged(f);
}

void Dj::on_filter_countChanged()
{
    //qDebug() << __PRETTY_FUNCTION__ ;
    //Filter* f = qobject_cast<Filter*>(QObject::sender());
    //Q_EMIT countChanged();
}

void Dj::on_filter_activated()
{
    Filter* f = qobject_cast<Filter*>(QObject::sender());
    QList<Filter*>::iterator i;
     for (i = p->filters.begin(); i != p->filters.end(); ++i)
    {
         if ( (*i) != f )
            (*i)->setActive(false);
         else
         {
             p->rotation = p->filters.indexOf((*i));
         }

     }
     //p->wc.wakeAll();
}

void Dj::on_filter_maxUsageChanged()
{
    Filter* f = qobject_cast<Filter*>(QObject::sender());
    if ( p->filter == f )
        checkSequence();

}

void Dj::checkSequence()
{

    qDebug() << __FUNCTION__ << "rotation=" << p->rotation << "/" << p->filters.count()
             << " repeat=" << p->filter->usage() << "/" <<  p->filter->maxUsage() ;

    if ( p->filter->usage() >= p->filter->maxUsage() )
    {
        int i = p->filters.count();
        do
        {
            p->rotation++;
            i--;
            if ( p->rotation >= p->filters.count() )
                p->rotation = 0;
        }
        while (p->filters.at(p->rotation)->maxUsage() == 0
               && i>0);

        p->filters.at(p->rotation)->setActive(true);

        // setActive goes twice through SIGNAL/SLOT and back here,
        // therefore a waitcondition is necessary to avoid race condition
        /*p->mutex.lock();
        p->wc.wait(&p->mutex);
        p->mutex.unlock();*/
    }

}

//ToDo: - Switch LED before add the Track
//      - Allow 0 filter (skip)
//      - DJwidget: Playbutton für start
Filter* Dj::requestFilter()
{
    p->filter = p->filters.at(p->rotation);

    p->filter->setUsage(p->filter->usage() +1);

    checkSequence();

    qDebug() << __FUNCTION__ << "rotation=" << p->rotation << "/" << p->filters.count()
             << " repeat=" << p->filter->usage() << "/" <<  p->filter->maxUsage() ;

    qDebug() << __FUNCTION__ << "return filter=" << p->filters.at(p->rotation)->path()
            << "/" << p->filters.at(p->rotation)->genre()
            << "/" << p->filters.at(p->rotation)->artist();

    return p->filter;
}

QString Dj::description()
{
    return p->description;
}

void Dj::setDescription(QString value)
{
    p->description = value;
}

int Dj::countTracks()
{
    return p->countTracks;
}

void Dj::setCountTracks(int value)
{
    p->countTracks = value;
    Q_EMIT countChanged();
}

int Dj::lengthTracks()
{
    return p->lengthTracks;
}

void Dj::setLengthTracks(int value)
{
    p->lengthTracks = value;
}
