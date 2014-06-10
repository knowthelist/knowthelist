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


#include "collectionwidget.h"
#include "collectiontree.h"
#include "collectionupdater.h"
#include "progressbar.h"
#include "track.h"

#include <QTimerEvent>
#include <QPixmap>
#include <QDropEvent>
#include <Qt>
#include <QAction>
#include <QMenuBar>

class CollectionWidgetPrivate
{
    public:
        QMenu *actionsMenu;
        QAction *actionMode1;
        QAction *actionMode2;
        QAction *actionMode3;
        QAction* actionScan;
        CollectionTree *collectiontree;
        ProgressBar *progress;
        CollectionUpdater *updater;
        QTimer *timer;
        QToolButton *button;
        SearchEdit *searchEdit;
};


CollectionWidget::CollectionWidget( QWidget* parent ):
        QWidget(parent)
{
    p = new CollectionWidgetPrivate;

    QFont this_font(  this->font() );
    this_font.setPointSize( this_font.pointSize()-1 );

    p->actionsMenu = new QMenu(tr( "" ), this );

    QPushButton *pushSet =new QPushButton();
    QPixmap pixmap1(":images/settings.png");
    pushSet->setIcon(QIcon(pixmap1));
    pushSet->setIconSize(QSize(20,20));
    pushSet->setStyleSheet("QPushButton { border: none; padding: 0px; margin-left: 7px; margin-right: 14px; max-height: 20px;}");
    connect(pushSet,SIGNAL(clicked()),this,SLOT(onSetClicked()) );

    QPushButton *pushRandom =new QPushButton();
    QPixmap pixmap2(":images/shuffle.png");
    pushRandom->setIcon(QIcon(pixmap2));
    pushRandom->setIconSize(QSize(32,32));
    pushRandom->setStyleSheet("QPushButton { border: none; padding: 0px; margin-left: 7px;max-height: 20px; margin-right: 14px;}");
    pushRandom->setToolTip(tr( "Random Tracks" ));

    p->searchEdit = new SearchEdit();
    p->searchEdit->setToolTip(tr( "Enter space-separated terms to filter collection" ));

    QVBoxLayout *mainLayout = new QVBoxLayout;
    QWidget *headWidget = new QWidget(this);
    headWidget->setMaximumHeight(40);

    QHBoxLayout *headWidgetLayout = new QHBoxLayout;
    headWidgetLayout->setMargin(0);
    headWidgetLayout->setSpacing(1);

    headWidgetLayout->addWidget(pushSet);
    headWidgetLayout->addWidget(p->searchEdit);
    headWidgetLayout->addWidget(pushRandom);
    headWidget->setLayout(headWidgetLayout);

    p->updater = new CollectionUpdater();

    p->timer = new QTimer( this );

    p->collectiontree = new CollectionTree(this);

    headWidget->raise();
    mainLayout->addWidget(headWidget);

    mainLayout->setMargin(0);
    mainLayout->setSpacing(0);
 
    connect(pushRandom,SIGNAL(clicked()),p->collectiontree,SLOT(triggerRandomSelection()) );

    p->actionMode1 = new QAction( "Artist > Album",this );
    p->actionMode1->setCheckable(true);
    p->actionMode2 = new QAction( "Year > Artist > Album",this );
    p->actionMode2->setCheckable(true);
    p->actionMode3 = new QAction( "Genre > Artist > Album",this );
    p->actionMode3->setCheckable(true);

    connect(p->actionMode1, SIGNAL(triggered()),
            this, SLOT(mode1Selected()));
    connect(p->actionMode2, SIGNAL(triggered()),
            this, SLOT(mode2Selected()));
    connect(p->actionMode3, SIGNAL(triggered()),
            this, SLOT(mode3Selected()));

    QActionGroup *alignmentGroup = new QActionGroup(this);

    alignmentGroup->addAction( p->actionMode1 );
    alignmentGroup->addAction( p->actionMode2 );
    alignmentGroup->addAction( p->actionMode3 );

    p->actionsMenu->addActions(alignmentGroup->actions());

    connect( p->searchEdit, SIGNAL( textChanged( const QString& ) ),
             this,           SLOT( onSetFilterTimeout() ) );

    connect( p->searchEdit, SIGNAL( trackDropped(QString)),
             this,           SIGNAL( trackDropped(QString)) );

    connect( p->collectiontree, SIGNAL(selectionChanged(QList<Track*>)),
             this,           SIGNAL(selectionChanged(QList<Track*>)));

    connect( p->collectiontree, SIGNAL(wantLoad(QList<Track*>,QString)),
             this,           SIGNAL(wantLoad(QList<Track*>,QString)));

    connect( p->updater, SIGNAL(changesDone()), p->collectiontree, SLOT(createTrunk()));
    connect( p->collectiontree, SIGNAL(rescan()), p->updater, SLOT(scan()));

    setFocusProxy( p->collectiontree ); //default object to get focus
    setMaximumWidth(400);

    //Pogressbar for re-read collection
    p->progress = new ProgressBar(this);
    p->progress->setValue(0);


    mainLayout->addWidget(p->collectiontree);


    // Read config values
    QSettings settings;
    p->collectiontree->treeMode = static_cast<CollectionTree::mode>(settings.value("TreeMode",CollectionTree::MODENONE).toUInt());
    alignmentGroup->actions().at(0)->setChecked(p->collectiontree->treeMode==CollectionTree::MODENONE);
    alignmentGroup->actions().at(1)->setChecked(p->collectiontree->treeMode==CollectionTree::MODEYEAR);
    alignmentGroup->actions().at(2)->setChecked(p->collectiontree->treeMode==CollectionTree::MODEGENRE);

    p->collectiontree->createTrunk();
    setLayout(mainLayout);

    connect(p->updater, SIGNAL(progressChanged(int)), p->progress,SLOT(setValue(int)));
    connect(p->progress, SIGNAL(stopped()), p->updater,SLOT(stop()));
}

CollectionWidget::~CollectionWidget()
{
    QSettings settings;
    settings.setValue("TreeMode",p->collectiontree->treeMode);
    delete p;
}

void CollectionWidget::scan()
{
    if (p->progress->isHidden()){
        QSettings settings;
        p->updater->setDirectoryList( settings.value("Dirs").toStringList(), true );
    }
}

void CollectionWidget::mode1Selected()
{
    p->collectiontree->treeMode = CollectionTree::MODENONE;
    p->collectiontree->createTrunk();
}

void CollectionWidget::mode2Selected()
{
    p->collectiontree->treeMode = CollectionTree::MODEYEAR;
    p->collectiontree->createTrunk();
}

void CollectionWidget::mode3Selected()
{
    p->collectiontree->treeMode = CollectionTree::MODEGENRE;
    p->collectiontree->createTrunk();
}

void CollectionWidget::onSetFilterTimeout()
{
    if ( p->timer->isActive() ) p->timer->stop();
        p->timer->singleShot(200,this,SLOT(onSetFilter()) );
}

void CollectionWidget::onSetFilter()
{
    p->collectiontree->setFilter( p->searchEdit->text() );
    p->collectiontree->createTrunk();
}

void CollectionWidget::onSetClicked()
{
    p->actionsMenu->popup(QCursor::pos(),0);
}

bool CollectionWidget::hasItems()
{ 
  return (p->collectiontree->topLevelItemCount() > 0);
}

void CollectionWidget::loadSettings()
{
    QSettings settings;
    p->updater->setDoMonitor( settings.value("Monitor").toBool() );
    p->updater->setDirectoryList( settings.value("Dirs").toStringList());
}

void CollectionWidget::setFilterText( QString strFilter ){
    p->searchEdit->setText( strFilter );
    onSetFilter();
}

QString CollectionWidget::filterText()
{
    return p->searchEdit->text() ;
}

void CollectionWidget::resizeEvent(QResizeEvent *)
{
    QRect rec = p->collectiontree->geometry();
    p->progress->setGeometry(0,40,rec.width(),20);
}



SearchEdit::SearchEdit(QWidget *parent)
                    :QLineEdit(parent){
    QPixmap searchIcon(":images/search.png");
    QLabel* lbl = new QLabel(this);
    lbl->setScaledContents(true);
    lbl->setPixmap(searchIcon);
    lbl->setFixedSize(QSize(21,21));
    lbl->setStyleSheet("QLabel { border: none; padding: 0px; margin-left: 9px; margin-top: 8px;}");
    clearButton = new QToolButton(this);
    QPixmap pixmap(":images/clear_left.png");
      clearButton->setIcon(QIcon(pixmap));
      clearButton->setIconSize(QSize(18,18));
    clearButton->setCursor(Qt::ArrowCursor);
    clearButton->setStyleSheet("QToolButton { border: none; padding: 0px; margin-right: 2px; margin-top: 3px;}");
    clearButton->hide();
      setAttribute(Qt::WA_MacShowFocusRect, false);
      connect(clearButton, SIGNAL(clicked()), this, SLOT(clear()));
      connect(this, SIGNAL(textChanged(const QString&)), this, SLOT(updateCloseButton(const QString&)));
      int frameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
      //setStyleSheet(QString("QLineEdit { padding-right: %1px; } ").arg(clearButton->sizeHint().width() + frameWidth + 1));
      QSize msz = minimumSizeHint();
      setMinimumSize(qMax(msz.width(), clearButton->sizeHint().height() + frameWidth * 2 + 2),
                     qMax(msz.height(), clearButton->sizeHint().height() + frameWidth * 2 + 2));
    
                    }        
SearchEdit::~SearchEdit(){
    
}        

void SearchEdit::dragEnterEvent(QDragEnterEvent *event)
{
     if (event->mimeData()->hasFormat("text/playlistitem"))
        event->accept();
}

void SearchEdit::dropEvent( QDropEvent *event )
{
    if (event->mimeData()->hasFormat("text/playlistitem")) {

        //decode playlistitem
        QByteArray itemData = event->mimeData()->data("text/playlistitem");
        QDataStream stream(&itemData, QIODevice::ReadOnly);
        QVector<QStringList> tags;

        stream >> tags;
        event->setDropAction(Qt::CopyAction);
        event->accept();

        if ( tags.count() >0 ){
            Track *track = new Track(tags[0]);
            this->setText(track->artist());
        }

    }
    else
       event->ignore();

}

void SearchEdit::resizeEvent(QResizeEvent *)
   {
       QSize sz = clearButton->sizeHint();
       int frameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
       clearButton->move(rect().right() - frameWidth - sz.width(),
                         (rect().bottom() + 2 - sz.height())/2);
   }

// esc key for clean up
void SearchEdit::keyPressEvent(QKeyEvent *e)
{

  if( e->key() == Qt::Key_Escape )
      this->setText("");
   else
      QLineEdit::keyPressEvent( e );

}

 void SearchEdit::updateCloseButton(const QString &text)
   {
       clearButton->setVisible(!text.isEmpty());
   }





