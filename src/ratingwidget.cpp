/*
    Copyright 2010, David Sansome <me@davidsansome.com>
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

#include "ratingwidget.h"

#include <QMouseEvent>
#include <QStylePainter>
#include <QtDebug>

const int RatingPainter::kStarCount;
const int RatingPainter::kStarSize;

RatingPainter::RatingPainter() {

}

QRect RatingPainter::Contents(const QRect& rect) {
  const int width = kStarSize * kStarCount;
  return QRect(rect.x(), rect.y(), width, rect.height());
}

double RatingPainter::RatingForPos(const QPoint& pos, const QRect& rect) {
  const QRect contents = Contents(rect);
  const double raw = double(pos.x() - contents.left()) / contents.width();

  // Round to the nearest 0.1
  return double(int(raw * kStarCount * 2 + 0.5)) / (kStarCount * 2);
}

void RatingPainter::Paint(QPainter* painter, const QRect& rect, float rating) const
{
    Q_UNUSED(rect);

    // save some time here
    if (rating==0)
        return;

    rating *= kStarCount;
  QPixmap on(":/star-on.png");
  QPixmap off(":/star-off.png");

   // Draw the stars
    int x=kStarSize/2;
    for(int i=0; i<kStarCount; i++, x+=kStarSize)
    {
        const QRect t_rect(x, 0, kStarSize, kStarSize);

        if (rating - 0.25 <= i) {
          // Totally empty
          painter->drawPixmap(t_rect, off);
        } else if (rating - 0.75 <= i) {
          // Half full
          const QRect target_left(t_rect.x(), t_rect.y(), kStarSize/2, kStarSize);
          const QRect target_right(t_rect.x() + kStarSize/2, t_rect.y(), kStarSize/2, kStarSize);
          const QRect source_left(0, 0, kStarSize/2, kStarSize);
          const QRect source_right(kStarSize/2, 0, kStarSize/2, kStarSize);
          painter->drawPixmap(target_left, on, source_left);
          painter->drawPixmap(target_right, off, source_right);
        } else {
          // Totally full
          painter->drawPixmap(t_rect, on);
        }
    }
}


RatingWidget::RatingWidget(QWidget* parent)
  : QWidget(parent),
    rating_(0.0),
    hover_rating_(-1.0)
{
  setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
  setMouseTracking(true);
}

QSize RatingWidget::sizeHint() const {
  return QSize(RatingPainter::kStarSize * (RatingPainter::kStarCount+2),
               RatingPainter::kStarSize);
}

void RatingWidget::setRating(float rating) {
  rating_ = rating;
  update();
}

void RatingWidget::paintEvent(QPaintEvent* e) {
    Q_UNUSED(e);
  QStylePainter p(this);


  // Draw the stars
  painter_.Paint(&p, rect(), hover_rating_ == -1.0 ? rating_ : hover_rating_);
}

void RatingWidget::mousePressEvent(QMouseEvent* e) {
  rating_ = RatingPainter::RatingForPos(e->pos(), rect());
  emit RatingChanged(rating_);
}

void RatingWidget::mouseMoveEvent(QMouseEvent* e) {
  hover_rating_ = RatingPainter::RatingForPos(e->pos(), rect());
  update();
}

void RatingWidget::leaveEvent(QEvent*) {
  hover_rating_ = -1.0;
  update();
}
