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

#ifndef DJWIDGET_H
#define DJWIDGET_H

#include <QWidget>
#include "dj.h"
#include "track.h"

namespace Ui {
    class DjWidget;
}

class DjWidget : public QWidget {
    Q_OBJECT
public:
    DjWidget(QWidget *parent = 0);
    ~DjWidget();

    void setDj(Dj *dj);
    Dj* dj();

public Q_SLOTS:
    void activateDJ();
    void deactivateDJ();
    void updateView();

Q_SIGNALS:
   void activated();
   void filterCountChanged(int count);


protected:
    void changeEvent(QEvent *e);

private slots:

    void on_lblName_linkActivated(const QString &link);

private:
    Ui::DjWidget *ui;
    struct Private;
    Private *p;
};

#endif // DJWIDGET_H
