/*
    Copyright (C) 2014 Mario Stephan <mstephan@shared-files.de>

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

#ifndef STACKDISPLAY_H
#define STACKDISPLAY_H

#include <QWidget>

class StackDisplay : public QWidget {
    Q_OBJECT
public:
    explicit StackDisplay(QWidget* parent = 0);
    ~StackDisplay();

    void setCount(int value);
    void setSelected(int value);
    void setMargin(int value);
    void setBarColor(QColor color);

signals:

public slots:

private:
    void resizeEvent(QResizeEvent* e);
    void paintEvent(QPaintEvent*);
    void drawBars();
    class StackDisplayPrivate* p;
};

#endif // STACKDISPLAY_H
