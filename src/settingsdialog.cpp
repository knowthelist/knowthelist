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

#include "collectionsetupmodel.h"
#include "settingsdialog.h"
#include "djsettings.h"
#include "ui_settingsdialog.h"

#include <QtDebug>
#include <QtGui>
#include <QtSql>



class SettingsDialogPrivate
{
   public:
        Ui::SettingsDialog ui;
        QWidget *parent;
        CollectionSetupModel *model;
};

SettingsDialog::SettingsDialog(QWidget * parent)
	: QDialog(parent)
{
    p = new SettingsDialogPrivate;
	p->ui.setupUi(this);
        p->parent=parent;

	// set icons in the settings list

	QTableWidgetItem * item;
	p->ui.settingsGroupsTable->setIconSize(QSize(32,32));

	item = p->ui.settingsGroupsTable->item(0, 0);
    item->setIcon(QIcon(":slider.png"));
	item->setTextAlignment(Qt::AlignCenter);
        item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);

	item = p->ui.settingsGroupsTable->item(1, 0);
    item->setIcon(QIcon(":database.png"));
	item->setTextAlignment(Qt::AlignCenter);
        item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);

	item = p->ui.settingsGroupsTable->item(2, 0);
    item->setIcon(QIcon(":volume.png"));
	item->setTextAlignment(Qt::AlignCenter);
        item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);

	item = p->ui.settingsGroupsTable->item(3, 0);
    item->setIcon(QIcon(":DJ.png"));
	item->setTextAlignment(Qt::AlignCenter);
        item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);

	item = p->ui.settingsGroupsTable->item(4, 0);
    item->setIcon(QIcon(":list.png"));
	item->setTextAlignment(Qt::AlignCenter);
        item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);

    item = p->ui.settingsGroupsTable->item(5, 0);
    item->setIcon(QIcon(":folder.png"));
    item->setTextAlignment(Qt::AlignCenter);
        item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);

    item = p->ui.settingsGroupsTable->item(6, 0);
    item->setIcon(QIcon(":settings.png"));
    item->setTextAlignment(Qt::AlignCenter);
        item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);

	int w = p->ui.settingsGroupsTable->width();
	p->ui.settingsGroupsTable->setColumnWidth(0, w);

	// select first item
	p->ui.settingsGroupsTable->setCurrentCell(0, 0);

	// select first page
	p->ui.pages->setCurrentIndex(0);

    //Collection folder setup
    p->model = new CollectionSetupModel();

    p->ui.collectionsTreeView->setModel(p->model);
    p->ui.collectionsTreeView->setColumnHidden(1, true);
    p->ui.collectionsTreeView->setColumnHidden(2, true);
    p->ui.collectionsTreeView->setColumnHidden(3, true);
    p->ui.collectionsTreeView->expandToDepth(0);

    connect(p->ui.settingsGroupsTable, SIGNAL(itemSelectionChanged()),
            this, SLOT(tableSelectionChanged()));

    connect(p->ui.faderEndSlider, SIGNAL(sliderMoved(int)),
                    this, SLOT(on_faderEndSlider_sliderMoved(int)));
    connect(p->ui.faderTimeSlider, SIGNAL(sliderMoved(int)),
                    this, SLOT(on_faderTimeSlider_sliderMoved(int)));

    connect(p->ui.pushScanNow,SIGNAL(clicked()),this, SLOT(onScanNow()));
    connect(p->ui.pushResetStats,SIGNAL(clicked()),this, SIGNAL(resetStatsPressed()) );

    connect(p->ui.countDJ,SIGNAL(valueChanged(int)),this,SLOT(loadDjList(int)));
}

SettingsDialog::~SettingsDialog()
{
	delete p;
}

void SettingsDialog::setCurrentTab(SettingsDialog::Tab tab)
{
    p->ui.settingsGroupsTable->setCurrentCell(tab, 0);
    p->ui.pages->setCurrentIndex(tab);
}

int SettingsDialog::exec()
{
	// load settings
	if (!loadSettings()) {
		return QDialog::Rejected;
	}

	return QDialog::exec();
}

void SettingsDialog::accept()
{
    QSettings settings;
    settings.setValue("MonitorOutputDevice",p->ui.monitorOutputDevice->currentText());

    //Common
    settings.setValue("language",p->ui.comboLanguage->currentIndex());

    //save fade slider
    settings.setValue("faderTimeSlider",p->ui.faderTimeSlider->value());
    settings.setValue("faderEndSlider",p->ui.faderEndSlider->value());

    //Playlist settings
    settings.setValue("checkAutoRemove",p->ui.checkAutoRemove->isChecked());
    settings.setValue("editPlaylistRoot",p->ui.txtPlaylistRoot->text());

    //Silent settings
    settings.setValue("checkAutoCue",p->ui.checkAutoCue->isChecked());
    settings.setValue("checkSkipSilentEnd",p->ui.checkSkipSilentEnd->isChecked());

    //AutoDJ
    settings.setValue("minTracks",p->ui.minTracks->value());
    settings.setValue("countDJ",p->ui.countDJ->value());
    settings.setValue("isEnabledAutoDJCount",p->ui.checkAutoDjCountPlayed->isChecked());
    settings.beginGroup("AutoDJ");
    int maxDj=p->ui.countDJ->value();

        for (int d=0;d<maxDj;d++)
        {
            settings.beginGroup(QString::number(d));
            QListWidgetItem *item = p->ui.listDjNames->item(d);
            if (DjSettings *djs = dynamic_cast<DjSettings*>(p->ui.listDjNames->itemWidget(item))) {
                settings.setValue("Name", djs->name() );
                settings.setValue("FilterCount", djs->filterCount() );
            }
            settings.endGroup();
        }
        settings.endGroup();

    //CollectionFolders
    settings.setValue("Dirs",p->model->dirsChecked());
    settings.setValue("Monitor", p->ui.chkMonitor->isChecked() );

    //File Browser
    settings.setValue("editBrowerRoot",p->ui.txtBrowserRoot->text());

    QDialog::accept();
}

bool SettingsDialog::loadSettings()
{
    QSettings settings;
    p->ui.monitorOutputDevice->clear();
    p->ui.monitorOutputDevice->addItems(settings.value("MonitorOutputDevices").toStringList());

    int index = p->ui.monitorOutputDevice->findText(settings.value("MonitorOutputDevice").toString());
    p->ui.monitorOutputDevice->setCurrentIndex(index);

    //Common
    p->ui.comboLanguage->setCurrentIndex( settings.value("language",0 ).toInt());

    //fade slider
    p->ui.faderTimeSlider->setValue(settings.value("faderTimeSlider","12").toInt());
    p->ui.faderTimeLabel->setText(settings.value("faderTimeSlider","12").toString() + "s");
    p->ui.faderEndSlider->setValue(settings.value("faderEndSlider","12").toInt());
    p->ui.faderEndLabel->setText(settings.value("faderEndSlider","12").toString() + "s");

    //Playlist setting
    p->ui.checkAutoRemove->setChecked(settings.value("checkAutoRemove",true).toBool());
    p->ui.txtPlaylistRoot->setText(settings.value("editPlaylistRoot","").toString());

    //Silent setting
    p->ui.checkSkipSilentEnd->setChecked(settings.value("checkSkipSilentEnd",true).toBool());
    p->ui.checkAutoCue->setChecked(settings.value("checkAutoCue",true).toBool());

    //AutoDJ
    p->ui.minTracks->setValue(settings.value("minTracks","6").toInt());
    p->ui.countDJ->setValue(settings.value("countDJ","3").toInt());
    p->ui.checkAutoDjCountPlayed->setChecked(settings.value("isEnabledAutoDJCount",false).toBool());

    //CollectionFolders
    p->model->setDirsChecked(settings.value("Dirs").toStringList());
    p->ui.chkMonitor->setChecked(settings.value("Monitor").toBool());

    //File Browser
    p->ui.txtBrowserRoot->setText(settings.value("editBrowerRoot","").toString());

    //Load Dj list
    loadDjList(p->ui.countDJ->value());

    return true;
}

void SettingsDialog::loadDjList(int count)
{
    QSettings settings;
    QListWidgetItem *itm;
    DjSettings *djs;

    p->ui.listDjNames->clear();

    settings.beginGroup("AutoDJ");
    for (int d=0;d<count;d++)
    {
        settings.beginGroup(QString::number(d));
        itm = new QListWidgetItem(p->ui.listDjNames);
        p->ui.listDjNames->addItem(itm);
        djs = new DjSettings(p->ui.listDjNames);
        djs->setID(d+1);
        djs->setName(settings.value("Name","Dj%1").toString().arg(d+1));
        djs->setFilterCount(settings.value("FilterCount","2").toInt());
        p->ui.listDjNames->setItemWidget(itm,djs);
        settings.endGroup();
    }
    settings.endGroup();
}

void SettingsDialog::on_pushButton_clicked()
{
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::DirectoryOnly);
    if (dialog.exec())
         p->ui.txtBrowserRoot->setText(dialog.selectedFiles().first());
}

void SettingsDialog::on_pushPlaylistRoot_clicked()
{
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::DirectoryOnly);
    if (dialog.exec())
         p->ui.txtPlaylistRoot->setText(dialog.selectedFiles().first());
}

void SettingsDialog::on_pushAbout_clicked()
{
    QMessageBox::about(this,tr("About Knowthelist"),
                       tr("<h3>Knowthelist</h3>"
                          "<br />Copyright (C) 2005-2014 Mario Stephan "
                          "<br /><a href='mailto:mstephan@shared-files.de'>mstephan@shared-files.de</a>"
                          "<br /><br /><a href='http://knowthelist.github.io/knowthelist'>"
                          "http://knowthelist.github.io/knowthelist</a>"));

}

void SettingsDialog::onScanNow()
{
    QSettings settings;
    settings.setValue("Dirs",p->model->dirsChecked());
    Q_EMIT scanNowPressed();
}

void SettingsDialog::on_faderEndSlider_sliderMoved(int position)
{
    p->ui.faderEndLabel->setText(QString::number(position)+"s");
}

void SettingsDialog::on_faderTimeSlider_sliderMoved(int position)
{
    p->ui.faderTimeLabel->setText(QString::number(position)+"s");
}

void SettingsDialog::tableSelectionChanged()
{
        QTableWidgetItem * item = p->ui.settingsGroupsTable->selectedItems().first();
        p->ui.pages->setCurrentIndex(item->row());
}

