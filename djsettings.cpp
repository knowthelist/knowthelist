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

#include "djsettings.h"
#include "ui_djsettings.h"

DjSettings::DjSettings(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DjSettings)
{
    ui->setupUi(this);
}

DjSettings::~DjSettings()
{
    delete ui;
}

void DjSettings::setID(int value)
{
    ui->label->setText(QString::number(value));
}

void DjSettings::setFilterCount(int value)
{
    ui->spinBox->setValue(value);
}

void DjSettings::setName(QString value)
{
    ui->lineEdit->setText(value);
}

int DjSettings::filterCount()
{
    return ui->spinBox->value();
}

QString DjSettings::name()
{
    return ui->lineEdit->text();
}
