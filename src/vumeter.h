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

#ifndef VUMETER_H
#define VUMETER_H

#include <QWidget>
#include <QTime>
#include <math.h>

// helper functions
const float LEVEL_MIN= 1.f/(1<<20); // minimal positive sample for 20 bit resolution
inline float levelToDB(float level) {
        if (level<LEVEL_MIN) level=LEVEL_MIN; // prevent from div by 0
        return (25.0f)*log(level);

}

inline float DBToLevel(float db) {
	return exp(db/(log(2.f)/6.f));
}


class VUMeter : public QWidget
{
Q_OBJECT
    Q_PROPERTY( Qt::Orientation orientation READ orientation WRITE setOrientation )
public:
    VUMeter(QWidget *parent = 0);
    ~VUMeter();
 
    void setValueLeft( float );
    void setValueRight( float );
    void setPercentage( float );
    void checkPeakTime();
    void setOrientation( Qt::Orientation );
    Qt::Orientation orientation() const;
    void setLinesPerSegment( int );
    void setSpacesBetweenSegments( int );
    void setSpacesInSegments( int );
    void setLinesPerPeak( int );
    void setSpacesInPeak( int );
    void setMargin( int );
    QColor LevelColorNormal, LevelColorHigh, LevelColorOff, BackgroundColor;
    void reset();
    

protected:

    void paintEvent(QPaintEvent *);
    void resizeEvent(QResizeEvent *);

private:
    float valueLeft, valueRight;
    float peakLeft, peakRight;
    void  drawMeter();
    void paintBorder();
    Qt::Orientation orient;
    QTime peakTime;
    int sh, sw, secStep, ph, pw, pStep, m, h, w, step, space; 
    QColor colBack;
    QColor colValue;

};

inline Qt::Orientation VUMeter::orientation() const
{
    return orient;
}

#endif
