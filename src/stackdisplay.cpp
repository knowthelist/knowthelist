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

#include "stackdisplay.h"

#include <QColor>
#include <QStyleOption>
#include <QPainter>


class StackDisplayPrivate
{
    public:
        int height;
        int width;
        int margin;
        int count;
        int indexSelected;
        int barHeight;
        QColor colorBars;
};

StackDisplay::StackDisplay(QWidget *parent) :
    QWidget(parent)
{
    p = new StackDisplayPrivate;

    p->colorBars.setRgb ( 200,200,200 );

    p->count = 6;
    p->indexSelected = 1;
    p->barHeight = 3;
    p->margin = 8;
}



StackDisplay::~StackDisplay()
{
    delete p;
}

void StackDisplay::resizeEvent( QResizeEvent* e )
{
    Q_UNUSED(e);

     p->width = width();
     p->height = height();
}

void StackDisplay::setBarColor ( QColor color )
{
    p->colorBars = color;
    update();
}


void StackDisplay::setCount ( int value )
{
    p->count = value;
    update();
}

void StackDisplay::setSelected( int value )
{
    p->indexSelected = value;
    update();
}

void StackDisplay::setMargin( int value)
{
    p->margin = value;
}

void StackDisplay::paintEvent(QPaintEvent *)
{
    drawBars();
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void StackDisplay::drawBars() {

    QPainter painter(this);
    painter.setPen ( p->colorBars );

    if (p->count==0)
        return;

    int space = (int)(p->height/p->count);
    int posY = 0;

    for ( int i=0;i<p->count;i++ ) {
        posY = posY + space;
        for ( int j=0;j<p->barHeight;j++ ) {
            //draw bars
            painter.drawLine (p->margin*2,
                              posY+j-p->barHeight,
                              p->width-p->margin,
                              posY+j-p->barHeight);
        }
        if (i==p->indexSelected){
            //draw triancle
            for ( int j=p->margin;j<p->margin+5;j++ ) {
                painter.drawLine (j,
                                  posY-(p->margin+5-j)+1-p->barHeight,
                                  j,
                                  posY+(p->margin+5-j)-1-p->barHeight);
            }
        }


}

}


