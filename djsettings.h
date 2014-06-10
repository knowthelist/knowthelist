#ifndef DJSETTINGS_H
#define DJSETTINGS_H
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

#include <QWidget>

namespace Ui {
class DjSettings;
}

class DjSettings : public QWidget
{
    Q_OBJECT
    
public:
    explicit DjSettings(QWidget *parent = 0);
    ~DjSettings();

    void setID(int value);
    void setFilterCount(int value);
    void setName(QString value);
    int filterCount();
    QString name();
    
private:
    Ui::DjSettings *ui;
};

#endif // DJSETTINGS_H
