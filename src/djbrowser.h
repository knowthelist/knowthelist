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

#ifndef DJBROWSER_H
#define DJBROWSER_H

#include "dj.h"

#include <QWidget>
#include <QtGui>

class DjBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit DjBrowser(QWidget *parent = 0);
    ~DjBrowser();
    void updateList();
    void saveSettings();
    
signals:
    void selectionChanged(Dj*);
    void selectionStarted();
    
public slots:
    void addDj();
    void loadDj();
    void removeDj();
    void startDj();

private:
    class DjBrowserPrivate *p;
    
};

#endif // DJBROWSER_H
