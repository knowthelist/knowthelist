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

#ifndef DJFILTERWIDGET_H
#define DJFILTERWIDGET_H

#include <QWidget>
#include <QTimer>
#include "filter.h"


namespace Ui {
    class DjFilterWidget;
}

class DjFilterWidget : public QWidget {
    Q_OBJECT
public:
    DjFilterWidget(QWidget *parent = 0);
    ~DjFilterWidget();

    void setFilter(Filter *filter);
    Filter* filter();

protected:
    void changeEvent(QEvent *e);

Q_SIGNALS:
   //void filterChanged();

private:
    Ui::DjFilterWidget *ui;
    QTimer* timer;
    struct Private;
    Private *p;

private slots:
    void on_toolButton_clicked();
    void on_pushActivate_clicked();
    void on_txtPath_textChanged(QString );
    void on_txtGenre_textChanged(QString );
    void on_txtArtist_textChanged(QString );
    void on_sliFilterValue_valueChanged(int value);
    void slotSetFilter();
    void on_filter_statusChanged(bool b);
    void on_filter_countChanged();
    void on_filter_usageChanged();
    void on_filter_maxUsageChanged();
};

#endif // DJFILTERWIDGET_H
