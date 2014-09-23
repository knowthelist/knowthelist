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

#include "modeselector.h"
#include "ui_modeselector.h"

ModeSelector::ModeSelector(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::ModeSelector)
{
    ui->setupUi(this);
    ui->led1->setLook(QLed::Flat);
    ui->led1->setShape(QLed::Rectangular);
    ui->led1->setColor(QColor(35,119,246));
    ui->led2->setLook(QLed::Flat);
    ui->led2->setShape(QLed::Rectangular);
    ui->led2->setColor(QColor(35,119,246));
    ui->led3->setLook(QLed::Flat);
    ui->led3->setShape(QLed::Rectangular);
    ui->led3->setColor(QColor(35,119,246));
    ui->led2->off();
    ui->led3->off();
    selMode = ModeSelector::MODENONE;
}

ModeSelector::~ModeSelector()
{
    delete ui;
}

void ModeSelector::on_push1_clicked()
{
    //ui->led1->on();
    //ui->led2->off();
    //ui->led3->off();
    selMode = ModeSelector::MODENONE;
    ui->push1->setStyleSheet("QPushButton { border: 1px solid #3399ff;}");
    ui->push2->setStyleSheet("QPushButton { border: 1px solid #aaaaaa;}");
    ui->push3->setStyleSheet("QPushButton { border: 1px solid #aaaaaa;}");
    Q_EMIT modeChanged(selMode);
}
void ModeSelector::on_push2_clicked()
{
    //ui->led1->off();
    //ui->led2->on();
    //ui->led3->off();
    selMode = ModeSelector::MODEYEAR;
    ui->push1->setStyleSheet("QPushButton { border: 1px solid #aaaaaa;}");
    ui->push2->setStyleSheet("QPushButton { border: 1px solid #3399ff;}");
    ui->push3->setStyleSheet("QPushButton { border: 1px solid #aaaaaa;}");
    Q_EMIT modeChanged(selMode);
}
void ModeSelector::on_push3_clicked()
{
    //ui->led1->off();
    //ui->led2->off();
    //ui->led3->on();
    selMode = ModeSelector::MODEGENRE;
    ui->push1->setStyleSheet("QPushButton { border: 1px solid #aaaaaa;}");
    ui->push2->setStyleSheet("QPushButton { border: 1px solid #aaaaaa;}");
    ui->push3->setStyleSheet("QPushButton { border: 1px solid #3399ff;}");
    Q_EMIT modeChanged(selMode);
}
void ModeSelector::setMode(ModeSelector::modeType value)
{
    switch (value)
    {
        case MODEGENRE:
            on_push3_clicked();
            break;
        case MODEYEAR:
            on_push2_clicked();
            break;
        default:
            on_push1_clicked();
            break;
    }
}
ModeSelector::modeType ModeSelector::mode()
{
    return selMode;

}
