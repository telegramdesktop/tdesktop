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

#include "qcocoabackingstore.h"
#include <QtGui/QPainter>
#include "qcocoahelpers.h"

QT_BEGIN_NAMESPACE

QCocoaBackingStore::QCocoaBackingStore(QWindow *window)
    : QPlatformBackingStore(window), m_imageWasEqual(false)
{
}

QCocoaBackingStore::~QCocoaBackingStore()
{
}

QPaintDevice *QCocoaBackingStore::paintDevice()
{
    QCocoaWindow *cocoaWindow = static_cast<QCocoaWindow *>(window()->handle());
    int windowDevicePixelRatio = int(cocoaWindow->devicePixelRatio());

    // Receate the backing store buffer if the effective buffer size has changed,
    // either due to a window resize or devicePixelRatio change.
    QSize effectiveBufferSize = m_requestedSize * windowDevicePixelRatio;
    if (m_qImage.size() != effectiveBufferSize) {
        QImage::Format format = (window()->format().hasAlpha() || cocoaWindow->m_drawContentBorderGradient)
                ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
        m_qImage = QImage(effectiveBufferSize, format);
        m_qImage.setDevicePixelRatio(windowDevicePixelRatio);
        if (format == QImage::Format_ARGB32_Premultiplied)
            m_qImage.fill(Qt::transparent);
    }
    return &m_qImage;
}

void QCocoaBackingStore::flush(QWindow *win, const QRegion &region, const QPoint &offset)
{
    if (!m_qImage.isNull()) {
        if (QCocoaWindow *cocoaWindow = static_cast<QCocoaWindow *>(win->handle()))
            [cocoaWindow->m_qtView flushBackingStore:this region:region offset:offset];
    }
}

QImage QCocoaBackingStore::toImage() const
{
    return m_qImage;
}

void QCocoaBackingStore::resize(const QSize &size, const QRegion &)
{
    m_requestedSize = size;
}

bool QCocoaBackingStore::scroll(const QRegion &area, int dx, int dy)
{
    extern void qt_scrollRectInImage(QImage &img, const QRect &rect, const QPoint &offset);
    const qreal devicePixelRatio = m_qImage.devicePixelRatio();
    QPoint qpoint(dx * devicePixelRatio, dy * devicePixelRatio);
    const QVector<QRect> qrects = area.rects();
    for (int i = 0; i < qrects.count(); ++i) {
        const QRect &qrect = QRect(qrects.at(i).topLeft() * devicePixelRatio, qrects.at(i).size() * devicePixelRatio);
        qt_scrollRectInImage(m_qImage, qrect, qpoint);
    }
    return true;
}

void QCocoaBackingStore::beginPaint(const QRegion &region)
{
    if (m_qImage.hasAlphaChannel()) {
        QPainter p(&m_qImage);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        const QVector<QRect> rects = region.rects();
        const QColor blank = Qt::transparent;
        for (QVector<QRect>::const_iterator it = rects.begin(), end = rects.end(); it != end; ++it)
            p.fillRect(*it, blank);
    }
}

void QCocoaBackingStore::beforeBeginPaint(QWindow *win) {
	m_imageWasEqual = false;
	if (!m_qImage.isNull()) {
		if (QCocoaWindow *cocoaWindow = static_cast<QCocoaWindow *>(win->handle())) {
			if ([cocoaWindow->m_qtView beforeBeginPaint:this])
				m_imageWasEqual = true;
		}
	}
}

void QCocoaBackingStore::afterEndPaint(QWindow *win) {
	if (!m_qImage.isNull()) {
		if (QCocoaWindow *cocoaWindow = static_cast<QCocoaWindow *>(win->handle())) {
			if (m_imageWasEqual)
				[cocoaWindow->m_qtView afterEndPaint:this];
		}
	}
	m_imageWasEqual = false;
}

qreal QCocoaBackingStore::getBackingStoreDevicePixelRatio()
{
    return m_qImage.devicePixelRatio();
}

QT_END_NAMESPACE
