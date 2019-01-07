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

struct VUMeterPrivate
{
    double valueLeft;
    double valueRight;
    double peakLeft;
    double peakRight;
    Qt::Orientation orientation;
    QTime peakTime;
    int linesPerSegment;
    int ledSize;
    int spacesInSegments;
    int segmentsPerPeak;
    int margin;
    int maxLevel;
    int step;
    int spacesBetweenSegments;
    QColor colBack;
    QColor colValue;
};

VUMeter::VUMeter(QWidget *parent)
 : QWidget(parent)
 , p( new VUMeterPrivate )
{
    setOrientation( Qt::Vertical );

    BackgroundColor.setRgb ( 40,40,40 );
    LevelColorNormal.setRgb ( 0,200,0 );
    LevelColorHigh.setRgb ( 200,0,0 );
    LevelColorOff.setRgb(80,120,120);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, BackgroundColor);
    setPalette(pal);
    p->valueLeft = p->valueRight = 0;
    p->peakLeft = p->peakRight = 0;
    p->margin = 1;
    p->linesPerSegment = 1;
    p->spacesBetweenSegments = 0;
    p->step = p->linesPerSegment + p->spacesBetweenSegments;
    p->segmentsPerPeak = 1;
    p->colBack = QColor(60, 60, 60);
    p->colValue = Qt::white;
}


VUMeter::~VUMeter()
{
}

void VUMeter:: setOrientation( Qt::Orientation o)
{
    p->orientation = o;
}


inline Qt::Orientation VUMeter::orientation() const
{
    return p->orientation;
}

void VUMeter::reset()
{
    p->peakLeft = p->peakRight = 0;
    p->valueLeft = p->valueRight = 0;
    update();
}

void VUMeter::resizeEvent( QResizeEvent* e )
{   
    Q_UNUSED(e);

    int size = (orientation() == Qt::Vertical) ? width() : height();
    p->ledSize = ((size - p->margin) / 2);
    
    p->maxLevel = ( ((orientation() == Qt::Vertical) ? height() : width() )
                / (p->linesPerSegment + p->step)) * (p->linesPerSegment + p->step) + p->margin;
    qDebug() << Q_FUNC_INFO << "size: "<< size;
    qDebug() << Q_FUNC_INFO << "ledSize: "<< p->ledSize;
    qDebug() << Q_FUNC_INFO << "margin: "<< p->margin;
}

void VUMeter::checkPeakTime() {
    if ( p->peakTime.elapsed() >= 1000 ) {
        p->peakLeft = 0;
        p->peakRight = 0;
        p->peakTime.restart();
        update();
    }
}

void VUMeter::setValueLeft ( double f )
{
    double fl = f;

    if (fl < 0.0) fl = 0.0;
        
    if ( fl > p->peakLeft ) {
        p->peakLeft = fl;
        p->peakTime.start();
    }

    if (p->valueLeft != fl){
        p->valueLeft = fl;
        if (p->valueLeft == 0.0)
            p->peakLeft = 0.0;
        update();
        checkPeakTime();
    }
}

void VUMeter::setPercentage( double f )
{
    //qDebug() << "Level: "<< f;
    p->valueRight = p->valueLeft = f ;
    update();
}

void VUMeter::setValueRight ( double f )
{
    double fr = f;

    if (fr < 0.0) fr = 0.0;
    
    if ( fr > p->peakRight ) {
        p->peakRight = fr;
        p->peakTime.start();
    }

    if (p->valueRight != fr ){
        p->valueRight = fr ;
        if (p->valueRight == 0.0)
            p->peakRight = 0.0;
        update();
        checkPeakTime();
    }
}

void VUMeter::setLinesPerSegment( int i ) {
    p->linesPerSegment = i;
    p->step = p->linesPerSegment + p->spacesBetweenSegments;
}

void VUMeter::setSpacesBetweenSegments( int i ) {
    p->spacesBetweenSegments = i;
    p->step = p->linesPerSegment + p->spacesBetweenSegments;
}

void VUMeter::setSegmentsPerPeak( int i ){
    p->segmentsPerPeak = i;
}

void VUMeter::setMargin( int i ){
    p->margin = i;
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

    int valueLeft = 0;
    int valueRight = 0;
    int peakLeft = 0;
    int peakRight = 0;
    QColor colorLeft;
    QColor colorRight;

    valueLeft = static_cast<int>((p->maxLevel / p->step * p->valueLeft) * p->step);
    peakLeft = static_cast<int>((p->maxLevel / p->step * p->peakLeft) * p->step);

    valueRight = static_cast<int>((p->maxLevel / p->step * p->valueRight) * p->step);
    peakRight = static_cast<int>((p->maxLevel / p->step * p->peakRight) * p->step);

    // value segments
    for( int segment = 0; segment < p->maxLevel; segment += p->step ){

        // left value
        if ( segment < valueLeft && (segment <= .75 * p->maxLevel) )
            colorLeft = LevelColorNormal;
        else if ( segment <= valueLeft && (segment > .75 * p->maxLevel)  )
            colorLeft = LevelColorHigh;
        else
            colorLeft = LevelColorOff;

        // right value
        if ( segment < valueRight && (segment <= .75 * p->maxLevel) )
            colorRight = LevelColorNormal;
        else if ( segment <= valueRight && (segment > .75 * p->maxLevel)  )
            colorRight = LevelColorHigh;
        else
            colorRight = LevelColorOff;

        // LEDs
        for ( int led = 0; led < p->linesPerSegment; led++ ) {

            int level = segment + led;

            if ( orientation() == Qt::Vertical ) {
                // draw left
                painter.setPen(colorLeft);
                painter.drawLine ( 0, p->maxLevel - level,
                                   p->ledSize - 1, p->maxLevel - level);
                // draw right
                painter.setPen(colorRight);
                painter.drawLine ( p->margin + p->ledSize, p->maxLevel - level,
                                   p->margin + p->ledSize *2 - 1, p->maxLevel - level);
            } else {
                // draw left
                painter.setPen(colorLeft);
                painter.drawLine ( level, 0,
                                   level, p->ledSize - 1);
                // draw right
                painter.setPen(colorRight);
                painter.drawLine ( level, p->margin + p->ledSize,
                                   level, p->margin + p->ledSize *2 - 1);
            }

         }
      }

    // peak segments

    // left peak
    if ( peakLeft > .75 * p->maxLevel)
        colorLeft = LevelColorHigh;
    else
        colorLeft = LevelColorNormal;

    // right peak
    if ( peakRight > .75 * p->maxLevel)
        colorRight = LevelColorHigh;
    else
        colorRight = LevelColorNormal;

    // LEDs
    for( int segment = 0; segment < p->linesPerSegment * p->segmentsPerPeak; segment += p->step){

        for ( int led = 0; led < p->linesPerSegment; led++) {

            int level = segment + led - 1;

            if ( orientation() == Qt::Vertical ) {
                // draw left
                if ( peakLeft ) {
                    painter.setPen(colorLeft);
                    painter.drawLine ( 0, p->maxLevel - (peakLeft - level ),
                                       p->ledSize - 1, p->maxLevel - (peakLeft - level));
                }
                // draw right
                if ( peakRight ) {
                    painter.setPen(colorRight);
                    painter.drawLine ( p->margin + p->ledSize, p->maxLevel - (peakRight - level),
                                       p->margin + p->ledSize *2 - 1, p->maxLevel - (peakRight - level));
                }
            } else {
                // draw left
                if ( peakLeft ) {
                    painter.setPen(colorLeft);
                    painter.drawLine ( peakLeft - level, 0,
                                       peakLeft - level, p->ledSize - 1);
                }
                // draw right
                if ( peakRight ) {
                    painter.setPen(colorRight);
                    painter.drawLine ( peakRight - level, p->margin + p->ledSize,
                                       peakRight - level, p->margin + p->ledSize *2 - 1);
                }
            }

         }
    }

}

