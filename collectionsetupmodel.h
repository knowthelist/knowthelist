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

#ifndef COLLECTIONSETUPMODEL_H
#define COLLECTIONSETUPMODEL_H

#include <QTreeView>
#include <QDirModel>
#include <Qt>

class CollectionSetupModel : public QDirModel
{
public:
    CollectionSetupModel(QWidget *parent = 0);
    QStringList dirsChecked();
    void setDirsChecked(QStringList list);

private:
        QSet<QString> checked;
        QSet<QString> checkedPartially;
virtual Qt::ItemFlags flags(const QModelIndex& index) const;

virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;

bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole);
};

#endif // COLLECTIONSETUPMODEL_H
