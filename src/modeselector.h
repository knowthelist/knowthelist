/*
    Copyright (C) 2014 Mario Stephan <mstephan@shared-files.de>

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

#ifndef MODESELECTOR_H
#define MODESELECTOR_H

#include <QFrame>

namespace Ui {
class ModeSelector;
}

class ModeSelector : public QFrame
{
    Q_OBJECT
    
public:
    explicit ModeSelector(QWidget *parent = 0);
    ~ModeSelector();
    enum modeType { MODENONE, MODEYEAR, MODEGENRE };

    void setMode(ModeSelector::modeType value);
    ModeSelector::modeType mode();

Q_SIGNALS:
    void modeChanged(ModeSelector::modeType value);
    
private slots:
    void on_push1_clicked();
    void on_push2_clicked();
    void on_push3_clicked();

private:
    Ui::ModeSelector *ui;
    modeType selMode;
};

#endif // MODESELECTOR_H
