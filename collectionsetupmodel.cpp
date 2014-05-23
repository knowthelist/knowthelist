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
#include <QTreeView>
#include <QDirModel>
#include <Qt>
#include <QDebug>

CollectionSetupModel::CollectionSetupModel( QWidget *parent )
{

}

Qt::ItemFlags CollectionSetupModel::flags(const QModelIndex& index) const
{
        Qt::ItemFlags f = QDirModel::flags(index);
        if (index.column() == 0) // make the first column checkable
                f |= Qt::ItemIsUserCheckable;
        return f;
}

QVariant CollectionSetupModel::data(const QModelIndex& index, int role) const
{
        if (index.isValid() && index.column() == 0 && role == Qt::CheckStateRole)
        {
            // the item is checked only if we have stored its path
            if (checkedPartially.contains(filePath(index)) )
            {
                return Qt::PartiallyChecked;
            }
            else
                return (checked.contains(filePath(index)) ? Qt::Checked : Qt::Unchecked);
        }
        return QDirModel::data(index, role);
}

bool CollectionSetupModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
        if (index.isValid() && index.column() == 0 && role == Qt::CheckStateRole)
        {
                // store checked paths, remove unchecked paths
                QModelIndex idx=parent(index);
                if (value.toInt() == Qt::Checked)
                {
                        checked.insert(filePath(index));
                        // make parents always partially checked
                        while( idx.isValid() )
                        {
                            checkedPartially.insert(filePath(idx));
                            checked.remove(filePath(idx));
                            idx =idx.parent();
                        }
                }
                else
                {
                        checked.remove(filePath(index));
                        // make parent unchecked if index was his only child
                        while( idx.isValid() )
                        {
                            bool hasChildren=false;
                            foreach (const QString &value, checked)
                                    if ( value.contains(filePath(idx)) )
                                        hasChildren = true;
                            if ( !hasChildren ){
                                checkedPartially.remove(filePath(idx));
                                checked.insert(filePath(idx));
                            }
                            idx =idx.parent();
                        }
                }
                emit dataChanged(parent(index),index);
                return true;
        }
        return QDirModel::setData(index, value, role);
}

QVariant CollectionSetupModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal) {
        if (role != Qt::DisplayRole)
            return QVariant();
        switch (section) {
        case 0: return tr("Select folders for music collection");
        default: return QVariant();
        }
    }
    return QAbstractItemModel::headerData(section, orientation, role);

}

QStringList CollectionSetupModel::dirsChecked()
{
     QStringList list;
     foreach (const QString &value, checked)
         list << value;
     return list;
}

void
CollectionSetupModel::setDirsChecked(QStringList list)
{
    checked.clear();
    checkedPartially.clear();
    foreach (const QString &value, list)
    {
        /* index parents Qt::PartiallyChecked */
        QModelIndex idx=parent(index(value));
        while( idx.isValid() )
        {
           checkedPartially.insert(filePath(idx));
           idx =idx.parent();
        }
        checked.insert(value);
     }
}
