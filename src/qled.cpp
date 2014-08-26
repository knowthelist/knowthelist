/* LED class adapted from KDE to use under QT
 *
    Copyright (C) 1998 Jörg Habenicht (j.habenicht@europemail.com)
    Copyright (C) 2010 Christoph Feck <christoph@maxiom.de>
    Copyright (C) 2011 Mario Stephan <mstephan@shared-files.de>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "qled.h"

#include <QtGui/QPainter>
#include <QtGui/QImage>
#include <QStyle>
#include <QStyleOption>

class QLed::Private
{
  public:
    Private()
      : darkFactor( 300 ),
        state( On ), look( Raised ), shape( Circular )
    {
    }

    int darkFactor;
    QColor color;
    State state;
    Look look;
    Shape shape;
    QPixmap cachedPixmap[2]; // for both states
    QStyle::ControlElement ce_indicatorLedCircular;
    QStyle::ControlElement ce_indicatorLedRectangular;
};


QLed::QLed( QWidget *parent )
  : QWidget( parent ),
    d( new Private )
{
  setColor( Qt::green );
}


QLed::QLed( const QColor& color, QWidget *parent )
  : QWidget( parent ),
    d( new Private )
{
  setColor( color );
}

QLed::QLed( const QColor& color, State state, Look look, Shape shape,
            QWidget *parent )
  : QWidget( parent ),
    d( new Private )
{
  d->state = (state == Off ? Off : On);
  d->look = look;
  d->shape = shape;
  setColor( color );
}

QLed::~QLed()
{
  delete d;
}

void QLed::paintEvent( QPaintEvent* )
{
    switch( d->shape ) {
      case Rectangular:
        switch ( d->look ) {
          case Sunken:
            paintRectFrame( false );
            break;
          case Raised:
            paintRectFrame( true );
            break;
          case Flat:
            paintRect();
            break;
        }
        break;
      case Circular:
        switch ( d->look ) {
          case Flat:
            paintFlat();
            break;
          case Raised:
            paintRaised();
            break;
          case Sunken:
            paintSunken();
            break;
        }
        break;
    }
}

int QLed::ledWidth() const
{
  // Make sure the LED is round!
  int size = qMin(width(), height());

  // leave one pixel border
  size -= 2;

  return qMax(0, size);
}

bool QLed::paintCachedPixmap()
{
    if (d->cachedPixmap[d->state].isNull()) {
        return false;
    }
    QPainter painter(this);
    painter.drawPixmap(1, 1, d->cachedPixmap[d->state]);
    return true;
}

void QLed::paintFlat()
{
    paintLed(Circular, Flat);
}

void QLed::paintRaised()
{
    paintLed(Circular, Raised);
}

void QLed::paintSunken()
{
    paintLed(Circular, Sunken);
}

void QLed::paintRect()
{
    paintLed(Rectangular, Flat);
}

void QLed::paintRectFrame( bool raised )
{
    paintLed(Rectangular, raised ? Raised : Sunken);
}

QLed::State QLed::state() const
{
  return d->state;
}

QLed::Shape QLed::shape() const
{
  return d->shape;
}

  QColor QLed::color() const
{
  return d->color;
}

QLed::Look QLed::look() const
{
  return d->look;
}

void QLed::setState( State state )
{
  if ( d->state == state)
    return;

  d->state = (state == Off ? Off : On);
  updateCachedPixmap();
}

void QLed::setShape( Shape shape )
{
  if ( d->shape == shape )
    return;

  d->shape = shape;
  updateCachedPixmap();
}

void QLed::setColor( const QColor &color )
{
  if ( d->color == color )
    return;

  d->color = color;
  updateCachedPixmap();
}

void QLed::setDarkFactor( int darkFactor )
{
  if ( d->darkFactor == darkFactor )
    return;

  d->darkFactor = darkFactor;
  updateCachedPixmap();
}

int QLed::darkFactor() const
{
  return d->darkFactor;
}

void QLed::setLook( Look look )
{
  if ( d->look == look)
    return;

  d->look = look;
  updateCachedPixmap();
}

void QLed::toggle()
{
  d->state = (d->state == On ? Off : On);
  updateCachedPixmap();
}

void QLed::on()
{
  setState( On );
}

void QLed::off()
{
  setState( Off );
}

void QLed::resizeEvent( QResizeEvent * )
{
    updateCachedPixmap();
}

QSize QLed::sizeHint() const
{
    QStyleOption option;
    option.initFrom(this);
    int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize, &option, this);
    return QSize( iconSize,  iconSize );
}

QSize QLed::minimumSizeHint() const
{
  return QSize( 16, 16 );
}

void QLed::updateCachedPixmap()
{
    d->cachedPixmap[Off] = QPixmap();
    d->cachedPixmap[On] = QPixmap();
    update();
}

void QLed::paintLed(Shape shape, Look look)
{
    if (paintCachedPixmap()) {
        return;
    }

    QSize size(width() - 2, height() - 2);
    if (shape == Circular) {
        const int width = ledWidth();
        size = QSize(width, width);
    }
    QPointF center(size.width() / 2.0, size.height() / 2.0);
    const int smallestSize = qMin(size.width(), size.height());
    QPainter painter;
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(0);
    QRadialGradient fillGradient(center, smallestSize / 2.0, QPointF(center.x(), size.height() / 3.0));
    const QColor fillColor = d->state != Off ? d->color : d->color.dark(d->darkFactor);
    fillGradient.setColorAt(0.0, fillColor.light(250));
    fillGradient.setColorAt(0.5, fillColor.light(130));
    fillGradient.setColorAt(1.0, fillColor);
    QConicalGradient borderGradient(center, look == Sunken ? 90 : -90);
    QColor borderColor = palette().color(QPalette::Dark);
    if (d->state == On) {
        QColor glowOverlay = fillColor;
        glowOverlay.setAlpha(80);
        borderColor = overlayColors(borderColor, glowOverlay);
    }
    borderGradient.setColorAt(0.2, borderColor);
    borderGradient.setColorAt(0.5, palette().color(QPalette::Light));
    borderGradient.setColorAt(0.8, borderColor);

    painter.begin(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(look == Flat ? QBrush(fillColor) : QBrush(fillGradient));
    const QBrush penBrush = (look == Flat) ? QBrush(borderColor) : QBrush(borderGradient);
    const qreal penWidth = smallestSize / 8.0;
    painter.setPen(QPen(penBrush, penWidth));
    QRectF r(penWidth / 2.0, penWidth / 2.0, size.width() - penWidth, size.height() - penWidth);
    if (shape == Rectangular) {
        painter.drawRect(r);
    } else {
        painter.drawEllipse(r);
    }
    painter.end();

    d->cachedPixmap[d->state] = QPixmap::fromImage(image);
    painter.begin(this);
    painter.drawPixmap(1, 1, d->cachedPixmap[d->state]);
    painter.end();
}

 QColor QLed::overlayColors(const QColor &base, const QColor &paint,
                                   QPainter::CompositionMode comp)
 {
    QImage img(1, 1, QImage::Format_ARGB32_Premultiplied);
     QPainter p(&img);
     QColor start = base;
     start.setAlpha(255); // opaque
     p.fillRect(0, 0, 1, 1, start);
     p.setCompositionMode(comp);
     p.fillRect(0, 0, 1, 1, paint);
     p.end();
     return img.pixel(0, 0);
}
