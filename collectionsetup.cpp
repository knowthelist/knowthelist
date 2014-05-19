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

#include "collectionsetup.h"
#include "collectionsetupmodel.h"

#include <QVBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QLabel>
#include <QCheckBox>

class CollectionSetupPrivate
{
    public:
        QTreeView *directorytree;
        CollectionSetupModel *model;
        QStringList dirs;
        QCheckBox *chkMonitor;
};

CollectionSetup::CollectionSetup( QWidget *parent )
    : QDialog( parent )
{
    p = new CollectionSetupPrivate;

    QVBoxLayout *layout = new QVBoxLayout;
    QHBoxLayout *hl = new QHBoxLayout;

    QLabel *label = new QLabel( tr("Select folders for your music collection"), this );

    p->directorytree=new QTreeView(this);


    p->model = new CollectionSetupModel(this);
    p->directorytree->setModel(p->model);
    p->directorytree->setColumnHidden(1, true);
    p->directorytree->setColumnHidden(2, true);
    p->directorytree->setColumnHidden(3, true);
    p->directorytree->expandToDepth(0);

    p->chkMonitor   = new QCheckBox( tr("&Watch folders for changes"), this );
    p->chkMonitor->setToolTip(tr( "Scan for new files and update the music collection" ) );

    QPushButton *okBtn = new QPushButton(tr("Ok") );
    QPushButton *cnBtn = new QPushButton(tr("Cancel") );

    connect(okBtn,SIGNAL(clicked()),this,SLOT(okPressed()));
    connect(cnBtn,SIGNAL(clicked()),this,SLOT(cnPressed()));

    layout->addWidget(label);
    layout->addWidget(p->directorytree);
    layout->addWidget(p->chkMonitor);
    okBtn->resize(70,20);
    cnBtn->resize(70,20);
    hl->addStretch(1);
    hl->addWidget(okBtn);
    hl->addWidget(cnBtn);
    layout->addLayout(hl);
    setLayout(layout);
    setMinimumSize(600,0);

//    setStyleSheet(QString("QDialog, QTreeView{    "
//                          "background: qlineargradient("
//                "x1:0, y1:0, x2:0, y2:1,"
//                "stop: 0.1 #999999,"
//                 "stop:0.5 #505050,"
//                "stop:1 black"
//              ");"
//             "}"));

    // Read config values
    QSettings settings;
    p->model->setDirsChecked(settings.value("Dirs").toStringList());
    p->chkMonitor->setChecked(settings.value("Monitor").toBool());

}

QStringList CollectionSetup::dirs()
{
    return p->dirs;
}

bool CollectionSetup::monitor()
{
    return p->chkMonitor->isChecked();
}

void CollectionSetup::okPressed()
{
    p->dirs = p->model->dirsChecked();
    writeConfig();
    emit accept();
}

void CollectionSetup::cnPressed()
{
   emit reject();
}

void CollectionSetup::writeConfig()
{
    QSettings settings;

    settings.setValue("Dirs",p->dirs);
    settings.setValue("Monitor",monitor() );
}


