/***************************************************************************
 *   Copyright (C) 2008 - Giuseppe Cigala                                  *
 *   g_cigala@virgilio.it                                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#include "qvumeter.h"
#include <QPainter>

QVUMeter::QVUMeter(QWidget *parent) : QWidget(parent)
{
    colBack = QColor(40, 40, 40);
    colValue = Qt::white;
    colHigh = Qt::red;
    colLow = Qt::green;
    dimVal = 9;
    min = 0;
    max = 100;
    leftVal = 0;
    rightVal = 0;

}


void QVUMeter::paintEvent(QPaintEvent *)
{
    paintBorder();
    paintBar();
    paintValue();

}

void QVUMeter::paintBorder()
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

void QVUMeter::paintBar()
{
    QPainter painter(this);
    painter.setWindow(0, 0, 100, 540);
    painter.setRenderHint(QPainter::Antialiasing);

    QLinearGradient linGrad(50, 0, 50, 500);
    linGrad.setColorAt(0, colHigh);
    linGrad.setColorAt(1, colLow);
    linGrad.setSpread(QGradient::PadSpread);
    painter.setBrush(linGrad);

    // draw color bar
   
    QRectF bar3(20, 50, 25, 450);
    painter.drawRect(bar3);
    QRectF bar4(55, 50, 25, 450);
    painter.drawRect(bar4);
    
    // draw background bar
    painter.setBrush(QColor(40, 40, 40));
    
    double length = 450.0;
    double leftBar = abs(length * (min-leftVal)/(max-min));
    double rightBar = abs(length * (min-rightVal)/(max-min));
    QRectF bar1(20, 50, 25, 450-leftBar);
    painter.drawRect(bar1);
    QRectF bar2(55, 50, 25, 450-rightBar);
    painter.drawRect(bar2);


    painter.setPen(QPen(Qt::black, 2));

    for (int i = 0; i <=60; i++)
    {
        painter.drawLine(21, 500-450*i/60, 44, 500-450*i/60);
        painter.drawLine(56, 500-450*i/60, 79, 500-450*i/60);
    }

}

void QVUMeter::paintValue()
{
    QPainter painter(this);
    painter.setWindow(0, 0, 100, 540);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setBrush(Qt::black);
    painter.drawRect(20, 15, 25, 25);
    painter.drawRect(55, 15, 25, 25);
    painter.setPen(Qt::gray);
    painter.drawLine(20, 40, 45, 40);
    painter.drawLine(45, 15, 45, 40);
    painter.drawLine(55, 40, 80, 40);
    painter.drawLine(80, 15, 80, 40);

    painter.setPen(QPen(colValue, 1));
    QFont valFont("Arial", dimVal, QFont::Bold);
    painter.setFont(valFont);

    QRectF leftR(20, 15, 25, 25);
    QString lVal = QString("%1").arg(leftVal, 0,'f', 0);
    painter.drawText(leftR, Qt::AlignCenter, lVal);
    QRectF rightR(55, 15, 25, 25);
    QString rVal = QString("%1").arg(rightVal, 0,'f', 0);
    painter.drawText(rightR, Qt::AlignCenter, rVal);

    Q_EMIT valueLChanged(leftVal);
    Q_EMIT valueRChanged(rightVal);

}


void QVUMeter::setValueDim(int dim)
{
    dimVal = dim;
    update();
}

void QVUMeter::setColorBg(QColor color)
{
    colBack = color;
    update();
}

void QVUMeter::setColorValue(QColor color)
{
    colValue = color;
    update();
}

void QVUMeter::setColorHigh(QColor color)
{
    colHigh = color;
    update();
}


void QVUMeter::setColorLow(QColor color)
{
    colLow = color;
    update();
}

void QVUMeter::setLeftValue(double leftValue)
{
    if (leftValue > max)
    {
        leftVal = max;
        update();
    }
    else if (leftValue < min)
    {
        leftVal = min;
        update();
    }
    else if (leftVal != leftValue)
    {
        leftVal = leftValue;
        update();
    }
}

void QVUMeter::setRightValue(double rightValue)
{
    if (rightValue > max)
    {
        rightVal = max;
        update();
    }
    else if (rightValue < min)
    {
        rightVal = min;
        update();
    }
    else
    {
        rightVal = rightValue;
        update();
    }
}

void QVUMeter::setMinValue(double minValue)
{
    if (minValue > max)
    {
        min = max;
        max = minValue;
        update();
    }
    else
    {
        min = minValue;
        update();
    }
}

void QVUMeter::setMaxValue(double maxValue)
{
    if (maxValue < min)
    {
        max = min;
        min = maxValue;
        update();
    }
    else
    {
        max = maxValue;
        update();
    }
}

QSize QVUMeter::minimumSizeHint() const
{
    return QSize(10, 54);
}

QSize QVUMeter::sizeHint() const
{
    return QSize(100, 540);
}


