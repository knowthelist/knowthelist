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

#ifndef RATINGWIDGET_H
#define RATINGWIDGET_H

#include <QFrame>
#include <QPixmap>

class RatingPainter {
public:
  RatingPainter();

  static const int kStarCount = 5;
  static const int kStarSize = 15;
  static QRect Contents(const QRect& rect);
  static double RatingForPos(const QPoint& pos, const QRect& rect);

  void Paint(QPainter* painter, const QRect& rect, float rating) const;

private:
  QPixmap stars_[kStarCount*2+1];
};

class RatingWidget : public QWidget {
  Q_OBJECT
  Q_PROPERTY(float rating READ rating WRITE setRating);

public:
  RatingWidget(QWidget* parent = 0);

  QSize sizeHint() const;

  float rating() const { return rating_; }
  void setRating(float rating);

signals:
  void RatingChanged(float rating);

protected:
  void paintEvent(QPaintEvent*);
  void mousePressEvent(QMouseEvent* e);
  void mouseMoveEvent(QMouseEvent* e);
  void leaveEvent(QEvent*);

private:
  RatingPainter painter_;
  float rating_;
  float hover_rating_;
};

#endif // RATINGWIDGET_H

