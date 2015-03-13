/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia. For licensing terms and
** conditions see http://qt.digia.com/licensing. For further information
** use the contact form at http://qt.digia.com/contact-us.
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
** In addition, as a special exception, Digia gives you certain additional
** rights. These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QWINDOWSWINDOW_H
#define QWINDOWSWINDOW_H

#include "qtwindows_additional.h"
#ifdef Q_OS_WINCE
#  include "qplatformfunctions_wince.h"
#endif
#include "qwindowsscaling.h"
#include "qwindowscursor.h"
#include "qwindowsopenglcontext.h"

#include <qpa/qplatformwindow.h>

QT_BEGIN_NAMESPACE

class QWindowsOleDropTarget;
class QDebug;

struct QWindowsGeometryHint
{
    QWindowsGeometryHint() {}
    explicit QWindowsGeometryHint(const QWindow *w, const QMargins &customMargins);
    static QMargins frame(DWORD style, DWORD exStyle);
    static bool handleCalculateSize(const QMargins &customMargins, const MSG &msg, LRESULT *result);
#ifndef Q_OS_WINCE //MinMax maybe define struct if not available
    void applyToMinMaxInfo(DWORD style, DWORD exStyle, MINMAXINFO *mmi) const;
    void applyToMinMaxInfo(HWND hwnd, MINMAXINFO *mmi) const;
#endif
    bool validSize(const QSize &s) const;

    static inline QPoint mapToGlobal(HWND hwnd, const QPoint &);
    static inline QPoint mapToGlobal(const QWindow *w, const QPoint &);
    static inline QPoint mapFromGlobal(const HWND hwnd, const QPoint &);
    static inline QPoint mapFromGlobal(const QWindow *w, const QPoint &);

    static bool positionIncludesFrame(const QWindow *w);

    QSize minimumSize;
    QSize maximumSize;
    QMargins customMargins;
};

struct QWindowCreationContext
{
    QWindowCreationContext(const QWindow *w, const QRect &r,
                           const QMargins &customMargins,
                           DWORD style, DWORD exStyle);
#ifndef Q_OS_WINCE //MinMax maybe define struct if not available
    void applyToMinMaxInfo(MINMAXINFO *mmi) const
        { geometryHint.applyToMinMaxInfo(style, exStyle, mmi); }
#endif

    QWindowsGeometryHint geometryHint;
    DWORD style;
    DWORD exStyle;
    QRect requestedGeometry;
    QRect obtainedGeometry;
    QMargins margins;
    QMargins customMargins;  // User-defined, additional frame for WM_NCCALCSIZE
    int frameX; // Passed on to CreateWindowEx(), including frame.
    int frameY;
    int frameWidth;
    int frameHeight;
};

struct QWindowsWindowData
{
    QWindowsWindowData() : hwnd(0), embedded(false) {}

    Qt::WindowFlags flags;
    QRect geometry;
    QMargins frame; // Do not use directly for windows, see FrameDirty.
    QMargins customMargins; // User-defined, additional frame for NCCALCSIZE
    HWND hwnd;
    bool embedded;
#ifndef QT_NO_OPENGL
    QSharedPointer<QWindowsStaticOpenGLContext> staticOpenGLContext;
#endif // QT_NO_OPENGL

    static QWindowsWindowData create(const QWindow *w,
                                     const QWindowsWindowData &parameters,
                                     const QString &title);
};

class QWindowsWindow : public QPlatformWindow
{
public:
    enum Flags
    {
        AutoMouseCapture = 0x1, //! Automatic mouse capture on button press.
        WithinSetParent = 0x2,
        FrameDirty = 0x4,            //! Frame outdated by setStyle, recalculate in next query.
        OpenGLSurface = 0x10,
        OpenGL_ES2 = 0x20,
        OpenGLDoubleBuffered = 0x40,
        OpenGlPixelFormatInitialized = 0x80,
        BlockedByModal = 0x100,
        SizeGripOperation = 0x200,
        FrameStrutEventsEnabled = 0x400,
        SynchronousGeometryChangeEvent = 0x800,
        WithinSetStyle = 0x1000,
        WithinDestroy = 0x2000,
        TouchRegistered = 0x4000,
        AlertState = 0x8000,
        Exposed = 0x10000,
        WithinCreate = 0x20000,
        WithinMaximize = 0x40000,
        MaximizeToFullScreen = 0x80000
    };

    QWindowsWindow(QWindow *window, const QWindowsWindowData &data);
    ~QWindowsWindow();

    QSurfaceFormat format() const Q_DECL_OVERRIDE { return m_format; }
    void setGeometryDp(const QRect &rectIn);
    void setGeometry(const QRect &rect) Q_DECL_OVERRIDE
        { setGeometryDp(QWindowsScaling::mapToNative(rect)); }
    QRect geometryDp() const { return m_data.geometry; }
    QRect geometry() const Q_DECL_OVERRIDE
        { return QWindowsScaling::mapFromNative(geometryDp()); }
    QRect normalGeometryDp() const;
    QRect normalGeometry() const Q_DECL_OVERRIDE
        { return QWindowsScaling::mapFromNative(normalGeometryDp()); }
    qreal devicePixelRatio() const Q_DECL_OVERRIDE
        { return qreal(QWindowsScaling::factor()); }
    void setVisible(bool visible) Q_DECL_OVERRIDE;
    bool isVisible() const;
    bool isExposed() const Q_DECL_OVERRIDE { return testFlag(Exposed); }
    bool isActive() const Q_DECL_OVERRIDE;
    bool isEmbedded(const QPlatformWindow *parentWindow) const Q_DECL_OVERRIDE;
    QPoint mapToGlobalDp(const QPoint &pos) const;
    QPoint mapToGlobal(const QPoint &pos) const Q_DECL_OVERRIDE
        { return mapToGlobalDp(pos * QWindowsScaling::factor()) / QWindowsScaling::factor(); }
    QPoint mapFromGlobalDp(const QPoint &pos) const;
    QPoint mapFromGlobal(const QPoint &pos) const Q_DECL_OVERRIDE
        { return mapFromGlobalDp(pos * QWindowsScaling::factor()) / QWindowsScaling::factor(); }
    void setWindowFlags(Qt::WindowFlags flags) Q_DECL_OVERRIDE;
    void setWindowState(Qt::WindowState state) Q_DECL_OVERRIDE;

    HWND handle() const { return m_data.hwnd; }

    WId winId() const Q_DECL_OVERRIDE { return WId(m_data.hwnd); }
    void setParent(const QPlatformWindow *window) Q_DECL_OVERRIDE;

    void setWindowTitle(const QString &title) Q_DECL_OVERRIDE;
    void raise() Q_DECL_OVERRIDE;
    void lower() Q_DECL_OVERRIDE;

    void windowEvent(QEvent *event);

    void propagateSizeHints() Q_DECL_OVERRIDE;
    QMargins frameMarginsDp() const;
    QMargins frameMargins() const Q_DECL_OVERRIDE { return frameMarginsDp() / QWindowsScaling::factor(); }

    void setOpacity(qreal level) Q_DECL_OVERRIDE;
    void setMask(const QRegion &region) Q_DECL_OVERRIDE;
    qreal opacity() const { return m_opacity; }
    void requestActivateWindow() Q_DECL_OVERRIDE;

    bool setKeyboardGrabEnabled(bool grab) Q_DECL_OVERRIDE;
    bool setMouseGrabEnabled(bool grab) Q_DECL_OVERRIDE;
    inline bool hasMouseCapture() const { return GetCapture() == m_data.hwnd; }

    bool startSystemResize(const QPoint &, Qt::Corner corner) Q_DECL_OVERRIDE;

    void setFrameStrutEventsEnabled(bool enabled);
    bool frameStrutEventsEnabled() const { return testFlag(FrameStrutEventsEnabled); }

    QMargins customMargins() const { return m_data.customMargins; }
    void setCustomMargins(const QMargins &m);

    inline unsigned style() const
        { return GetWindowLongPtr(m_data.hwnd, GWL_STYLE); }
    void setStyle(unsigned s) const;
    inline unsigned exStyle() const
        { return GetWindowLongPtr(m_data.hwnd, GWL_EXSTYLE); }
    void setExStyle(unsigned s) const;

    bool handleWmPaint(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void handleMoved();
    void handleResized(int wParam);
    void handleHidden();
    void handleCompositionSettingsChanged();

    static inline HWND handleOf(const QWindow *w);
    static inline QWindowsWindow *baseWindowOf(const QWindow *w);
    static QWindow *topLevelOf(QWindow *w);
    static inline void *userDataOf(HWND hwnd);
    static inline void setUserDataOf(HWND hwnd, void *ud);

    static bool setWindowLayered(HWND hwnd, Qt::WindowFlags flags, bool hasAlpha, qreal opacity);
    bool isLayered() const;

    HDC getDC();
    void releaseDC();
#ifndef Q_OS_WINCE // maybe available on some SDKs revisit WM_GETMINMAXINFO
    void getSizeHints(MINMAXINFO *mmi) const;
    bool handleNonClientHitTest(const QPoint &globalPos, LRESULT *result) const;
#endif // !Q_OS_WINCE

#ifndef QT_NO_CURSOR
    QWindowsWindowCursor cursor() const { return m_cursor; }
#endif
    void setCursor(const QWindowsWindowCursor &c);
    void applyCursor();

    static QByteArray debugWindowFlags(Qt::WindowFlags wf);

    inline bool testFlag(unsigned f) const  { return (m_flags & f) != 0; }
    inline void setFlag(unsigned f) const   { m_flags |= f; }
    inline void clearFlag(unsigned f) const { m_flags &= ~f; }

    void setEnabled(bool enabled);
    bool isEnabled() const;
    void setWindowIcon(const QIcon &icon);

    void *surface(void *nativeConfig);

#ifndef Q_OS_WINCE
    void setAlertState(bool enabled);
    bool isAlertState() const { return testFlag(AlertState); }
    void alertWindow(int durationMs = 0);
    void stopAlertWindow();
#endif

private:
    inline void show_sys() const;
    inline void hide_sys() const;
    inline void setGeometry_sys(const QRect &rect) const;
    inline QRect frameGeometry_sys() const;
    inline QRect geometry_sys() const;
    inline QWindowsWindowData setWindowFlags_sys(Qt::WindowFlags wt, unsigned flags = 0) const;
    inline bool isFullScreen_sys() const;
    inline void setWindowState_sys(Qt::WindowState newState);
    inline void setParent_sys(const QPlatformWindow *parent);
    inline void updateTransientParent() const;
	inline void clearTransientParent() const;
    void destroyWindow();
    inline bool isDropSiteEnabled() const { return m_dropTarget != 0; }
    void setDropSiteEnabled(bool enabled);
    void updateDropSite();
    void handleGeometryChange();
    void handleWindowStateChange(Qt::WindowState state);
    inline void destroyIcon();
    void fireExpose(const QRegion &region, bool force=false);

    mutable QWindowsWindowData m_data;
    mutable unsigned m_flags;
    HDC m_hdc;
    Qt::WindowState m_windowState;
    qreal m_opacity;
#ifndef QT_NO_CURSOR
    QWindowsWindowCursor m_cursor;
#endif
    QWindowsOleDropTarget *m_dropTarget;
    unsigned m_savedStyle;
    QRect m_savedFrameGeometry;
    const QSurfaceFormat m_format;
#ifdef Q_OS_WINCE
    bool m_previouslyHidden;
#endif
    HICON m_iconSmall;
    HICON m_iconBig;
    void *m_surface;
};

// Debug
QDebug operator<<(QDebug d, const RECT &r);
#ifndef Q_OS_WINCE // maybe available on some SDKs revisit WM_GETMINMAXINFO/WM_NCCALCSIZE
QDebug operator<<(QDebug d, const MINMAXINFO &i);
QDebug operator<<(QDebug d, const NCCALCSIZE_PARAMS &p);
#endif

// ---------- QWindowsGeometryHint inline functions.
QPoint QWindowsGeometryHint::mapToGlobal(HWND hwnd, const QPoint &qp)
{
    POINT p = { qp.x(), qp.y() };
    ClientToScreen(hwnd, &p);
    return QPoint(p.x, p.y);
}

QPoint QWindowsGeometryHint::mapFromGlobal(const HWND hwnd, const QPoint &qp)
{
    POINT p = { qp.x(), qp.y() };
    ScreenToClient(hwnd, &p);
    return QPoint(p.x, p.y);
}

QPoint QWindowsGeometryHint::mapToGlobal(const QWindow *w, const QPoint &p)
    { return QWindowsGeometryHint::mapToGlobal(QWindowsWindow::handleOf(w), p); }

QPoint QWindowsGeometryHint::mapFromGlobal(const QWindow *w, const QPoint &p)
    { return QWindowsGeometryHint::mapFromGlobal(QWindowsWindow::handleOf(w), p); }


// ---------- QWindowsBaseWindow inline functions.

QWindowsWindow *QWindowsWindow::baseWindowOf(const QWindow *w)
{
    if (w)
        if (QPlatformWindow *pw = w->handle())
            return static_cast<QWindowsWindow *>(pw);
    return 0;
}

HWND QWindowsWindow::handleOf(const QWindow *w)
{
    if (const QWindowsWindow *bw = QWindowsWindow::baseWindowOf(w))
        return bw->handle();
    return 0;
}

void *QWindowsWindow::userDataOf(HWND hwnd)
{
    return (void *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

void QWindowsWindow::setUserDataOf(HWND hwnd, void *ud)
{
    SetWindowLongPtr(hwnd, GWLP_USERDATA, LONG_PTR(ud));
}

inline void QWindowsWindow::destroyIcon()
{
    if (m_iconBig) {
        DestroyIcon(m_iconBig);
        m_iconBig = 0;
    }
    if (m_iconSmall) {
        DestroyIcon(m_iconSmall);
        m_iconSmall = 0;
    }
}

inline bool QWindowsWindow::isLayered() const
{
#ifndef Q_OS_WINCE
    return GetWindowLongPtr(m_data.hwnd, GWL_EXSTYLE) & WS_EX_LAYERED;
#else
    return false;
#endif
}

QT_END_NAMESPACE

Q_DECLARE_METATYPE(QMargins)

#endif // QWINDOWSWINDOW_H
