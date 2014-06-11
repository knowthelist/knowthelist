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

#ifndef QLed_H
#define QLed_H

#include <QtGui/QWidget>
#include <QPainter>

class QColor;
class QLed : public QWidget
 {
     Q_OBJECT
     Q_ENUMS( State Shape Look )

   public:

     enum State { Off, On };

     enum Shape { Rectangular, Circular };

     enum Look  { Flat, Raised, Sunken };

     explicit QLed( QWidget *parent = 0 );

     explicit QLed( const QColor &color, QWidget *parent = 0 );

    QLed( const QColor& color, QLed::State state, QLed::Look look, QLed::Shape shape,
           QWidget *parent = 0 );

     ~QLed();

     QColor color() const;

     State state() const;

     Look look() const;

     Shape shape() const;

    int darkFactor() const;

     void setColor( const QColor& color );

     void setState( State state );

     void setLook( Look look );

     void setShape( Shape shape );

     void setDarkFactor( int darkFactor );

     virtual QSize sizeHint() const;
     virtual QSize minimumSizeHint() const;

   public Q_SLOTS:

     void toggle();

     void on();

     void off();

   protected:
     virtual int ledWidth() const;

    virtual void paintFlat();

    virtual void paintRaised();

     virtual void paintSunken();

     virtual void paintRect();

     virtual void paintRectFrame( bool raised );

     void paintEvent( QPaintEvent* );
     void resizeEvent( QResizeEvent* );

     bool paintCachedPixmap();

     void updateCachedPixmap();

     void paintLed(Shape shape, Look look);

   private:
     class Private;
     Private * const d;
     QColor overlayColors(const QColor &base, const QColor &paint,
                                     QPainter::CompositionMode comp = QPainter::CompositionMode_SourceOver);
};

#endif
