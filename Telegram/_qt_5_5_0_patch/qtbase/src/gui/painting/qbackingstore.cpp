/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <qbackingstore.h>
#include <qwindow.h>
#include <qpixmap.h>
#include <qpa/qplatformbackingstore.h>
#include <qpa/qplatformintegration.h>
#include <qscreen.h>
#include <qdebug.h>

#include <private/qguiapplication_p.h>
#include <private/qwindow_p.h>

QT_BEGIN_NAMESPACE

class QBackingStorePrivate
{
public:
    QBackingStorePrivate(QWindow *w)
        : window(w)
    {
    }

    QWindow *window;
    QPlatformBackingStore *platformBackingStore;
    QRegion staticContents;
    QSize size;
};

/*!
    \class QBackingStore
    \since 5.0
    \inmodule QtGui

    \brief The QBackingStore class provides a drawing area for QWindow.

    QBackingStore enables the use of QPainter to paint on a QWindow with type
    RasterSurface. The other way of rendering to a QWindow is through the use
    of OpenGL with QOpenGLContext.

    A QBackingStore contains a buffered representation of the window contents,
    and thus supports partial updates by using QPainter to only update a sub
    region of the window contents.

    QBackingStore might be used by an application that wants to use QPainter
    without OpenGL acceleration and without the extra overhead of using the
    QWidget or QGraphicsView UI stacks. For an example of how to use
    QBackingStore see the \l{Raster Window Example}.
*/

/*!
    Flushes the given \a region from the specified window \a win onto the
    screen.

    Note that the \a offset parameter is currently unused.
*/
void QBackingStore::flush(const QRegion &region, QWindow *win, const QPoint &offset)
{
    if (!win)
        win = window();
    if (!win->handle()) {
        qWarning() << "QBackingStore::flush() called for "
            << win << " which does not have a handle.";
        return;
    }

#ifdef QBACKINGSTORE_DEBUG
    if (win && win->isTopLevel() && !qt_window_private(win)->receivedExpose) {
        qWarning().nospace() << "QBackingStore::flush() called with non-exposed window "
            << win << ", behavior is undefined";
    }
#endif

    d_ptr->platformBackingStore->flush(win, region, offset);
}

/*!
    \fn QPaintDevice* QBackingStore::paintDevice()

    Implement this function to return the appropriate paint device.
*/
QPaintDevice *QBackingStore::paintDevice()
{
    return d_ptr->platformBackingStore->paintDevice();
}

/*!
    Constructs an empty surface for the given top-level \a window.
*/
QBackingStore::QBackingStore(QWindow *window)
    : d_ptr(new QBackingStorePrivate(window))
{
    d_ptr->platformBackingStore = QGuiApplicationPrivate::platformIntegration()->createPlatformBackingStore(window);
}

/*!
    Destroys this surface.
*/
QBackingStore::~QBackingStore()
{
    delete d_ptr->platformBackingStore;
}

/*!
    Returns a pointer to the top-level window associated with this
    surface.
*/
QWindow* QBackingStore::window() const
{
    return d_ptr->window;
}

/*!
    This function is called before painting onto the surface begins,
    with the \a region in which the painting will occur.

    \sa endPaint(), paintDevice()
*/

void QBackingStore::beginPaint(const QRegion &region)
{
    d_ptr->platformBackingStore->beginPaint(region);
}

void QBackingStore::beforeBeginPaint(QWindow *win)
{
	if (!win)
		win = window();
	if (!win->handle())
		return;
	d_ptr->platformBackingStore->beforeBeginPaint(win);
}

/*!
    This function is called after painting onto the surface has ended.

    \sa beginPaint(), paintDevice()
*/
void QBackingStore::endPaint()
{
    d_ptr->platformBackingStore->endPaint();
}

void QBackingStore::afterEndPaint(QWindow *win)
{
	if (!win)
		win = window();
	if (!win->handle())
		return;
	d_ptr->platformBackingStore->afterEndPaint(win);
}

/*!
      Sets the size of the windowsurface to be \a size.

      \sa size()
*/
void QBackingStore::resize(const QSize &size)
{
    d_ptr->size = size;
    d_ptr->platformBackingStore->resize(size, d_ptr->staticContents);
}

/*!
    Returns the current size of the windowsurface.
*/
QSize QBackingStore::size() const
{
    return d_ptr->size;
}

/*!
    Scrolls the given \a area \a dx pixels to the right and \a dy
    downward; both \a dx and \a dy may be negative.

    Returns \c true if the area was scrolled successfully; false otherwise.
*/
bool QBackingStore::scroll(const QRegion &area, int dx, int dy)
{
    Q_UNUSED(area);
    Q_UNUSED(dx);
    Q_UNUSED(dy);

    return d_ptr->platformBackingStore->scroll(area, dx, dy);
}

void QBackingStore::setStaticContents(const QRegion &region)
{
    d_ptr->staticContents = region;
}

QRegion QBackingStore::staticContents() const
{
    return d_ptr->staticContents;
}

bool QBackingStore::hasStaticContents() const
{
    return !d_ptr->staticContents.isEmpty();
}

void Q_GUI_EXPORT qt_scrollRectInImage(QImage &img, const QRect &rect, const QPoint &offset)
{
    // make sure we don't detach
    uchar *mem = const_cast<uchar*>(const_cast<const QImage &>(img).bits());

    int lineskip = img.bytesPerLine();
    int depth = img.depth() >> 3;

    const QRect imageRect(0, 0, img.width(), img.height());
    const QRect r = rect & imageRect & imageRect.translated(-offset);
    const QPoint p = rect.topLeft() + offset;

    if (r.isEmpty())
        return;

    const uchar *src;
    uchar *dest;

    if (r.top() < p.y()) {
        src = mem + r.bottom() * lineskip + r.left() * depth;
        dest = mem + (p.y() + r.height() - 1) * lineskip + p.x() * depth;
        lineskip = -lineskip;
    } else {
        src = mem + r.top() * lineskip + r.left() * depth;
        dest = mem + p.y() * lineskip + p.x() * depth;
    }

    const int w = r.width();
    int h = r.height();
    const int bytes = w * depth;

    // overlapping segments?
    if (offset.y() == 0 && qAbs(offset.x()) < w) {
        do {
            ::memmove(dest, src, bytes);
            dest += lineskip;
            src += lineskip;
        } while (--h);
    } else {
        do {
            ::memcpy(dest, src, bytes);
            dest += lineskip;
            src += lineskip;
        } while (--h);
    }
}

QPlatformBackingStore *QBackingStore::handle() const
{
    return d_ptr->platformBackingStore;
}

QT_END_NAMESPACE
