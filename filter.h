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

#ifndef FILTER_H
#define FILTER_H

#include <QObject>

class Filter: public QObject
{

    Q_OBJECT

  public:

    explicit Filter();
    explicit Filter(QString genre, int grade);
    ~Filter();
    int maxUsage();
    void setMaxUsage(int);
    int usage();
    void setUsage(int);
    QString genre();
    QString path();
    QString artist();
    QString description();
    int count();
    long length();
    void setPath(QString);
    void setGenre(QString);
    void setArtist(QString);
    void setCount(int);
    void setLength(long);
    bool active();
    void setActive(bool);
    void update();

 Q_SIGNALS:
    void statusChanged(bool status);
    void countChanged();
    void usageChanged();
    void maxUsageChanged();
    void activated();
    void filterChanged();


public slots:

private:
    struct Private;
    Private * p;

};

#endif // FILTER_H
