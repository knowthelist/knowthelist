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

#include "filter.h"
#include <QtCore>



struct Filter::Private
{
        int maxUsage;
        int usage;
        bool isActive;
        QString path;
        QString genre;
        QString artist;
        int count;
        long length;
};

/*
 *
 *ToDo: - replace path with a string from Tag (if we have a good tagger tool inside)
 **/

Filter::Filter()
    :p(new Private)
{
    p->usage = 0;
    p->maxUsage = 0;
    p->count = 0;
    p->length = 0;
    p->path = "",
    p->genre = "";
    p->artist = "";
    p->isActive = false;
}

Filter::Filter(QString genre, int maxUsage)
    :p(new Private)
{
    p->maxUsage = maxUsage;
    p->genre = genre;
}

Filter::~Filter()
{
    delete p;
}


int Filter::maxUsage()
{
    return p->maxUsage;
}

void Filter::setMaxUsage(int value)
{
    p->maxUsage = value;
    emit maxUsageChanged();
}

bool Filter::active()
{
    return p->isActive;
}

void Filter::setActive(bool b)
{
    p->isActive = b;
    Q_EMIT statusChanged(b);
    if ( b == true )
    {
        setUsage(0);
        Q_EMIT activated();
    }
}

void Filter::update()
{
    //qDebug() << Q_FUNC_INFO ;
    Q_EMIT filterChanged();
}

QString Filter::path()
{
    return p->path;
}

void Filter::setPath(QString path)
{
    p->path = path;
}

QString Filter::genre()
{
    return p->genre;
}

void Filter::setGenre(QString genre)
{
    p->genre = genre;
}

QString Filter::artist()
{
    return p->artist;
}

void Filter::setArtist(QString artist)
{
    p->artist = artist;
}

int Filter::usage()
{
    return p->usage;
}

void Filter::setUsage(int value)
{
    p->usage = value;
    emit usageChanged();
}

int Filter::count()
{
    return p->count;
}

void Filter::setCount(int value)
{
    p->count = value;
    emit countChanged();
}

long Filter::length()
{
    return p->length;
}

void Filter::setLength(long value)
{
    p->length = value;
}

QString Filter::description()
{
    QString ret;
    if ( !p->artist.isEmpty())
        ret+=p->artist +" ";
    if ( !p->genre.isEmpty())
        ret+=p->genre +" ";
    if ( !p->path.isEmpty())
    {
        QStringList token = p->path.split("/");
        ret+=token.last() +" ";

    }
    return  ret;
}
