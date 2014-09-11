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

#include "progressbar.h"

#include <QtGui>
#include <QHBoxLayout>

ProgressBar::ProgressBar(  QWidget* parent, const char* name):
        QWidget(parent)
{

    QHBoxLayout *mainLayout = new QHBoxLayout;
    mainLayout->setMargin(0);
    mainLayout->setSpacing(1);

  bar = new QProgressBar(this);
  bar->setMinimumHeight(16);
  bar->setRange(0,100);

  clearButton = new QToolButton(this );
  QPixmap pixmap(":clear_left.png");
  clearButton->setIcon(QIcon(pixmap));
  clearButton->setIconSize(QSize(18,17));
  clearButton->setStyleSheet("QToolButton {max-height: 16px;min-height: 16px;}");
  clearButton->resize(22,16);
  connect( clearButton, SIGNAL(clicked()), this, SIGNAL(stopped()) );

  mainLayout->addWidget(clearButton);
  mainLayout->addWidget(bar);

  setLayout(mainLayout);

}

ProgressBar::~ProgressBar() {}  

void ProgressBar::setValue(int value)
{
    bar->setValue(value);
    if (value > 0 && value < 100)
        this->show();
    else
        this->hide();
}

int ProgressBar::value()
{
    return bar->value();
}

void ProgressBar::resizeEvent(QResizeEvent *ev)
{
    bar->setGeometry(0,0,ev->size().width(),20);
}

