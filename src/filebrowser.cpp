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

#include "filebrowser.h"
#include <Qt>
#include <QtGui>

struct FileBrowserPrivate
{
        QVBoxLayout *layout;
        QTreeView *filetree;
        QFileSystemModel *model;
};

FileBrowser::FileBrowser(QWidget *parent) :
    QWidget(parent)
  , p( new FileBrowserPrivate )
{
     p->layout = new QVBoxLayout;

    // ToDo: maybe we add some buttons and links here

      p->model = new QFileSystemModel;
      p->model->setRootPath(QDir::currentPath());
      p->filetree = new QTreeView(this);
      p->filetree->setModel(p->model);
      p->filetree->setDragEnabled(true);
      p->filetree->setSelectionMode(QAbstractItemView::ContiguousSelection);
      p->filetree->header()->resizeSection(0,400);
      p->filetree->setRootIndex(p->model->index(QDesktopServices::storageLocation(QDesktopServices::MusicLocation)));
      p->filetree->setAttribute(Qt::WA_MacShowFocusRect, false);
      p->layout->addWidget(p->filetree);

      setLayout(p->layout);
      setAttribute(Qt::WA_MacShowFocusRect, false);
}

void FileBrowser::setRootPath(QString path){

    p->filetree->setRootIndex(p->model->setRootPath(path));

}
