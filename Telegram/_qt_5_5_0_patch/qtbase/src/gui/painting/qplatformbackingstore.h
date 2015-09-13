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

#ifndef QPLATFORMBACKINGSTORE_H
#define QPLATFORMBACKINGSTORE_H

//
//  W A R N I N G
//  -------------
//
// This file is part of the QPA API and is not meant to be used
// in applications. Usage of this API may make your code
// source and binary incompatible with future versions of Qt.
//

#include <QtCore/qrect.h>
#include <QtCore/qobject.h>

#include <QtGui/qwindow.h>
#include <QtGui/qregion.h>
#include <QtGui/qopengl.h>

QT_BEGIN_NAMESPACE


class QRegion;
class QRect;
class QPoint;
class QImage;
class QPlatformBackingStorePrivate;
class QPlatformWindow;
class QPlatformTextureList;
class QPlatformTextureListPrivate;
class QOpenGLContext;
class QPlatformGraphicsBuffer;

#ifndef QT_NO_OPENGL
class Q_GUI_EXPORT QPlatformTextureList : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(QPlatformTextureList)
public:
    enum Flag {
        StacksOnTop = 0x01
    };
    Q_DECLARE_FLAGS(Flags, Flag)

    explicit QPlatformTextureList(QObject *parent = 0);
    ~QPlatformTextureList();

    int count() const;
    bool isEmpty() const { return count() == 0; }
    GLuint textureId(int index) const;
    QRect geometry(int index) const;
    QRect clipRect(int index) const;
    void *source(int index);
    Flags flags(int index) const;
    void lock(bool on);
    bool isLocked() const;

    void appendTexture(void *source, GLuint textureId, const QRect &geometry,
                       const QRect &clipRect = QRect(), Flags flags = 0);
    void clear();

 Q_SIGNALS:
    void locked(bool);
};
Q_DECLARE_OPERATORS_FOR_FLAGS(QPlatformTextureList::Flags)
#endif

class Q_GUI_EXPORT QPlatformBackingStore
{
public:
    explicit QPlatformBackingStore(QWindow *window);
    virtual ~QPlatformBackingStore();

    QWindow *window() const;

    virtual QPaintDevice *paintDevice() = 0;

    // 'window' can be a child window, in which case 'region' is in child window coordinates and
    // offset is the (child) window's offset in relation to the window surface.
    virtual void flush(QWindow *window, const QRegion &region, const QPoint &offset) = 0;
#ifndef QT_NO_OPENGL
    virtual void composeAndFlush(QWindow *window, const QRegion &region, const QPoint &offset,
                                 QPlatformTextureList *textures, QOpenGLContext *context,
                                 bool translucentBackground);
    virtual QImage toImage() const;
    enum TextureFlag {
        TextureSwizzle = 0x01,
        TextureFlip = 0x02
    };
    Q_DECLARE_FLAGS(TextureFlags, TextureFlag)
    virtual GLuint toTexture(const QRegion &dirtyRegion, QSize *textureSize, TextureFlags *flags) const;
#endif

    virtual QPlatformGraphicsBuffer *graphicsBuffer() const;

    virtual void resize(const QSize &size, const QRegion &staticContents) = 0;

    virtual bool scroll(const QRegion &area, int dx, int dy);

    virtual void beginPaint(const QRegion &);
	virtual void beforeBeginPaint(QWindow *window) {
	}
    virtual void endPaint();
	virtual void afterEndPaint(QWindow *window) {
	}

private:
    QPlatformBackingStorePrivate *d_ptr;
};

#ifndef QT_NO_OPENGL
Q_DECLARE_OPERATORS_FOR_FLAGS(QPlatformBackingStore::TextureFlags)
#endif

QT_END_NAMESPACE

#endif // QPLATFORMBACKINGSTORE_H
