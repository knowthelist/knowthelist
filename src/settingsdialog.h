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

#ifndef SETTINGSDIALOG_H_
#define SETTINGSDIALOG_H_

#include <QDialog>


class SettingsDialog : public QDialog
{
	Q_OBJECT
public:
	SettingsDialog(QWidget * parent = 0);
	~SettingsDialog();
	int exec();
    enum Tab  { TabFader = 0,
                 TabCollection = 1,
                 TabMonitor = 2
               };
    void setCurrentTab(Tab tab);
public slots:
	void accept();
    void tableSelectionChanged();

signals:
      void  scanNowPressed();
      void  resetStatsPressed();



protected:
    bool loadSettings();

private slots:
        void on_faderEndSlider_sliderMoved(int position);
        void on_faderTimeSlider_sliderMoved(int position);
        void on_pushButton_clicked();
        void onScanNow();
        void loadDjList(int count);


protected:

private:
	enum ItemRole { ItemRoleId = Qt::UserRole+1 };
    class SettingsDialogPrivate *p;
};

#endif /* SETTINGSDIALOG_H_ */
