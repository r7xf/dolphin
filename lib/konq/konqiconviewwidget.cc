/* This file is part of the KDE projects
   Copyright (C) 1998, 1999 Torben Weis <weis@kde.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
    General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/
#include "konqiconviewwidget.h"

#include <qapplication.h>
#include <qclipboard.h>
#include <qfile.h>
#include <qdragobject.h>
#include <qlist.h>
#include <qregion.h>

#include <kapp.h>
#include <kcursor.h>
#include <kdebug.h>
#include <kio/job.h>
#include <kio/paste.h>
#include <klocale.h>
#include <kfileivi.h>
#include <konqfileitem.h>
#include <kmessagebox.h>
#include <konqdefaults.h>
#include <konqsettings.h>
#include <konqdrag.h>
#include <konqoperations.h>
#include <kglobalsettings.h>
#include <kpropsdlg.h>
#include <kipc.h>

#include <assert.h>
#include <unistd.h>

KonqIconViewWidget::KonqIconViewWidget( QWidget * parent, const char * name, WFlags f )
    : KIconView( parent, name, f ),
      m_rootItem( 0L ),
      m_bImagePreviewAllowed( false )
{
    QObject::connect( this, SIGNAL( dropped( QDropEvent *, const QValueList<QIconDragItem> & ) ),
		      this, SLOT( slotDropped( QDropEvent*, const QValueList<QIconDragItem> & ) ) );
	
    QObject::connect( this, SIGNAL( selectionChanged() ),
                      this, SLOT( slotSelectionChanged() ) );

    QObject::connect(
      horizontalScrollBar(),  SIGNAL(valueChanged(int)),
      this,                   SLOT(slotViewportScrolled(int)));

    QObject::connect(
      verticalScrollBar(),  SIGNAL(valueChanged(int)),
      this,                 SLOT(slotViewportScrolled(int)));

    kapp->addKipcEventMask( KIPC::IconChanged );
    connect( kapp, SIGNAL(iconChanged(int)), SLOT(slotIconChanged(int)) );
    connect( this, SIGNAL(onItem(QIconViewItem *)), SLOT(slotOnItem(QIconViewItem *)) );
    connect( this, SIGNAL(onViewport()), SLOT(slotOnViewport()) );

    // hardcoded settings
    setSelectionMode( QIconView::Extended );
    setItemTextPos( QIconView::Bottom );

    int sz = KGlobal::iconLoader()->currentSize( KIcon::Desktop );
    setGridX( sz + 20 );
    setGridY( sz + 20 );
    setAutoArrange( true );
    setSorting( true, sortDirection() );
    m_bSortDirsFirst = true;
    m_size = 0; // default is DesktopIcon size
    m_pActiveItem = 0;
    m_LineupMode = LineupBoth;
    // configurable settings
    initConfig();
    // emit our signals
    slotSelectionChanged();
    m_iconPositionGroupPrefix = QString::fromLatin1( "IconPosition::" );
}

void KonqIconViewWidget::slotIconChanged( int group )
{
    if (group != KIcon::Desktop)
	return;

    QIconViewItem *it;
    for (it=firstItem(); it; it = it->nextItem())
    {
	KFileIVI *ivi = static_cast<KFileIVI *>(it);
	ivi->setIcon( m_size, KIcon::DefaultState, m_bImagePreviewAllowed );
    }

    int sz = KGlobal::iconLoader()->currentSize( KIcon::Desktop );
    setGridX( sz + 20 );
    setGridY( sz + 20 );
    updateContents();
}

void KonqIconViewWidget::slotOnItem( QIconViewItem *item )
{
    if (m_pActiveItem != 0L)
	m_pActiveItem->setIcon( m_size, KIcon::DefaultState, m_bImagePreviewAllowed, false, true );
    m_pActiveItem = static_cast<KFileIVI *>(item);
    m_pActiveItem->setIcon( m_size, KIcon::ActiveState, m_bImagePreviewAllowed, false, true );
}

void KonqIconViewWidget::slotOnViewport()
{
    if (m_pActiveItem == 0L)
	return;
    m_pActiveItem->setIcon( m_size, KIcon::DefaultState, m_bImagePreviewAllowed, false, true );
    m_pActiveItem = 0L;
}

void KonqIconViewWidget::clear()
{
    KIconView::clear();
    m_pActiveItem = 0L;
}

void KonqIconViewWidget::takeItem( QIconViewItem *item )
{
    if ( m_pActiveItem == static_cast<KFileIVI *>(item) )
        m_pActiveItem = 0L;
    KIconView::takeItem( item );
}

void KonqIconViewWidget::initConfig()
{
    m_pSettings = KonqFMSettings::settings();

    // Color settings
    QColor normalTextColor	 = m_pSettings->normalTextColor();
    // QColor highlightedTextColor	 = m_pSettings->highlightedTextColor();
    setItemColor( normalTextColor );

    /*
      // What does this do ? (David)
      if ( m_bgPixmap.isNull() )
      viewport()->setBackgroundMode( PaletteBackground );
      else
      viewport()->setBackgroundMode( NoBackground );
    */

    // Font settings
    QFont font( m_pSettings->standardFont() );
    font.setUnderline( m_pSettings->underlineLink() );
    setItemFont( font );

    setWordWrapIconText( m_pSettings->wordWrapText() );
}

void KonqIconViewWidget::setIcons( int size )
{
    m_size = size;
    for ( QIconViewItem *it = firstItem(); it; it = it->nextItem() ) {
        KFileIVI * ivi = static_cast<KFileIVI *>( it );
	ivi->setIcon( size, ivi->state(), m_bImagePreviewAllowed );
    }
}

void KonqIconViewWidget::refreshMimeTypes()
{
    for ( QIconViewItem *it = firstItem(); it; it = it->nextItem() )
	(static_cast<KFileIVI *>( it ))->item()->refreshMimeType();
    setIcons( m_size );
}

void KonqIconViewWidget::setURL( const KURL &kurl )
{
  m_url = kurl;
  if ( m_url.isLocalFile() )
    m_dotDirectoryPath = m_url.path().append( ".directory" );
  else
    m_dotDirectoryPath = QString::null;
}

void KonqIconViewWidget::setImagePreviewAllowed( bool b )
{
    m_bImagePreviewAllowed = b;
    for ( QIconViewItem *it = firstItem(); it; it = it->nextItem() ) {
	(static_cast<KFileIVI *>( it ))->setIcon( m_size,
		KIcon::DefaultState, m_bImagePreviewAllowed );
    }
}

KFileItemList KonqIconViewWidget::selectedFileItems()
{
    KFileItemList lstItems;

    QIconViewItem *it = firstItem();
    for (; it; it = it->nextItem() )
	if ( it->isSelected() ) {
	    KFileItem *fItem = (static_cast<KFileIVI *>(it))->item();
	    lstItems.append( fItem );
	}
    return lstItems;
}

void KonqIconViewWidget::slotDropped( QDropEvent *ev, const QValueList<QIconDragItem> & )
{
    // Drop on background
    if ( m_rootItem ) // otherwise, it's too early, '.' is not yet listed
        KonqOperations::doDrop( m_rootItem, ev, this );
}

void KonqIconViewWidget::slotDropItem( KFileIVI *item, QDropEvent *ev )
{
    assert( item );
    KonqOperations::doDrop( item->item(), ev, this );
}

void KonqIconViewWidget::drawBackground( QPainter *p, const QRect &r )
{
    const QPixmap *pm = viewport()->backgroundPixmap();
    if (!pm || pm->isNull()) {
	p->fillRect(r, viewport()->backgroundColor());
	return;
    }

    int ax = (r.x() % pm->width());
    int ay = (r.y() % pm->height());
    p->drawTiledPixmap(r, *pm, QPoint(ax, ay));
}

QDragObject * KonqIconViewWidget::dragObject()
{
    if ( !currentItem() )
	return 0;

    KonqDrag *drag = new KonqDrag( viewport() );
    // Position of the mouse in the view
    QPoint orig = viewportToContents( viewport()->mapFromGlobal( QCursor::pos() ) );
    // Position of the item clicked in the view
    QPoint itempos = currentItem()->pixmapRect( FALSE ).topLeft();
    // Set pixmap, with the correct offset
    drag->setPixmap( *currentItem()->pixmap(), orig - itempos );
    // Append all items to the drag object
    for ( QIconViewItem *it = firstItem(); it; it = it->nextItem() ) {
	if ( it->isSelected() ) {
          QString itemURL = (static_cast<KFileIVI *>(it))->item()->url().url();
          QIconDragItem id;
          id.setData( QCString(itemURL.latin1()) );
          drag->append( id,
                        QRect( it->pixmapRect( FALSE ).x() - orig.x(),
                               it->pixmapRect( FALSE ).y() - orig.y(),
                               it->pixmapRect().width(), it->pixmapRect().height() ),
                        QRect( it->textRect( FALSE ).x() - orig.x(),
                               it->textRect( FALSE ).y() - orig.y(),	
                               it->textRect().width(), it->textRect().height() ),
                        itemURL );
	}
    }
    return drag;
}

void KonqIconViewWidget::contentsDragEnterEvent( QDragEnterEvent *e )
{
    if ( e->provides( "text/uri-list" ) )
    {
        QByteArray payload = e->encodedData( "text/uri-list" );
        if ( !payload.size() )
            kdError() << "Empty data !" << endl;
        // Cache the URLs, since we need them every time we move over a file
        // (see KFileIVI)
        bool ok = KonqDrag::decode( e, m_lstDragURLs );
        if( !ok )
            kdError() << "Couldn't decode urls dragged !" << endl;
    }
    KIconView::contentsDragEnterEvent( e );
}

void KonqIconViewWidget::setItemFont( const QFont &f )
{
    setFont( f );
}

void KonqIconViewWidget::setItemColor( const QColor &c )
{
    iColor = c;
}

QColor KonqIconViewWidget::itemColor() const
{
    return iColor;
}


void KonqIconViewWidget::slotSelectionChanged()
{
    // This code is very related to TreeViewBrowserExtension::updateActions
    bool cutcopy, del;
    bool bInTrash = false;
    int iCount = 0;
    KFileItem * firstSelectedItem = 0L;

    for ( QIconViewItem *it = firstItem(); it; it = it->nextItem() )
    {
	if ( it->isSelected() )
        {
	    iCount++;
            if ( ! firstSelectedItem )
                firstSelectedItem = (static_cast<KFileIVI *>( it ))->item();

            if ( (static_cast<KFileIVI *>( it ))->item()->url().directory(false) == KGlobalSettings::trashPath() )
                bInTrash = true;
        }
    }
    cutcopy = del = ( iCount > 0 );

    emit enableAction( "cut", cutcopy );
    emit enableAction( "copy", cutcopy );
    emit enableAction( "trash", del && !bInTrash );
    emit enableAction( "del", del );
    emit enableAction( "shred", del );

    bool bKIOClipboard = !KIO::isClipboardEmpty();
    QMimeSource *data = QApplication::clipboard()->data();
    bool paste = ( bKIOClipboard || data->encodedData( data->format() ).size() != 0 ) &&
	(iCount <= 1); // We can't paste to more than one destination, can we ?

    emit enableAction( "pastecopy", paste );
    emit enableAction( "pastecut", paste );

    KFileItemList lstItems;
    if ( firstSelectedItem )
        lstItems.append( firstSelectedItem );
    emit enableAction( "properties", ( iCount == 1 ) &&
                       KPropertiesDialog::canDisplay( lstItems ) );
    emit enableAction( "editMimeType", ( iCount == 1 ) );

    // TODO : if only one url, check that it's a dir
}

void KonqIconViewWidget::cutSelection()
{
    //TODO: grey out items
    copySelection();
}

void KonqIconViewWidget::copySelection()
{
    kdDebug() << " -- KonqIconViewWidget::copySelection() -- " << endl;
    QDragObject * obj = dragObject();
    QApplication::clipboard()->setData( obj );
}

void KonqIconViewWidget::pasteSelection( bool move )
{
    KURL::List lst = selectedUrls();
    assert ( lst.count() <= 1 );
    if ( lst.count() == 1 )
	KIO::pasteClipboard( lst.first(), move );
    else
	KIO::pasteClipboard( url(), move );
}

KURL::List KonqIconViewWidget::selectedUrls()
{
    KURL::List lstURLs;

    for ( QIconViewItem *it = firstItem(); it; it = it->nextItem() )
	if ( it->isSelected() )
	    lstURLs.append( (static_cast<KFileIVI *>( it ))->item()->url() );
    return lstURLs;
}

QRect KonqIconViewWidget::iconArea() const
{
    return m_IconRect;
}

void KonqIconViewWidget::setIconArea(const QRect &rect)
{
    m_IconRect = rect;
}

int KonqIconViewWidget::lineupMode() const
{
    return m_LineupMode;
}

void KonqIconViewWidget::setLineupMode(int mode)
{
    m_LineupMode = mode;
}

bool KonqIconViewWidget::sortDirectoriesFirst() const
{
  return m_bSortDirsFirst;
}

void KonqIconViewWidget::setSortDirectoriesFirst( bool b )
{
  m_bSortDirsFirst = b;
}

void KonqIconViewWidget::slotViewportScrolled(int)
{
  emit(viewportAdjusted());
}

void KonqIconViewWidget::viewportResizeEvent(QResizeEvent * e)
{
  KIconView::viewportResizeEvent(e);
  emit(viewportAdjusted());
}

void KonqIconViewWidget::contentsDropEvent( QDropEvent *e )
{
  KIconView::contentsDropEvent( e );
  emit dropped();
}

void KonqIconViewWidget::slotSaveIconPositions()
{
  if ( m_dotDirectoryPath.isEmpty() )
    return;
  KSimpleConfig dotDirectory( m_dotDirectoryPath );
  QIconViewItem *it = firstItem();
  while ( it )
  {
    KFileIVI *ivi = static_cast<KFileIVI *>( it );
    KonqFileItem *item = ivi->item();

    dotDirectory.setGroup( QString( m_iconPositionGroupPrefix ).append( item->url().fileName() ) );
    dotDirectory.writeEntry( "X", it->x() );
    dotDirectory.writeEntry( "Y", it->y() );
    dotDirectory.writeEntry( "Exists", true );

    it = it->nextItem();
  }

  QStringList groups = dotDirectory.groupList();
  QStringList::ConstIterator gIt = groups.begin();
  QStringList::ConstIterator gEnd = groups.end();
  for (; gIt != gEnd; ++gIt )
    if ( (*gIt).left( m_iconPositionGroupPrefix.length() ) == m_iconPositionGroupPrefix )
    {
      dotDirectory.setGroup( *gIt );
      if ( dotDirectory.hasKey( "Exists" ) )
        dotDirectory.deleteEntry( "Exists", false );
      else
        dotDirectory.deleteGroup( *gIt );
    }

  dotDirectory.sync();
}

// Adapted version of QIconView::insertInGrid, that works relative to
// m_IconRect, instead of the entire viewport.

void KonqIconViewWidget::insertInGrid(QIconViewItem *item)
{
    if (0L == item)
	return;

    if (!m_IconRect.isValid())
    {
	QIconView::insertInGrid(item);
	return;
    }

    QRegion r(m_IconRect);
    QIconViewItem *i = firstItem();
    int y = -1;
    for (; i; i = i->nextItem() )
    {
	r = r.subtract(i->rect());
	y = QMAX(y, i->y() + i->height());
    }

    QArray<QRect> rects = r.rects();
    QArray<QRect>::Iterator it = rects.begin();
    bool foundPlace = FALSE;
    for (; it != rects.end(); ++it)
    {
	QRect rect = *it;
	if (rect.width() >= item->width() && rect.height() >= item->height())
	{
	    int sx = 0, sy = 0;
	    if (rect.width() >= item->width() + spacing())
		sx = spacing();
	    if (rect.height() >= item->height() + spacing())
		sy = spacing();
	    item->move(rect.x() + sx, rect.y() + sy);
	    foundPlace = true;
	    break;
	}
    }

    if (!foundPlace)
	item->move(m_IconRect.topLeft());

    //item->dirty = false;
    return;
}


/*
 * Utility class QIVItemBin is used by KonqIconViewWidget::lineupIcons().
 */

class QIVItemBin
{
public:
    QIVItemBin() {}
    ~QIVItemBin() {}

    int count() { return mData.count(); }
    void add(QIconViewItem *item) { mData.append(item); }

    QIconViewItem *top();
    QIconViewItem *bottom();
    QIconViewItem *left();
    QIconViewItem *right();

private:
    QList<QIconViewItem> mData;
};

QIconViewItem *QIVItemBin::top()
{
    if (mData.count() == 0)
	return 0L;

    QIconViewItem *it = mData.first();
    QIconViewItem *item = it;
    int y = it->y();
    for (it=mData.next(); it; it=mData.next())
    {
	if (it->y() < y)
	{
	    y = it->y();
	    item = it;
	}
    }
    mData.remove(item);
    return item;
}

QIconViewItem *QIVItemBin::bottom()
{
    if (mData.count() == 0)
	return 0L;

    QIconViewItem *it = mData.first();
    QIconViewItem *item = it;
    int y = it->y();
    for (it=mData.next(); it; it=mData.next())
    {
	if (it->y() > y)
	{
	    y = it->y();
	    item = it;
	}
    }
    mData.remove(item);
    return item;
}

QIconViewItem *QIVItemBin::left()
{
    if (mData.count() == 0)
	return 0L;

    QIconViewItem *it=mData.first();
    QIconViewItem *item = it;
    int x = it->x();
    for (it=mData.next(); it; it=mData.next())
    {
	if (it->x() < x)
	{
	    x = it->x();
	    item = it;
	}
    }
    mData.remove(item);
    return item;
}

QIconViewItem *QIVItemBin::right()
{
    if (mData.count() == 0)
	return 0L;

    QIconViewItem *it=mData.first();
    QIconViewItem *item = it;
    int x = it->x();
    for (it=mData.next(); it; it=mData.next())
    {
	if (it->x() > x)
	{
	    x = it->x();
	    item = it;
	}
    }
    mData.remove(item);
    return item;
}
	

/*
 * The algorithm used for lineing up the icons could be called
 * "beating flat the icon field". Imagine the icon field to be some height
 * field on a regular grid, with the height being the number of icons in
 * each grid element. Now imagine slamming on the field with a shovel or
 * some other flat surface. The high peaks will be flattened and spread out
 * over their adjacent areas. This is basically what the algorithm tries to
 * simulate.
 *
 * First, the icons are binned to a grid of the desired size. If all bins
 * are containing at most one icon, we're done, of course. We just have to
 * move all icons to the center of each grid element.
 * For each bin which has more than one icon in it, we calculate 4
 * "friction coefficients", one for each cardinal direction. The friction
 * coefficient of a direction is the number of icons adjacent in that
 * direction. The idea is that this number is somewhat a measure in which
 * direction the icons should flow: icons flow in the direction of lowest
 * friction coefficient. We move a maximum of one icon per bin and loop over
 * all bins. This procedure is repeated some maximum number of times or until
 * no icons are moved anymore.
 *
 * I don't know if this algorithm is good or bad, I don't even know if it will
 * work all the time. It seems a correct thing to do, however, and it seems to
 * work particularly well. In any case, the number of runs is limited so there
 * can be no races.
 */

#define MIN2(a,b)   ((a) < (b) ? (a) : (b))
#define MIN3(a,b,c) ((a) < MIN2((b),(c)) ? (a) : ((b) < MIN2((a),(c)) ? (b) : (c)))

void KonqIconViewWidget::lineupIcons()
{
    int dx = gridX() + spacing();
    int dy = gridY() + spacing();
    kdDebug(1203) << "dx = " << dx << ", dy = " << dy << "\n";

    if ((dx < 15) || (dy < 15))
    {
	kdWarning(1203) << "Do you really have that fine a grid?\n";
	return;
    }

    int x1, x2, y1, y2;
    if (m_IconRect.isValid())
    {
	x1 = m_IconRect.left(); x2 = m_IconRect.right();
	y1 = m_IconRect.top(); y2 = m_IconRect.bottom();
    } else
    {
	x1 = 0; x2 = viewport()->width();
	y1 = 0; y2 = viewport()->height();
    }
	
    int nx = (x2 - x1) / dx;
    int ny = (y2 - y1) / dy;

    kdDebug(1203) << "nx = " << nx << " ny = " << ny << "\n";
    if ((nx > 150) || (ny > 100))
    {
	kdWarning(1203) << "Do you really have that fine a grid?\n";
	return;
    }
    if ((nx <= 1) || (ny <= 1))
    {
	kdWarning(1203) << "Iconview is too small, not doing anything.\n";
	return;
    }

    // Create a grid of (ny x nx) bins.
    typedef QIVItemBin *QIVItemPtr;
    QIVItemPtr (*bins)[nx] = new QIVItemPtr[ny][nx];

    int i, j;
    for (j=0; j<ny; j++)
    {
	for (i=0; i<nx; i++)
	    bins[j][i] = 0L;
    }

    // Put each ivi in its corresponding bin.
    QIconViewItem *item;
    for (item=firstItem(); item; item=item->nextItem())
    {
	i = (item->x() + item->width()/2 - x1) / dx;
	if (i < 0) i = 0;
	else if (i >= nx) i = nx - 1;
	j = (item->y() + item->height()/2 - y1) / dy;
	if (j < 0) j = 0;
	else if (j >= ny) j = ny - 1;

	if (bins[j][i] == 0L)
	    bins[j][i] = new QIVItemBin;
	bins[j][i]->add(item);
    }

    // The shuffle code
    int n, k;
    int infinity = 100000, nmoves = 1;
    for (n=0; (n < 10) && (nmoves != 0); n++)
    {
	nmoves = 0;
	for (j=0; j<ny; j++)
	{
	    for (i=0; i<nx; i++)
	    {
		if (!bins[j][i] || (bins[j][i]->count() < 2))
		    continue;

		// Calculate the 4 "friction coefficients".
		int tf = 0;
		for (k=j-1; (k >= 0) && bins[k][i] && bins[k][i]->count(); k--)
		    tf += bins[k][i]->count();
		if (k == -1)
		    tf += infinity;

		int bf = 0;
		for (k=j+1; (k < ny) && bins[k][i] && bins[k][i]->count(); k++)
		    bf += bins[k][i]->count();
		if (k == ny)
		    bf += infinity;

		int lf = 0;
		for (k=i-1; (k >= 0) && bins[j][k] && bins[j][k]->count(); k--)
		    lf += bins[j][k]->count();
		if (k == -1)
		    lf += infinity;

		int rf = 0;
		for (k=i+1; (k < nx) && bins[j][k] && bins[j][k]->count(); k++)
		    rf += bins[j][k]->count();
		if (k == nx)
		    rf += infinity;

		// If we are stuck between walls, continue
		if ( (tf >= infinity) && (bf >= infinity) &&
		     (lf >= infinity) && (rf >= infinity)
		   )
		    continue;

		// Is there a preferred lineup direction?
		if (m_LineupMode == LineupHorizontal)
		{
		    tf += infinity;
		    bf += infinity;
		} else if (m_LineupMode == LineupVertical)
		{
		    lf += infinity;
		    rf += infinity;
		}
		
		// Move one item in the direction of the least friction.
		if (tf <= MIN3(bf,lf,rf))
		{
		    if (!bins[j-1][i])
			bins[j-1][i] = new QIVItemBin;
		    bins[j-1][i]->add(bins[j][i]->top());
		} else if (bf <= MIN3(tf,lf,rf))
		{
		    if (!bins[j+1][i])
			bins[j+1][i] = new QIVItemBin;
		    bins[j+1][i]->add(bins[j][i]->bottom());
		} else if (lf <= MIN3(tf,bf,rf))
		{
		    if (!bins[j][i-1])
			bins[j][i-1] = new QIVItemBin;
		    bins[j][i-1]->add(bins[j][i]->left());
		} else
		{
		    if (!bins[j][i+1])
			bins[j][i+1] = new QIVItemBin;
		    bins[j][i+1]->add(bins[j][i]->right());
		}

		nmoves++;
	    }
	}
	kdDebug(1203) << "nmoves = " << nmoves << "\n";
    }

    // Perform the actual moving
    n = 0;
    int x, y;
    for (j=0; j<ny; j++)
    {
	for (i=0; i<nx; i++)
	{
	    if (!bins[j][i] || !bins[j][i]->count())
		continue;

	    item = bins[j][i]->top();
	    x = x1 + i*dx + spacing(); y = y1 + j*dy + spacing();
	    if (item->pos() != QPoint(x, y))
	    {
		item->move(x, y);
	    }
	    if (bins[j][i]->count())
	    {
		kdDebug(1203) << "Lineup incomplete..\n";
		item = bins[j][i]->top();
		for (k=1; item; k++)
		{
		    x = x1 + i*dx + spacing() + 10*k; y = y1 + j*dy + spacing() + 5*k;
		    if (item->pos() != QPoint(x, y))
		    {
			item->move(x, y);
		    }
		    item = bins[j][i]->top();
		}
	    }
	    delete bins[j][i];
	    n++;
	}
    }

    updateContents();
    delete[] bins;
    kdDebug(1203) << n << " icons successfully moved.\n";
    return;
}


#include "konqiconviewwidget.moc"
