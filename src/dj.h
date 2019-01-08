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

#ifndef DJ_H
#define DJ_H

#include "filter.h"

class Dj : public QObject {
    Q_OBJECT
public:
    Dj();
    ~Dj();
    QList<Filter*> filters();
    Filter* requestFilter();
    QString name;
    QString description();
    void setDescription(QString);
    long countTracks();
    void setCountTracks(int);
    long lengthTracks();
    void setLengthTracks(int);
    void addFilter(Filter* filter);
    void removeFilter(Filter* filter);
    void setActiveFilterIdx(int idx);
    int activeFilterIdx();

Q_SIGNALS:
    void filterChanged(Filter*);
    void countChanged();

private Q_SLOTS:
    void on_filter_activated();
    void on_filter_filterChanged();
    void on_filter_countChanged();
    void on_filter_maxUsageChanged();
    void checkSequence();

protected:
private:
    struct DjPrivate* p;
};

#endif // DJ_H
