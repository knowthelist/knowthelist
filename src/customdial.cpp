/*
    Copyright (C) 2019 Mario Stephan <mstephan@shared-files.de>

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

#include "customdial.h"

#include <QColor>
#include <QPainter>

#include <cmath>

CustomDial::CustomDial(QWidget* parent)
    : QDial(parent)
{
    qDebug() << Q_FUNC_INFO << "START";
    // Defaults
    QDial::setRange(0, 360);
}

void CustomDial::paintEvent(QPaintEvent*)
{
    static const double degree270 = 1.5 * M_PI;
    static const double degree225 = 1.25 * M_PI;
    static const int scaleDiameter = static_cast<int>(round((this->height() * 0.8) / 2) * 2);
    static const int scaleOffset = static_cast<int>((this->height() - scaleDiameter) / 2);
    static const int dialDiameter = static_cast<int>(round((this->height() * 0.65) / 2) * 2);
    static const int dialOffset = static_cast<int>((this->height() - dialDiameter) / 2);

    QPainter painter(this);

    // Smooth out the circle
    painter.setRenderHint(QPainter::Antialiasing);

    // Gradient for dial
    QRadialGradient radialGrad(QPointF(dialOffset, dialOffset), dialDiameter);
    radialGrad.setColorAt(0.1, "#999");
    radialGrad.setColorAt(0.5, "#777");
    radialGrad.setColorAt(1, "#222");

    QBrush brush(radialGrad);
    painter.setBrush(brush);
    QPen pen;
    pen.setStyle(Qt::NoPen);
    painter.setPen(pen);

    // Draw dial
    painter.drawEllipse(dialOffset, dialOffset, dialDiameter, dialDiameter);

    // Draw black border of dial
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor("#333"), 3));
    painter.drawEllipse(dialOffset, dialOffset, dialDiameter, dialDiameter);

    // Draw scale arc
    int startAngle = 225 * 16;
    int spanAngle = -270 * 16;
    QRectF rectScale(scaleOffset, scaleOffset, scaleDiameter, scaleDiameter);
    painter.setPen(QPen(QColor("#444"), 6));
    painter.drawArc(rectScale, startAngle, spanAngle);
    painter.setPen(QPen(QColor("#777"), 2));
    painter.drawArc(rectScale, startAngle, spanAngle);

    // Calc notch position
    double ratio = static_cast<double>(QDial::value()) / QDial::maximum();
    double angle = ratio * degree270 - degree225;
    double r = dialDiameter / 2.0;
    double y = sin(angle) * (r - dialOffset / 2) + r + dialOffset;
    double x = cos(angle) * (r - dialOffset / 2) + r + dialOffset;
    double c = r + dialOffset;

    // Draw the notch
    painter.setPen(QPen(QColor("#ccc"), 2));
    painter.drawLine(QPointF(x, y), QPointF(c, c));
}
