/*
    Copyright (C) 2005-2011 Mario Stephan <mstephan@shared-files.de>

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
    m = 1;
    sh = 1;
    space = 1;
    step = sh + space;
    secStep = 2;
    pStep = 2;
    ph = 4;
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
    sw = ((w - 3*m)/2);
    
    h = (((orientation() == Qt::Vertical) ? height() : width() ) / (sh + step)) * (sh + step) +m ;
    
    //QWidget::resizeEvent( e );
    //repaint();
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
    sh = i;
    step = sh + space;
}

void VUMeter::setSpacesBetweenSegments( int i ) {
    space = i;
    step = sh + space;
}

void VUMeter::setSpacesInSegments( int i ) {
    secStep = i-1;
    if ( secStep<1 ) secStep=1;
}

void VUMeter::setLinesPerPeak( int i ){
    ph = i;
}


void VUMeter::setSpacesInPeak( int i ) {
    pStep = i-1;
    if ( pStep<1 ) pStep=1;
}

void VUMeter::setMargin( int i ){
    m = i;
}

void VUMeter::paintEvent(QPaintEvent *)
{
    drawMeter();
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void VUMeter::paintBorder()
{
    QPainter painter(this);
    painter.setWindow(0, 0, 100, 540);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor light = Qt::white;
    QColor dark = colBack.darker(140);

    painter.setPen(QPen(colBack, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QLinearGradient linGrad(5, 250, 10, 250);
    linGrad.setColorAt(0, light);
    linGrad.setColorAt(1, colBack);
    linGrad.setSpread(QGradient::PadSpread);
    painter.setBrush(linGrad);
    QRectF border(5, 5, 90, 530);
    painter.drawRoundRect(border, 30, 5);
    QLinearGradient linGrad1(85, 250, 95, 250);
    linGrad1.setColorAt(0, colBack);
    linGrad1.setColorAt(1, dark);
    linGrad1.setSpread(QGradient::PadSpread);
    painter.setBrush(linGrad1);
    QRectF border1(20, 5, 75, 530);
    painter.drawRoundRect(border1, 30, 5);

    // paint label

    painter.setPen(QPen(colValue, 2));
    QRectF Left(20, 505, 25, 20);
    QRectF Right(55, 505, 25, 20);
    QFont valFont("Arial", 12, QFont::Bold);
    painter.setFont(valFont);
    painter.drawText(Left, Qt::AlignCenter, "L");
    painter.drawText(Right, Qt::AlignCenter, "R");



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
            x1 = m;
            x2 = m + sw;
        }
        //Right value
        else {
            value = (int)(h/step * valueRight)*step;
            peak = (int)(h/step * peakRight)*step;
            x1 = w - m - sw;
            x2 = w - m;
        }

        for( int sec=0; sec < h+m; sec+=step ){
            if ( sec < value && (sec <= .75*h) )
                painter.setPen ( LevelColorNormal );
            else if ( sec < value && (sec > .75 * h)  )
                painter.setPen ( LevelColorHigh );
            else
                painter.setPen ( LevelColorOff );



            if ( orientation() == Qt::Vertical )
                for ( int led=0; led<sh; led+=secStep )
                    painter.drawLine ( x1, h-(sec+led)+m, x2, h-(sec+led)+m );
            else
                for ( int led=0; led<sh; led+=secStep )
                    painter.drawLine ( sec+led+m, x1, sec+led+m, x2 );
        }

        //peaks
        if ( peak ) {

            if ( peak >= .75*h )
            painter.setPen ( LevelColorHigh );
            else
            painter.setPen ( LevelColorNormal );
            if ( orientation() == Qt::Vertical ){
                 for (int led=0; led<ph; led+=pStep)
                     painter.drawLine ( x1, h-(peak+led)+m, x2, h-(peak+led)+m );}
            else
                 for (int led=0; led<ph; led+=pStep)
                    painter.drawLine ( peak+led+m, x1, peak+led+m, x2 );
       }
    }

}

