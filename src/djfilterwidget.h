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
    void setID(QString value);
    void slideCloseWidget(bool open);
    void setAllGenres(QStringList &values);
    void setAllArtists(QStringList values);


protected:
    void changeEvent(QEvent *event);
    void mousePressEvent(QMouseEvent* event);
    void keyPressEvent(QKeyEvent *e);

Q_SIGNALS:
    void deleted();

private:
    Ui::DjFilterWidget* ui;
    QTimer* timer;
    struct DjFilterWidgetPrivate* p;

private slots:
    void on_pushActivate_clicked();
    void on_txtPath_textChanged(QString );
    void on_cmbGenres_editTextChanged(QString );
    void on_cmbArtists_editTextChanged(QString );
    void on_sliFilterValue_valueChanged(int value);
    void slotSetFilter();
    void onFilterStatusChanged(bool b);
    void onFilterCountChanged();
    void onFilterUsageChanged();
    void onFilterMaxUsageChanged();
    void on_lbl1_linkActivated(const QString &link);
    void timerSlide_timeOut();
    void on_pushClose_clicked();
};

#endif // DJFILTERWIDGET_H
