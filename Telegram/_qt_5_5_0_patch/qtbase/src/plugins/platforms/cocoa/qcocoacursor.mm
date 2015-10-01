/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
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

#include "qcocoacursor.h"
#include "qcocoawindow.h"
#include "qcocoahelpers.h"
#include "qcocoaautoreleasepool.h"

#include <QtGui/QBitmap>

QT_BEGIN_NAMESPACE

QCocoaCursor::QCocoaCursor()
{
}

QCocoaCursor::~QCocoaCursor()
{
    // release cursors
    QHash<Qt::CursorShape, NSCursor *>::const_iterator i = m_cursors.constBegin();
    while (i != m_cursors.constEnd()) {
        [*i release];
        ++i;
    }
}

void QCocoaCursor::changeCursor(QCursor *cursor, QWindow *window)
{
    NSCursor * cocoaCursor = convertCursor(cursor);

    if (QPlatformWindow * platformWindow = window->handle())
        static_cast<QCocoaWindow *>(platformWindow)->setWindowCursor(cocoaCursor);
}

QPoint QCocoaCursor::pos() const
{
    return qt_mac_flipPoint([NSEvent mouseLocation]).toPoint();
}

void QCocoaCursor::setPos(const QPoint &position)
{
    CGPoint pos;
    pos.x = position.x();
    pos.y = position.y();

    CGEventRef e = CGEventCreateMouseEvent(0, kCGEventMouseMoved, pos, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

NSCursor *QCocoaCursor::convertCursor(QCursor * cursor)
{
    const Qt::CursorShape newShape = cursor ? cursor->shape() : Qt::ArrowCursor;
    NSCursor *cocoaCursor;

    // Check for a suitable built-in NSCursor first:
    switch (newShape) {
    case Qt::ArrowCursor:
        cocoaCursor= [NSCursor arrowCursor];
        break;
    case Qt::CrossCursor:
        cocoaCursor = [NSCursor crosshairCursor];
        break;
    case Qt::IBeamCursor:
        cocoaCursor = [NSCursor IBeamCursor];
        break;
    case Qt::WhatsThisCursor: //for now just use the pointing hand
    case Qt::PointingHandCursor:
        cocoaCursor = [NSCursor pointingHandCursor];
        break;
    case Qt::SplitVCursor:
        cocoaCursor = [NSCursor resizeUpDownCursor];
        break;
    case Qt::SplitHCursor:
        cocoaCursor = [NSCursor resizeLeftRightCursor];
        break;
    case Qt::OpenHandCursor:
        cocoaCursor = [NSCursor openHandCursor];
        break;
    case Qt::ClosedHandCursor:
        cocoaCursor = [NSCursor closedHandCursor];
        break;
    case Qt::DragMoveCursor:
        cocoaCursor = [NSCursor crosshairCursor];
        break;
    case Qt::DragCopyCursor:
        cocoaCursor = [NSCursor crosshairCursor];
        break;
    case Qt::DragLinkCursor:
        cocoaCursor = [NSCursor dragLinkCursor];
        break;
    default : {
        // No suitable OS cursor exist, use cursors provided
        // by Qt for the rest. Check for a cached cursor:
        cocoaCursor = m_cursors.value(newShape);
        if (cocoaCursor && cursor->shape() == Qt::BitmapCursor) {
            [cocoaCursor release];
            cocoaCursor = 0;
        }
        if (cocoaCursor == 0) {
            cocoaCursor = createCursorData(cursor);
            if (cocoaCursor == 0)
                return [NSCursor arrowCursor];

            m_cursors.insert(newShape, cocoaCursor);
        }

        break; }
    }
    return cocoaCursor;
}


// Creates an NSCursor for the given QCursor.
NSCursor *QCocoaCursor::createCursorData(QCursor *cursor)
{
    /* Note to self... ***
     * mask x data
     * 0xFF x 0x00 == fully opaque white
     * 0x00 x 0xFF == xor'd black
     * 0xFF x 0xFF == fully opaque black
     * 0x00 x 0x00 == fully transparent
     */
#define QT_USE_APPROXIMATE_CURSORS
#ifdef QT_USE_APPROXIMATE_CURSORS
    static const uchar cur_ver_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x03, 0xc0, 0x07, 0xe0, 0x0f, 0xf0,
        0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x0f, 0xf0,
        0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00, 0x00 };
    static const uchar mcur_ver_bits[] = {
        0x00, 0x00, 0x03, 0x80, 0x07, 0xc0, 0x0f, 0xe0, 0x1f, 0xf0, 0x3f, 0xf8,
        0x7f, 0xfc, 0x07, 0xc0, 0x07, 0xc0, 0x07, 0xc0, 0x7f, 0xfc, 0x3f, 0xf8,
        0x1f, 0xf0, 0x0f, 0xe0, 0x07, 0xc0, 0x03, 0x80 };

    static const uchar cur_hor_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x20, 0x18, 0x30,
        0x38, 0x38, 0x7f, 0xfc, 0x7f, 0xfc, 0x38, 0x38, 0x18, 0x30, 0x08, 0x20,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uchar mcur_hor_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x04, 0x40, 0x0c, 0x60, 0x1c, 0x70, 0x3c, 0x78,
        0x7f, 0xfc, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0x7f, 0xfc, 0x3c, 0x78,
        0x1c, 0x70, 0x0c, 0x60, 0x04, 0x40, 0x00, 0x00 };

    static const uchar cur_fdiag_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xf8, 0x00, 0xf8, 0x00, 0x78,
        0x00, 0xf8, 0x01, 0xd8, 0x23, 0x88, 0x37, 0x00, 0x3e, 0x00, 0x3c, 0x00,
        0x3e, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uchar mcur_fdiag_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x07, 0xfc, 0x03, 0xfc, 0x01, 0xfc, 0x00, 0xfc,
        0x41, 0xfc, 0x63, 0xfc, 0x77, 0xdc, 0x7f, 0x8c, 0x7f, 0x04, 0x7e, 0x00,
        0x7f, 0x00, 0x7f, 0x80, 0x7f, 0xc0, 0x00, 0x00 };

    static const uchar cur_bdiag_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x3e, 0x00, 0x3c, 0x00, 0x3e, 0x00,
        0x37, 0x00, 0x23, 0x88, 0x01, 0xd8, 0x00, 0xf8, 0x00, 0x78, 0x00, 0xf8,
        0x01, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uchar mcur_bdiag_bits[] = {
        0x00, 0x00, 0x7f, 0xc0, 0x7f, 0x80, 0x7f, 0x00, 0x7e, 0x00, 0x7f, 0x04,
        0x7f, 0x8c, 0x77, 0xdc, 0x63, 0xfc, 0x41, 0xfc, 0x00, 0xfc, 0x01, 0xfc,
        0x03, 0xfc, 0x07, 0xfc, 0x00, 0x00, 0x00, 0x00 };

    static const unsigned char cur_up_arrow_bits[] = {
        0x00, 0x80, 0x01, 0x40, 0x01, 0x40, 0x02, 0x20, 0x02, 0x20, 0x04, 0x10,
        0x04, 0x10, 0x08, 0x08, 0x0f, 0x78, 0x01, 0x40, 0x01, 0x40, 0x01, 0x40,
        0x01, 0x40, 0x01, 0x40, 0x01, 0x40, 0x01, 0xc0 };
    static const unsigned char mcur_up_arrow_bits[] = {
        0x00, 0x80, 0x01, 0xc0, 0x01, 0xc0, 0x03, 0xe0, 0x03, 0xe0, 0x07, 0xf0,
        0x07, 0xf0, 0x0f, 0xf8, 0x0f, 0xf8, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0,
        0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0 };
#endif
    const uchar *cursorData = 0;
    const uchar *cursorMaskData = 0;
    QPoint hotspot = cursor->hotSpot();

    switch (cursor->shape()) {
    case Qt::BitmapCursor: {
        if (cursor->pixmap().isNull())
            return createCursorFromBitmap(cursor->bitmap(), cursor->mask(), hotspot);
        else
            return createCursorFromPixmap(cursor->pixmap(), hotspot);
        break; }
    case Qt::BlankCursor: {
        QPixmap pixmap = QPixmap(16, 16);
        pixmap.fill(Qt::transparent);
        return createCursorFromPixmap(pixmap);
        break; }
    case Qt::WaitCursor: {
        QPixmap pixmap = QPixmap(QLatin1String(":/qt-project.org/mac/cursors/images/spincursor.png"));
        return createCursorFromPixmap(pixmap, hotspot);
        break; }
    case Qt::SizeAllCursor: {
        QPixmap pixmap = QPixmap(QLatin1String(":/qt-project.org/mac/cursors/images/sizeallcursor.png"));
        return createCursorFromPixmap(pixmap, QPoint(8, 8));
        break; }
    case Qt::BusyCursor: {
        QPixmap pixmap = QPixmap(QLatin1String(":/qt-project.org/mac/cursors/images/waitcursor.png"));
        return createCursorFromPixmap(pixmap, hotspot);
        break; }
    case Qt::ForbiddenCursor: {
        QPixmap pixmap = QPixmap(QLatin1String(":/qt-project.org/mac/cursors/images/forbiddencursor.png"));
        return createCursorFromPixmap(pixmap, hotspot);
        break; }
#define QT_USE_APPROXIMATE_CURSORS
#ifdef QT_USE_APPROXIMATE_CURSORS
    case Qt::SizeVerCursor:
        cursorData = cur_ver_bits;
        cursorMaskData = mcur_ver_bits;
        hotspot = QPoint(8, 8);
        break;
    case Qt::SizeHorCursor:
        cursorData = cur_hor_bits;
        cursorMaskData = mcur_hor_bits;
        hotspot = QPoint(8, 8);
        break;
    case Qt::SizeBDiagCursor:
        cursorData = cur_fdiag_bits;
        cursorMaskData = mcur_fdiag_bits;
        hotspot = QPoint(8, 8);
        break;
    case Qt::SizeFDiagCursor:
        cursorData = cur_bdiag_bits;
        cursorMaskData = mcur_bdiag_bits;
        hotspot = QPoint(8, 8);
        break;
    case Qt::UpArrowCursor:
        cursorData = cur_up_arrow_bits;
        cursorMaskData = mcur_up_arrow_bits;
        hotspot = QPoint(8, 0);
        break;
#endif
    default:
        qWarning("Qt: QCursor::update: Invalid cursor shape %d", cursor->shape());
        return 0;
    }

    // Create an NSCursor from image data if this a self-provided cursor.
    if (cursorData) {
        QBitmap bitmap(QBitmap::fromData(QSize(16, 16), cursorData, QImage::Format_Mono));
        QBitmap mask(QBitmap::fromData(QSize(16, 16), cursorMaskData, QImage::Format_Mono));
        return (createCursorFromBitmap(&bitmap, &mask, hotspot));
    }

    return 0; // should not happen, all cases covered above
}

NSCursor *QCocoaCursor::createCursorFromBitmap(const QBitmap *bitmap, const QBitmap *mask, const QPoint hotspot)
{
    QImage finalCursor(bitmap->size(), QImage::Format_ARGB32);
    QImage bmi = bitmap->toImage().convertToFormat(QImage::Format_RGB32);
    QImage bmmi = mask->toImage().convertToFormat(QImage::Format_RGB32);
    for (int row = 0; row < finalCursor.height(); ++row) {
        QRgb *bmData = reinterpret_cast<QRgb *>(bmi.scanLine(row));
        QRgb *bmmData = reinterpret_cast<QRgb *>(bmmi.scanLine(row));
        QRgb *finalData = reinterpret_cast<QRgb *>(finalCursor.scanLine(row));
        for (int col = 0; col < finalCursor.width(); ++col) {
            if (bmmData[col] == 0xff000000 && bmData[col] == 0xffffffff) {
                finalData[col] = 0xffffffff;
            } else if (bmmData[col] == 0xff000000 && bmData[col] == 0xffffffff) {
                finalData[col] = 0x7f000000;
            } else if (bmmData[col] == 0xffffffff && bmData[col] == 0xffffffff) {
                finalData[col] = 0x00000000;
            } else {
                finalData[col] = 0xff000000;
            }
        }
    }

    return createCursorFromPixmap(QPixmap::fromImage(finalCursor), hotspot);
}

NSCursor *QCocoaCursor::createCursorFromPixmap(const QPixmap pixmap, const QPoint hotspot)
{
    NSPoint hotSpot = NSMakePoint(hotspot.x(), hotspot.y());
    NSImage *nsimage;
    if (pixmap.devicePixelRatio() > 1.0) {
        QSize layoutSize = pixmap.size() / pixmap.devicePixelRatio();
        QPixmap scaledPixmap = pixmap.scaled(layoutSize);
        nsimage = static_cast<NSImage *>(qt_mac_create_nsimage(scaledPixmap));
        CGImageRef cgImage = qt_mac_toCGImage(pixmap.toImage());
        NSBitmapImageRep *imageRep = [[NSBitmapImageRep alloc] initWithCGImage:cgImage];
        [nsimage addRepresentation:imageRep];
        [imageRep release];
        CGImageRelease(cgImage);
    } else {
        nsimage = static_cast<NSImage *>(qt_mac_create_nsimage(pixmap));
    }

    NSCursor *nsCursor = [[NSCursor alloc] initWithImage:nsimage hotSpot: hotSpot];
    [nsimage release];
    return nsCursor;
}

QT_END_NAMESPACE
