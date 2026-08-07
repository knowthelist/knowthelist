/*
    Copyright (C) 2005-2026 Mario Stephan <mstephan@shared-files.de>

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

#include "settingsdialog.h"
#include "collectionsetupmodel.h"
#include "djsettings.h"
#include "ui_settingsdialog.h"

#include <QFileDialog>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QtDebug>
#include <QWidget>
#include <QtSql>

class SettingsDialogPrivate {
public:
    Ui::SettingsDialog ui;
    QWidget* parent;
    CollectionSetupModel* model;
    QGroupBox* beatSyncGroup;
    QCheckBox* checkBeatSyncEnabled;
    QCheckBox* checkBeatCueEnabled;
    QCheckBox* checkBeatAnalyzeTempo;
    QGroupBox* transitionGroup;
    QCheckBox* checkBeatBlend;
    QCheckBox* checkBassSwap;
    QCheckBox* checkVocalHandoff;
    QCheckBox* checkEqualPower;
    QCheckBox* checkHardCut;
    QComboBox* transitionStyle;
    QSpinBox* minimumTransitionDuration;
    QSpinBox* maximumTransitionDuration;
    QSpinBox* maximumTempoCorrection;
};

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    p = new SettingsDialogPrivate;
    p->ui.setupUi(this);
    p->parent = parent;
    p->ui.settingsGroupsTable->item(0, 0)->setText(tr("Transitions"));
    p->ui.label->setText(tr("Transition timing and analysis"));
    p->ui.label_5->setText(tr("Start transition before cue end"));
    p->ui.label_7->setText(tr("Fallback transition duration"));
    p->ui.pages->setMinimumHeight(690);

    // set icons in the settings list

    QTableWidgetItem* item;
    p->ui.settingsGroupsTable->setIconSize(QSize(32, 32));

    item = p->ui.settingsGroupsTable->item(0, 0);
    item->setIcon(QIcon(":slider.png"));
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    item = p->ui.settingsGroupsTable->item(1, 0);
    item->setIcon(QIcon(":database.png"));
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    item = p->ui.settingsGroupsTable->item(2, 0);
    item->setIcon(QIcon(":volume.png"));
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    item = p->ui.settingsGroupsTable->item(3, 0);
    item->setIcon(QIcon(":DJ.png"));
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    item = p->ui.settingsGroupsTable->item(4, 0);
    item->setIcon(QIcon(":list.png"));
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    item = p->ui.settingsGroupsTable->item(5, 0);
    item->setIcon(QIcon(":folder.png"));
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    item = p->ui.settingsGroupsTable->item(6, 0);
    item->setIcon(QIcon(":settings.png"));
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    int w = p->ui.settingsGroupsTable->width();
    p->ui.settingsGroupsTable->setColumnWidth(0, w);

    // select first item
    p->ui.settingsGroupsTable->setCurrentCell(0, 0);

    // select first page
    p->ui.pages->setCurrentIndex(0);

    //Collection folder setup
    p->model = new CollectionSetupModel(this);

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

    connect(p->ui.pushScanNow, SIGNAL(clicked()), this, SLOT(onScanNow()));
    connect(p->ui.pushResetStats, SIGNAL(clicked()), this, SIGNAL(resetStatsPressed()));

    connect(p->ui.countDJ, SIGNAL(valueChanged(int)), this, SLOT(loadDjList(int)));

    p->transitionGroup = new QGroupBox(tr("Automatic transition modes"), p->ui.page);
    p->transitionGroup->setGeometry(QRect(10, 245, 380, 295));
    auto* transitionLayout = new QVBoxLayout(p->transitionGroup);
    transitionLayout->setContentsMargins(8, 6, 8, 6);

    p->checkBeatBlend = new QCheckBox(tr("Beat / phrase blend"), p->transitionGroup);
    p->checkBassSwap = new QCheckBox(tr("Bass swap"), p->transitionGroup);
    p->checkVocalHandoff = new QCheckBox(tr("Vocal / mid handoff"), p->transitionGroup);
    p->checkEqualPower = new QCheckBox(tr("Equal-power blend"), p->transitionGroup);
    p->checkHardCut = new QCheckBox(tr("Hard cut"), p->transitionGroup);
    transitionLayout->addWidget(p->checkBeatBlend);
    transitionLayout->addWidget(p->checkBassSwap);
    transitionLayout->addWidget(p->checkVocalHandoff);
    transitionLayout->addWidget(p->checkEqualPower);
    transitionLayout->addWidget(p->checkHardCut);

    auto* transitionForm = new QFormLayout;
    p->transitionStyle = new QComboBox(p->transitionGroup);
    p->transitionStyle->addItem(tr("Balanced"), "balanced");
    p->transitionStyle->addItem(tr("Dance-focused"), "dance");
    p->transitionStyle->addItem(tr("Vocal-focused"), "vocal");
    p->minimumTransitionDuration = new QSpinBox(p->transitionGroup);
    p->minimumTransitionDuration->setRange(1, 30);
    p->maximumTransitionDuration = new QSpinBox(p->transitionGroup);
    p->maximumTransitionDuration->setRange(1, 30);
    p->maximumTempoCorrection = new QSpinBox(p->transitionGroup);
    p->maximumTempoCorrection->setRange(0, 50);
    p->maximumTempoCorrection->setSuffix("%");
    transitionForm->addRow(tr("Style bias"), p->transitionStyle);
    transitionForm->addRow(tr("Minimum duration"), p->minimumTransitionDuration);
    transitionForm->addRow(tr("Maximum duration"), p->maximumTransitionDuration);
    transitionForm->addRow(tr("Maximum tempo correction"), p->maximumTempoCorrection);
    transitionLayout->addLayout(transitionForm);

    // Beat analysis and cue options remain separate from transition selection.
    p->beatSyncGroup = new QGroupBox(tr("Beat Sync"), p->ui.page);
    p->beatSyncGroup->setGeometry(QRect(10, 550, 380, 100));

    p->checkBeatSyncEnabled = new QCheckBox(tr("Enable BPM analysis and beat sync cue"), p->beatSyncGroup);
    p->checkBeatSyncEnabled->setGeometry(QRect(10, 20, 360, 20));

    p->checkBeatCueEnabled = new QCheckBox(tr("Cue on detected beat"), p->beatSyncGroup);
    p->checkBeatCueEnabled->setGeometry(QRect(10, 42, 250, 20));

    p->checkBeatAnalyzeTempo = new QCheckBox(tr("Analyze BPM automatically on load"), p->beatSyncGroup);
    p->checkBeatAnalyzeTempo->setGeometry(QRect(10, 64, 280, 20));
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
    settings.setValue("MonitorOutputDevice", p->ui.monitorOutputDevice->currentText());

    //Common
    settings.setValue("language", p->ui.comboLanguage->currentIndex());

    //save fade slider
    settings.setValue("faderTimeSlider", p->ui.faderTimeSlider->value());
    settings.setValue("faderEndSlider", p->ui.faderEndSlider->value());
    settings.setValue("Transition/DefaultDurationSeconds", p->ui.faderTimeSlider->value());
    settings.setValue("Transition/LeadInSeconds", p->ui.faderEndSlider->value());
    settings.setValue("Transition/BeatBlendEnabled", p->checkBeatBlend->isChecked());
    settings.setValue("Transition/BassSwapEnabled", p->checkBassSwap->isChecked());
    settings.setValue("Transition/VocalHandoffEnabled", p->checkVocalHandoff->isChecked());
    settings.setValue("Transition/EqualPowerEnabled", p->checkEqualPower->isChecked());
    settings.setValue("Transition/HardCutEnabled", p->checkHardCut->isChecked());
    settings.setValue("Transition/Style", p->transitionStyle->currentData());
    settings.setValue("Transition/MinimumDurationSeconds", p->minimumTransitionDuration->value());
    settings.setValue("Transition/MaximumDurationSeconds", p->maximumTransitionDuration->value());
    settings.setValue("Transition/MaximumTempoCorrectionPercent", p->maximumTempoCorrection->value());

    //Playlist settings
    settings.setValue("checkAutoRemove", p->ui.checkAutoRemove->isChecked());

    //Silent settings
    settings.setValue("checkAutoCue", p->ui.checkAutoCue->isChecked());
    settings.setValue("checkSkipSilentEnd", p->ui.checkSkipSilentEnd->isChecked());
    settings.setValue("beatSyncEnabled", p->checkBeatSyncEnabled->isChecked());
    settings.setValue("beatSyncCueEnabled", p->checkBeatCueEnabled->isChecked());
    settings.setValue("beatSyncAnalyzeTempo", p->checkBeatAnalyzeTempo->isChecked());

    //AutoDJ
    settings.setValue("minTracks", p->ui.minTracks->value());
    settings.setValue("countDJ", p->ui.countDJ->value());
    settings.setValue("isEnabledAutoDJCount", p->ui.checkAutoDjCountPlayed->isChecked());
    settings.beginGroup("AutoDJ");
    int maxDj = p->ui.countDJ->value();

    for (int d = 0; d < maxDj; d++) {
        settings.beginGroup(QString::number(d));
        QListWidgetItem* item = p->ui.listDjNames->item(d);
        if (DjSettings* djs = dynamic_cast<DjSettings*>(p->ui.listDjNames->itemWidget(item))) {
            settings.setValue("Name", djs->name());
            settings.setValue("FilterCount", djs->filterCount());
        }
        settings.endGroup();
    }
    settings.endGroup();

    //CollectionFolders
    settings.setValue("Dirs", p->model->dirsChecked());
    settings.setValue("Monitor", p->ui.chkMonitor->isChecked());

    //File Browser
    settings.setValue("editBrowerRoot", p->ui.txtBrowserRoot->text());

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
    p->ui.comboLanguage->setCurrentIndex(settings.value("language", 0).toInt());

    //fade slider
    const QVariant defaultDuration = settings.value(
        "Transition/DefaultDurationSeconds", settings.value("faderTimeSlider", 12));
    const QVariant leadIn = settings.value(
        "Transition/LeadInSeconds", settings.value("faderEndSlider", 12));
    p->ui.faderTimeSlider->setValue(defaultDuration.toInt());
    p->ui.faderTimeLabel->setText(defaultDuration.toString() + "s");
    p->ui.faderEndSlider->setValue(leadIn.toInt());
    p->ui.faderEndLabel->setText(leadIn.toString() + "s");
    p->checkBeatBlend->setChecked(settings.value("Transition/BeatBlendEnabled", true).toBool());
    p->checkBassSwap->setChecked(settings.value("Transition/BassSwapEnabled", true).toBool());
    p->checkVocalHandoff->setChecked(settings.value("Transition/VocalHandoffEnabled", true).toBool());
    p->checkEqualPower->setChecked(settings.value("Transition/EqualPowerEnabled", true).toBool());
    p->checkHardCut->setChecked(settings.value("Transition/HardCutEnabled", true).toBool());
    const QString style = settings.value("Transition/Style", "balanced").toString();
    const int styleIndex = p->transitionStyle->findData(style);
    p->transitionStyle->setCurrentIndex(styleIndex >= 0 ? styleIndex : 0);
    p->minimumTransitionDuration->setValue(
        settings.value("Transition/MinimumDurationSeconds", 1).toInt());
    p->maximumTransitionDuration->setValue(
        settings.value("Transition/MaximumDurationSeconds", 30).toInt());
    p->maximumTempoCorrection->setValue(
        settings.value("Transition/MaximumTempoCorrectionPercent", 12).toInt());

    //Playlist setting
    p->ui.checkAutoRemove->setChecked(settings.value("checkAutoRemove", true).toBool());

    //Silent setting
    p->ui.checkSkipSilentEnd->setChecked(settings.value("checkSkipSilentEnd", true).toBool());
    p->ui.checkAutoCue->setChecked(settings.value("checkAutoCue", true).toBool());
    p->checkBeatSyncEnabled->setChecked(settings.value("beatSyncEnabled", true).toBool());
    p->checkBeatCueEnabled->setChecked(settings.value("beatSyncCueEnabled", true).toBool());
    p->checkBeatAnalyzeTempo->setChecked(settings.value("beatSyncAnalyzeTempo", true).toBool());

    //AutoDJ
    p->ui.minTracks->setValue(settings.value("minTracks", "6").toInt());
    p->ui.countDJ->setValue(settings.value("countDJ", "3").toInt());
    p->ui.checkAutoDjCountPlayed->setChecked(settings.value("isEnabledAutoDJCount", false).toBool());

    //CollectionFolders
    p->model->setDirsChecked(settings.value("Dirs").toStringList());
    p->ui.chkMonitor->setChecked(settings.value("Monitor").toBool());

    //File Browser
    p->ui.txtBrowserRoot->setText(settings.value("editBrowerRoot", "").toString());

    //Load Dj list
    loadDjList(p->ui.countDJ->value());

    return true;
}

void SettingsDialog::loadDjList(int count)
{
    QSettings settings;
    QListWidgetItem* itm;
    DjSettings* djs;

    p->ui.listDjNames->clear();

    settings.beginGroup("AutoDJ");
    for (int d = 0; d < count; d++) {
        settings.beginGroup(QString::number(d));
        itm = new QListWidgetItem(p->ui.listDjNames);
        p->ui.listDjNames->addItem(itm);
        djs = new DjSettings(p->ui.listDjNames);
        djs->setID(d + 1);
        djs->setName(settings.value("Name", QString("Dj%1").arg(d + 1)).toString());
        djs->setFilterCount(settings.value("FilterCount", "2").toInt());
        p->ui.listDjNames->setItemWidget(itm, djs);
        settings.endGroup();
    }
    settings.endGroup();
}

void SettingsDialog::on_pushButton_clicked()
{
    QFileDialog dialog(this);
    dialog.setFileMode(QFileDialog::Directory);
    if (dialog.exec())
        p->ui.txtBrowserRoot->setText(dialog.selectedFiles().first());
}

void SettingsDialog::on_pushAbout_clicked()
{
    QMessageBox msgBox;
    msgBox.setIconPixmap(QIcon(":knowthelist.png").pixmap(65, 65));
    msgBox.setText(QString("%1").arg("<h3>Knowthelist</h3>"
                                     "         Version "
        + QApplication::applicationVersion() + "<br />Copyright (C) 2005-2026 Mario Stephan "
                                               "<br /><a href='mailto:mstephan@shared-files.de'>mstephan@shared-files.de</a>"
                                               "<br /><br /><a href='http://knowthelist.github.io/knowthelist'>"
                                               "http://knowthelist.github.io/knowthelist</a>"
                                               "<br /><br /><div style='font-size:9px;'>Thanks to :"
                                               "<br />* Heiko Fischer   (for testing and new ideas)"
                                               "<br />* David Geiger and Adrien Daugabel   (for French translation and issue reports)"
                                               "<br />* Pavel Fric   (for Czech translation)"
                                               "<br />* László Farkas   (for Hungarian translation)</div>"));
    msgBox.setWindowTitle(tr("About Knowthelist"));
    msgBox.exec();
}

void SettingsDialog::onScanNow()
{
    QSettings settings;
    settings.setValue("Dirs", p->model->dirsChecked());
    Q_EMIT scanNowPressed();
}

void SettingsDialog::on_pushResetAnalysisCache_clicked()
{
    qDebug() << Q_FUNC_INFO << "Reset analysis cache prompt opened";
    const auto result = QMessageBox::warning(this,
                                             tr("Reset analysis cache"),
                                             tr("Delete all BPM and waveform analysis cache data?"),
                                             QMessageBox::Yes | QMessageBox::No,
                                             QMessageBox::No);
    if (result == QMessageBox::Yes) {
        qDebug() << Q_FUNC_INFO << "Reset analysis cache confirmed";
        Q_EMIT resetAnalysisCachePressed();
    } else {
        qDebug() << Q_FUNC_INFO << "Reset analysis cache canceled";
    }
}

void SettingsDialog::on_faderEndSlider_sliderMoved(int position)
{
    p->ui.faderEndLabel->setText(QString::number(position) + "s");
}

void SettingsDialog::on_faderTimeSlider_sliderMoved(int position)
{
    p->ui.faderTimeLabel->setText(QString::number(position) + "s");
}

void SettingsDialog::tableSelectionChanged()
{
    QTableWidgetItem* item = p->ui.settingsGroupsTable->selectedItems().first();
    p->ui.pages->setCurrentIndex(item->row());
}
