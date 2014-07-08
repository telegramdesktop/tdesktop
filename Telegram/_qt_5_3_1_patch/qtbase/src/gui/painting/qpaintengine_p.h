/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QPAINTENGINE_P_H
#define QPAINTENGINE_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists for the convenience
// of other Qt classes.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include "QtGui/qpainter.h"
#include "QtGui/qpaintengine.h"
#include "QtGui/qregion.h"
#include "private/qobject_p.h"

QT_BEGIN_NAMESPACE

class QPaintDevice;

class Q_GUI_EXPORT QPaintEnginePrivate
{
    Q_DECLARE_PUBLIC(QPaintEngine)
public:
    QPaintEnginePrivate() : pdev(0), q_ptr(0), currentClipDevice(0), hasSystemTransform(0),
                            hasSystemViewport(0) {}
    virtual ~QPaintEnginePrivate() { }
    QPaintDevice *pdev;
    QPaintEngine *q_ptr;
    QRegion systemClip;
    QRect systemRect;
    QRegion systemViewport;
    QTransform systemTransform;
    QPaintDevice *currentClipDevice;
    uint hasSystemTransform : 1;
    uint hasSystemViewport : 1;

    inline void transformSystemClip()
    {
        if (systemClip.isEmpty())
            return;

        if (hasSystemTransform) {
            if (systemTransform.type() <= QTransform::TxTranslate)
                systemClip.translate(qRound(systemTransform.dx()), qRound(systemTransform.dy()));
			else {
				// Transform the system clip region back from device pixels to device-independent pixels before
				// applying systemTransform, which already has transform from device-independent pixels to device pixels
#ifdef Q_OS_MAC
				QTransform scaleTransform;
				const qreal invDevicePixelRatio = 1. / pdev->devicePixelRatio();
				scaleTransform.scale(invDevicePixelRatio, invDevicePixelRatio);
				systemClip = systemTransform.map(scaleTransform.map(systemClip));
#else
				systemClip = systemTransform.map(systemClip);
#endif
			}
        }

        // Make sure we're inside the viewport.
        if (hasSystemViewport) {
            systemClip &= systemViewport;
            if (systemClip.isEmpty()) {
                // We don't want to paint without system clip, so set it to 1 pixel :)
                systemClip = QRect(systemViewport.boundingRect().topLeft(), QSize(1, 1));
            }
        }
    }

    inline void setSystemTransform(const QTransform &xform)
    {
        systemTransform = xform;
        if ((hasSystemTransform = !xform.isIdentity()) || hasSystemViewport)
            transformSystemClip();
        systemStateChanged();
    }

    inline void setSystemViewport(const QRegion &region)
    {
        systemViewport = region;
        hasSystemViewport = !systemViewport.isEmpty();
    }

    virtual void systemStateChanged() { }

    void drawBoxTextItem(const QPointF &p, const QTextItemInt &ti);
};

QT_END_NAMESPACE

#endif // QPAINTENGINE_P_H
