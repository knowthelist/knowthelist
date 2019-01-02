/*
    Copyright (C) 2005-2019 Mario Stephan <mstephan@shared-files.de>

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
 
#include "vumeter.h"
#include <QWidget>
#include <QStyleOption>
#include <QPainter>

VUMeter::VUMeter(QWidget *parent)
 : QWidget(parent)
{
    setOrientation( Qt::Vertical );

    BackgroundColor.setRgb ( 40,40,40 );
    LevelColorNormal.setRgb ( 0,200,0 );
    LevelColorHigh.setRgb ( 200,0,0 );
    LevelColorOff.setRgb(80,120,120);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, BackgroundColor);
    setPalette(pal);
    valueLeft= valueRight = 0;
    peakLeft = peakRight = 0;
    margin = 1;
    linesPerSegment = 1;
    spacesBetweenSegments = 1;
    step = linesPerSegment + spacesBetweenSegments;
    spacesInSegments = 2;
    spacesInPeak = 2;
    linesPerPeak = 4;
    colBack = QColor(60, 60, 60);
    colValue = Qt::white;
}



VUMeter::~VUMeter()
{
}

void VUMeter:: setOrientation( Qt::Orientation o)
{
    orient = o;
}

void VUMeter::reset()
{
    peakLeft = peakRight = 0;
    valueLeft = valueRight = 0;
    update();
}

void VUMeter::resizeEvent( QResizeEvent* e )
{   
    Q_UNUSED(e);

     w = (orientation() == Qt::Vertical) ? width() : height();
    ledWidth = ((w - 3*margin)/2);
    
    h = (((orientation() == Qt::Vertical) ? height() : width() ) / (linesPerSegment + step)) * (linesPerSegment + step) +margin ;
}

void VUMeter::checkPeakTime() {
    if ( peakTime.elapsed() >= 1000 ) {
        peakLeft = 0;
        peakRight = 0;
        peakTime.restart();
        update();
    }
}

void VUMeter::setValueLeft ( float f )
{
    float fl = f;

    if (fl<0.f) fl= 0.f;
        
    if ( fl > peakLeft ) {
        peakLeft = fl;
        peakTime.start();
    }

    if (valueLeft != fl){
        valueLeft = fl;
        if (valueLeft==0)
            peakLeft=0;
        update();
        checkPeakTime();
    }
}

void VUMeter::setPercentage( float f )
{
    //qDebug() << "Level: "<< f;
    valueRight = valueLeft = f ;
    update();
}

void VUMeter::setValueRight ( float f )
{
    float fr = f;

    if (fr<0.f) fr= 0.f;
    
    if ( fr > peakRight ) {
        peakRight = fr;
        peakTime.start();
    }

    if (valueRight != fr ){
        valueRight = fr ;
        if (valueRight==0)
            peakRight=0;
        update();
        checkPeakTime();
    }
}

void VUMeter::setLinesPerSegment( int i ){
    linesPerSegment = i;
    step = linesPerSegment + spacesBetweenSegments;
}

void VUMeter::setSpacesBetweenSegments( int i ) {
    spacesBetweenSegments = i;
    step = linesPerSegment + spacesBetweenSegments;
}

void VUMeter::setSpacesInSegments( int i ) {
    spacesInSegments = i-1;
    if ( spacesInSegments<1 ) spacesInSegments=1;
}

void VUMeter::setLinesPerPeak( int i ){
    linesPerPeak = i;
}


void VUMeter::setSpacesInPeak( int i ) {
    spacesInPeak = i-1;
    if ( spacesInPeak<1 ) spacesInPeak=1;
}

void VUMeter::setMargin( int i ){
    margin = i;
}

void VUMeter::paintEvent(QPaintEvent *)
{
    drawMeter();
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}


void VUMeter::drawMeter() {
    
    QPainter painter(this);

    int value = 0;
    int peak = 0;
    int x1, x2;

    for ( int i=1;i<3;i++ ) {


        //Left value
        if ( i==1 ) {
            value = (int)(h/step * valueLeft)*step;
            peak = (int)(h/step * peakLeft)*step;
            x1 = margin;
            x2 = margin + ledWidth;
        }
        //Right value
        else {
            value = (int)(h/step * valueRight)*step;
            peak = (int)(h/step * peakRight)*step;
            x1 = w - margin - ledWidth;
            x2 = w - margin;
        }

        for( int sec=0; sec < h+margin; sec+=step ){
            if ( sec < value && (sec <= .75*h) )
                painter.setPen ( LevelColorNormal );
            else if ( sec < value && (sec > .75 * h)  )
                painter.setPen ( LevelColorHigh );
            else
                painter.setPen ( LevelColorOff );



            if ( orientation() == Qt::Vertical )
                for ( int led=0; led<linesPerSegment; led+=spacesInSegments )
                    painter.drawLine ( x1, h-(sec+led)+margin, x2, h-(sec+led)+margin );
            else
                for ( int led=0; led<linesPerSegment; led+=spacesInSegments )
                    painter.drawLine ( sec+led+margin, x1, sec+led+margin, x2 );
        }

        //peaks
        if ( peak ) {

            if ( peak >= .75*h )
            painter.setPen ( LevelColorHigh );
            else
            painter.setPen ( LevelColorNormal );
            if ( orientation() == Qt::Vertical ){
                 for (int led=0; led<linesPerPeak; led+=spacesInPeak)
                     painter.drawLine ( x1, h-(peak+led)+margin, x2, h-(peak+led)+margin );}
            else
                 for (int led=0; led<linesPerPeak; led+=spacesInPeak)
                    painter.drawLine ( peak+led+margin, x1, peak+led+margin, x2 );
       }
    }

}

