/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the plugins of the Qt Toolkit.
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

#include "qwindowswindow.h"
#include "qwindowsnativeimage.h"
#include "qwindowscontext.h"
#include "qwindowsdrag.h"
#include "qwindowsscreen.h"
#ifdef QT_NO_CURSOR
#  include "qwindowscursor.h"
#endif

#if defined(QT_OPENGL_ES_2) || defined(QT_OPENGL_DYNAMIC)
#  include "qwindowseglcontext.h"
#  include <QtGui/QOpenGLFunctions>
#endif

#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtGui/QRegion>
#include <private/qsystemlibrary_p.h>
#include <private/qwindow_p.h>
#include <private/qguiapplication_p.h>
#include <qpa/qwindowsysteminterface.h>

#include <QtCore/QDebug>

QT_BEGIN_NAMESPACE

enum {
    defaultWindowWidth = 160,
    defaultWindowHeight = 160
};

Q_GUI_EXPORT HICON qt_pixmapToWinHICON(const QPixmap &);

static QByteArray debugWinStyle(DWORD style)
{
    QByteArray rc = "0x";
    rc += QByteArray::number(qulonglong(style), 16);
    if (style & WS_POPUP)
        rc += " WS_POPUP";
    if (style & WS_CHILD)
        rc += " WS_CHILD";
    if (style & WS_OVERLAPPED)
        rc += " WS_OVERLAPPED";
    if (style & WS_CLIPSIBLINGS)
        rc += " WS_CLIPSIBLINGS";
    if (style & WS_CLIPCHILDREN)
        rc += " WS_CLIPCHILDREN";
    if (style & WS_THICKFRAME)
        rc += " WS_THICKFRAME";
    if (style & WS_DLGFRAME)
        rc += " WS_DLGFRAME";
    if (style & WS_SYSMENU)
        rc += " WS_SYSMENU";
    if (style & WS_MINIMIZEBOX)
        rc += " WS_MINIMIZEBOX";
    if (style & WS_MAXIMIZEBOX)
        rc += " WS_MAXIMIZEBOX";
    return rc;
}

static QByteArray debugWinExStyle(DWORD exStyle)
{
    QByteArray rc = "0x";
    rc += QByteArray::number(qulonglong(exStyle), 16);
    if (exStyle & WS_EX_TOOLWINDOW)
        rc += " WS_EX_TOOLWINDOW";
    if (exStyle & WS_EX_CONTEXTHELP)
        rc += " WS_EX_CONTEXTHELP";
    if (exStyle & WS_EX_LAYERED)
        rc += " WS_EX_LAYERED";
    return rc;
}

static QByteArray debugWindowStates(Qt::WindowStates s)
{

    QByteArray rc = "0x";
    rc += QByteArray::number(int(s), 16);
    if (s & Qt::WindowMinimized)
        rc += " WindowMinimized";
    if (s & Qt::WindowMaximized)
        rc += " WindowMaximized";
    if (s & Qt::WindowFullScreen)
        rc += " WindowFullScreen";
    if (s & Qt::WindowActive)
        rc += " WindowActive";
    return rc;
}

#ifndef Q_OS_WINCE // maybe available on some SDKs revisit WM_GETMINMAXINFO
QDebug operator<<(QDebug d, const MINMAXINFO &i)
{
    d.nospace() << "MINMAXINFO maxSize=" << i.ptMaxSize.x << ','
                << i.ptMaxSize.y << " maxpos=" << i.ptMaxPosition.x
                 << ',' << i.ptMaxPosition.y << " mintrack="
                 << i.ptMinTrackSize.x << ',' << i.ptMinTrackSize.y
                 << " maxtrack=" << i.ptMaxTrackSize.x << ','
                 << i.ptMaxTrackSize.y;
    return d;
}
#endif // !Q_OS_WINCE

static inline QSize qSizeOfRect(const RECT &rect)
{
    return QSize(rect.right -rect.left, rect.bottom - rect.top);
}

static inline QRect qrectFromRECT(const RECT &rect)
{
    return QRect(QPoint(rect.left, rect.top), qSizeOfRect(rect));
}

static inline RECT RECTfromQRect(const QRect &rect)
{
    const int x = rect.left();
    const int y = rect.top();
    RECT result = { x, y, x + rect.width(), y + rect.height() };
    return result;
}

QDebug operator<<(QDebug d, const RECT &r)
{
    d.nospace() << "RECT: left/top=" << r.left << ',' << r.top
                << " right/bottom=" << r.right << ',' << r.bottom;
    return d;
}

#ifndef Q_OS_WINCE // maybe available on some SDKs revisit WM_NCCALCSIZE
QDebug operator<<(QDebug d, const NCCALCSIZE_PARAMS &p)
{
    qDebug().nospace() << "NCCALCSIZE_PARAMS "
        << qrectFromRECT(p.rgrc[0])
        << ' ' << qrectFromRECT(p.rgrc[1]) << ' '
        << qrectFromRECT(p.rgrc[2]);
    return d;
}
#endif // !Q_OS_WINCE

// Return the frame geometry relative to the parent
// if there is one.
static inline QRect frameGeometry(HWND hwnd, bool topLevel)
{
    RECT rect = { 0, 0, 0, 0 };
    GetWindowRect(hwnd, &rect); // Screen coordinates.
    const HWND parent = GetParent(hwnd);
    if (parent && !topLevel) {
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        POINT leftTop = { rect.left, rect.top };
        ScreenToClient(parent, &leftTop);
        rect.left = leftTop.x;
        rect.top = leftTop.y;
        rect.right = leftTop.x + width;
        rect.bottom = leftTop.y + height;
    }
    return qrectFromRECT(rect);
}

static inline QSize clientSize(HWND hwnd)
{
    RECT rect = { 0, 0, 0, 0 };
    GetClientRect(hwnd, &rect); // Always returns point 0,0, thus unusable for geometry.
    return qSizeOfRect(rect);
}

static bool applyBlurBehindWindow(HWND hwnd)
{
#ifdef Q_OS_WINCE
    Q_UNUSED(hwnd);
    return false;
#else
    enum { dwmBbEnable = 0x1, dwmBbBlurRegion = 0x2 };

    struct DwmBlurBehind {
        DWORD dwFlags;
        BOOL  fEnable;
        HRGN  hRgnBlur;
        BOOL  fTransitionOnMaximized;
    };

    typedef HRESULT (WINAPI *PtrDwmEnableBlurBehindWindow)(HWND, const DwmBlurBehind*);
    typedef HRESULT (WINAPI *PtrDwmIsCompositionEnabled)(BOOL *);

    // DWM API is available only from Windows Vista
    if (QSysInfo::windowsVersion() < QSysInfo::WV_VISTA)
        return false;

    static bool functionPointersResolved = false;
    static PtrDwmEnableBlurBehindWindow dwmBlurBehind = 0;
    static PtrDwmIsCompositionEnabled dwmIsCompositionEnabled = 0;

    if (Q_UNLIKELY(!functionPointersResolved)) {
        QSystemLibrary library(QStringLiteral("dwmapi"));
        if (library.load()) {
            dwmBlurBehind = (PtrDwmEnableBlurBehindWindow)(library.resolve("DwmEnableBlurBehindWindow"));
            dwmIsCompositionEnabled = (PtrDwmIsCompositionEnabled)(library.resolve("DwmIsCompositionEnabled"));
        }

        functionPointersResolved = true;
    }

    if (Q_UNLIKELY(!dwmBlurBehind || !dwmIsCompositionEnabled))
        return false;

    BOOL compositionEnabled;
    if (dwmIsCompositionEnabled(&compositionEnabled) != S_OK)
        return false;

    DwmBlurBehind blurBehind = {0, 0, 0, 0};

    if (compositionEnabled) {
        blurBehind.dwFlags = dwmBbEnable | dwmBbBlurRegion;
        blurBehind.fEnable = TRUE;
        blurBehind.hRgnBlur = CreateRectRgn(0, 0, -1, -1);
    } else {
        blurBehind.dwFlags = dwmBbEnable;
        blurBehind.fEnable = FALSE;
    }

    const bool result = dwmBlurBehind(hwnd, &blurBehind) == S_OK;

    if (blurBehind.hRgnBlur)
        DeleteObject(blurBehind.hRgnBlur);

    return result;
#endif // Q_OS_WINCE
}

// from qwidget_win.cpp, pass flags separately in case they have been "autofixed".
static bool shouldShowMaximizeButton(const QWindow *w, Qt::WindowFlags flags)
{
    if ((flags & Qt::MSWindowsFixedSizeDialogHint) || !(flags & Qt::WindowMaximizeButtonHint))
        return false;
    // if the user explicitly asked for the maximize button, we try to add
    // it even if the window has fixed size.
    return (flags & Qt::CustomizeWindowHint) ||
        w->maximumSize() == QSize(QWINDOWSIZE_MAX, QWINDOWSIZE_MAX);
}

// Set the WS_EX_LAYERED flag on a HWND if required. This is required for
// translucent backgrounds, not fully opaque windows and for
// Qt::WindowTransparentForInput (in combination with WS_EX_TRANSPARENT).
bool QWindowsWindow::setWindowLayered(HWND hwnd, Qt::WindowFlags flags, bool hasAlpha, qreal opacity)
{
#ifndef Q_OS_WINCE // maybe needs revisiting WS_EX_LAYERED
    const LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    const bool needsLayered = (flags & Qt::WindowTransparentForInput)
        || (hasAlpha && (flags & Qt::FramelessWindowHint)) || opacity < 1.0;
    const bool isLayered = (exStyle & WS_EX_LAYERED);
    if (needsLayered != isLayered) {
        if (needsLayered) {
            SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        } else {
            SetWindowLong(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
        }
    }
    return needsLayered;
#else // !Q_OS_WINCE
    Q_UNUSED(hwnd);
    Q_UNUSED(flags);
    Q_UNUSED(hasAlpha);
    Q_UNUSED(opacity);
    return false;
#endif // Q_OS_WINCE
}

static void setWindowOpacity(HWND hwnd, Qt::WindowFlags flags, bool hasAlpha, bool openGL, qreal level)
{
#ifdef Q_OS_WINCE // WINCE does not support that feature and microsoft explicitly warns to use those calls
    Q_UNUSED(hwnd);
    Q_UNUSED(flags);
    Q_UNUSED(hasAlpha);
    Q_UNUSED(level);
#else
    if (QWindowsWindow::setWindowLayered(hwnd, flags, hasAlpha, level)) {
        if (hasAlpha && !openGL && (flags & Qt::FramelessWindowHint)) {
            // Non-GL windows with alpha: Use blend function to update.
            BLENDFUNCTION blend = {AC_SRC_OVER, 0, (BYTE)(255.0 * level), AC_SRC_ALPHA};
            QWindowsContext::user32dll.updateLayeredWindow(hwnd, NULL, NULL, NULL, NULL, NULL, 0, &blend, ULW_ALPHA);
        } else {
            QWindowsContext::user32dll.setLayeredWindowAttributes(hwnd, 0, (int)(level * 255), LWA_ALPHA);
        }
    } else if (IsWindowVisible(hwnd)) { // Repaint when switching from layered.
        InvalidateRect(hwnd, NULL, TRUE);
    }
#endif // !Q_OS_WINCE
}

/*!
    \class WindowCreationData
    \brief Window creation code.

    This struct gathers all information required to create a window.
    Window creation is split in 3 steps:

    \list
    \li fromWindow() Gather all required information
    \li create() Create the system handle.
    \li initialize() Post creation initialization steps.
    \endlist

    The reason for this split is to also enable changing the QWindowFlags
    by calling:

    \list
    \li fromWindow() Gather information and determine new system styles
    \li applyWindowFlags() to apply the new window system styles.
    \li initialize() Post creation initialization steps.
    \endlist

    Contains the window creation code formerly in qwidget_win.cpp.

    \sa QWindowCreationContext
    \internal
    \ingroup qt-lighthouse-win
*/

struct WindowCreationData
{
    typedef QWindowsWindowData WindowData;
    enum Flags { ForceChild = 0x1, ForceTopLevel = 0x2 };

    WindowCreationData() : parentHandle(0), type(Qt::Widget), style(0), exStyle(0),
        topLevel(false), popup(false), dialog(false), desktop(false),
        tool(false), embedded(false), hasAlpha(false) {}

    void fromWindow(const QWindow *w, const Qt::WindowFlags flags, unsigned creationFlags = 0);
    inline WindowData create(const QWindow *w, const WindowData &data, QString title) const;
    inline void applyWindowFlags(HWND hwnd) const;
    void initialize(HWND h, bool frameChange, qreal opacityLevel) const;

    Qt::WindowFlags flags;
    HWND parentHandle;
    Qt::WindowType type;
    unsigned style;
    unsigned exStyle;
    bool isGL;
    bool topLevel;
    bool popup;
    bool dialog;
    bool desktop;
    bool tool;
    bool embedded;
    bool hasAlpha;
};

QDebug operator<<(QDebug debug, const WindowCreationData &d)
{
    debug.nospace() << QWindowsWindow::debugWindowFlags(d.flags)
        << " GL=" << d.isGL << " topLevel=" << d.topLevel << " popup="
        << d.popup << " dialog=" << d.dialog << " desktop=" << d.desktop
        << " embedded=" << d.embedded
        << " tool=" << d.tool << " style=" << debugWinStyle(d.style)
        << " exStyle=" << debugWinExStyle(d.exStyle)
        << " parent=" << d.parentHandle;
    return debug;
}

// Fix top level window flags in case only the type flags are passed.
static inline void fixTopLevelWindowFlags(Qt::WindowFlags &flags)
{
    switch (flags) {
    case Qt::Window:
        flags |= Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint
              |Qt::WindowMaximizeButtonHint|Qt::WindowCloseButtonHint;
        break;
    case Qt::Dialog:
        flags |= Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowContextHelpButtonHint | Qt::WindowCloseButtonHint;
        break;
    case Qt::Tool:
         flags |= Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint;
         break;
    default:
        break;
    }
}

void WindowCreationData::fromWindow(const QWindow *w, const Qt::WindowFlags flagsIn,
                                    unsigned creationFlags)
{
    isGL = w->surfaceType() == QWindow::OpenGLSurface;
    hasAlpha = w->format().hasAlpha();
    flags = flagsIn;

    // Sometimes QWindow doesn't have a QWindow parent but does have a native parent window,
    // e.g. in case of embedded ActiveQt servers. They should not be considered a top-level
    // windows in such cases.
    QVariant prop = w->property("_q_embedded_native_parent_handle");
    if (prop.isValid()) {
        embedded = true;
        parentHandle = (HWND)prop.value<WId>();
    }

    if (creationFlags & ForceChild) {
        topLevel = false;
    } else if (embedded) {
        // Embedded native windows (for example Active X server windows) are by
        // definition never toplevel, even though they do not have QWindow parents.
        topLevel = false;
    } else {
        topLevel = (creationFlags & ForceTopLevel) ? true : w->isTopLevel();
    }

    if (topLevel)
        fixTopLevelWindowFlags(flags);

    type = static_cast<Qt::WindowType>(int(flags) & Qt::WindowType_Mask);
    switch (type) {
    case Qt::Dialog:
    case Qt::Sheet:
        dialog = true;
        break;
    case Qt::Drawer:
    case Qt::Tool:
        tool = true;
        break;
    case Qt::Popup:
        popup = true;
        break;
    case Qt::Desktop:
        desktop = true;
        break;
    default:
        break;
    }
    if ((flags & Qt::MSWindowsFixedSizeDialogHint))
        dialog = true;

    // Parent: Use transient parent for top levels.
    if (popup) {
        flags |= Qt::WindowStaysOnTopHint; // a popup stays on top, no parent.
    } else if (!embedded) {
        if (const QWindow *parentWindow = topLevel ? w->transientParent() : w->parent())
            parentHandle = QWindowsWindow::handleOf(parentWindow);
    }

    if (popup || (type == Qt::ToolTip) || (type == Qt::SplashScreen)) {
        style = WS_POPUP;
    } else if (topLevel && !desktop) {
        if (flags & Qt::FramelessWindowHint)
            style = WS_POPUP;                // no border
        else if (flags & Qt::WindowTitleHint)
            style = WS_OVERLAPPED;
        else
            style = 0;
    } else {
        style = WS_CHILD;
    }

    if (!desktop) {
        // if (!testAttribute(Qt::WA_PaintUnclipped))
        // ### Commented out for now as it causes some problems, but
        // this should be correct anyway, so dig some more into this
#ifdef Q_FLATTEN_EXPOSE
        if (isGL)
            style |= WS_CLIPSIBLINGS | WS_CLIPCHILDREN; // see SetPixelFormat
#else
        style |= WS_CLIPSIBLINGS | WS_CLIPCHILDREN ;
#endif
        if (topLevel) {
            if ((type == Qt::Window || dialog || tool)) {
                if (!(flags & Qt::FramelessWindowHint)) {
                    style |= WS_POPUP;
                    if (flags & Qt::MSWindowsFixedSizeDialogHint) {
                        style |= WS_DLGFRAME;
                    } else {
                        style |= WS_THICKFRAME;
                    }
                    if (flags & Qt::WindowTitleHint)
                        style |= WS_CAPTION; // Contains WS_DLGFRAME
                }
                if (flags & Qt::WindowSystemMenuHint)
                    style |= WS_SYSMENU;
                if (flags & Qt::WindowMinimizeButtonHint)
                    style |= WS_MINIMIZEBOX;
                if (shouldShowMaximizeButton(w, flags))
                    style |= WS_MAXIMIZEBOX;
                if (tool)
                    exStyle |= WS_EX_TOOLWINDOW;
                if (flags & Qt::WindowContextHelpButtonHint)
                    exStyle |= WS_EX_CONTEXTHELP;
            } else {
                 exStyle |= WS_EX_TOOLWINDOW;
            }

#ifndef Q_OS_WINCE
            // make mouse events fall through this window
            // NOTE: WS_EX_TRANSPARENT flag can make mouse inputs fall through a layered window
            if (flagsIn & Qt::WindowTransparentForInput)
                exStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
#endif
        }
    }
}

QWindowsWindowData
    WindowCreationData::create(const QWindow *w, const WindowData &data, QString title) const
{
    typedef QSharedPointer<QWindowCreationContext> QWindowCreationContextPtr;

    WindowData result;
    result.flags = flags;

    if (desktop) {                        // desktop widget. No frame, hopefully?
        result.hwnd = GetDesktopWindow();
        result.geometry = frameGeometry(result.hwnd, true);
        result.embedded = false;
        qCDebug(lcQpaWindows) << "Created desktop window " << w << result.hwnd;
        return result;
    }
    if ((flags & Qt::WindowType_Mask) == Qt::ForeignWindow) {
        result.hwnd = reinterpret_cast<HWND>(w->winId());
        Q_ASSERT(result.hwnd);
        const LONG_PTR style = GetWindowLongPtr(result.hwnd, GWL_STYLE);
        const LONG_PTR exStyle = GetWindowLongPtr(result.hwnd, GWL_EXSTYLE);
        result.geometry = frameGeometry(result.hwnd, !GetParent(result.hwnd));
        result.frame = QWindowsGeometryHint::frame(style, exStyle);
        result.embedded = false;
        qCDebug(lcQpaWindows) << "Foreign window: " << w << result.hwnd << result.geometry << result.frame;
        return result;
    }

    const HINSTANCE appinst = (HINSTANCE)GetModuleHandle(0);

    const QString windowClassName = QWindowsContext::instance()->registerWindowClass(w, isGL);

    QRect rect = QPlatformWindow::initialGeometry(w, data.geometry, defaultWindowWidth, defaultWindowHeight);

    if (title.isEmpty() && (result.flags & Qt::WindowTitleHint))
        title = topLevel ? qAppName() : w->objectName();

    const wchar_t *titleUtf16 = reinterpret_cast<const wchar_t *>(title.utf16());
    const wchar_t *classNameUtf16 = reinterpret_cast<const wchar_t *>(windowClassName.utf16());

    // Capture events before CreateWindowEx() returns. The context is cleared in
    // the QWindowsWindow constructor.
    const QWindowCreationContextPtr context(new QWindowCreationContext(w, rect, data.customMargins, style, exStyle));
    QWindowsContext::instance()->setWindowCreationContext(context);

    qCDebug(lcQpaWindows).nospace()
        << "CreateWindowEx: " << w << *this << " class=" <<windowClassName << " title=" << title
        << "\nrequested: " << rect << ": "
        << context->frameWidth << 'x' <<  context->frameHeight
        << '+' << context->frameX << '+' << context->frameY
        << " custom margins: " << context->customMargins;

    result.hwnd = CreateWindowEx(exStyle, classNameUtf16, titleUtf16,
                                 style,
                                 context->frameX, context->frameY,
                                 context->frameWidth, context->frameHeight,
                                 parentHandle, NULL, appinst, NULL);
    qCDebug(lcQpaWindows).nospace()
        << "CreateWindowEx: returns " << w << ' ' << result.hwnd << " obtained geometry: "
        << context->obtainedGeometry << context->margins;

    if (!result.hwnd) {
        qErrnoWarning("%s: CreateWindowEx failed", __FUNCTION__);
        return result;
    }

    result.geometry = context->obtainedGeometry;
    result.frame = context->margins;
    result.embedded = embedded;
    result.customMargins = context->customMargins;

    if (isGL && hasAlpha)
        applyBlurBehindWindow(result.hwnd);

    return result;
}

void WindowCreationData::applyWindowFlags(HWND hwnd) const
{
    // Keep enabled and visible from the current style.
    const LONG_PTR oldStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
    const LONG_PTR oldExStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

    const LONG_PTR newStyle = style | (oldStyle & (WS_DISABLED|WS_VISIBLE));
    if (oldStyle != newStyle)
        SetWindowLongPtr(hwnd, GWL_STYLE, newStyle);
    const LONG_PTR newExStyle = exStyle;
    if (newExStyle != oldExStyle)
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, newExStyle);
    qCDebug(lcQpaWindows).nospace() << __FUNCTION__ << hwnd << *this
        << "\n    Style from " << debugWinStyle(oldStyle) << "\n    to "
        << debugWinStyle(newStyle) << "\n    ExStyle from "
        << debugWinExStyle(oldExStyle) << " to "
        << debugWinExStyle(newExStyle);
}

void WindowCreationData::initialize(HWND hwnd, bool frameChange, qreal opacityLevel) const
{
    if (desktop || !hwnd)
        return;
    UINT swpFlags = SWP_NOMOVE | SWP_NOSIZE;
    if (frameChange)
        swpFlags |= SWP_FRAMECHANGED;
    if (topLevel) {
        swpFlags |= SWP_NOACTIVATE;
        if ((flags & Qt::WindowStaysOnTopHint) || (type == Qt::ToolTip)) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, swpFlags);
            if (flags & Qt::WindowStaysOnBottomHint)
                qWarning() << "QWidget: Incompatible window flags: the window can't be on top and on bottom at the same time";
        } else if (flags & Qt::WindowStaysOnBottomHint) {
            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, swpFlags);
        } else if (frameChange) { // Force WM_NCCALCSIZE with wParam=1 in case of custom margins.
            SetWindowPos(hwnd, 0, 0, 0, 0, 0, swpFlags);
        }
        if (flags & (Qt::CustomizeWindowHint|Qt::WindowTitleHint)) {
            HMENU systemMenu = GetSystemMenu(hwnd, FALSE);
            if (flags & Qt::WindowCloseButtonHint)
                EnableMenuItem(systemMenu, SC_CLOSE, MF_BYCOMMAND|MF_ENABLED);
            else
                EnableMenuItem(systemMenu, SC_CLOSE, MF_BYCOMMAND|MF_GRAYED);
        }

        setWindowOpacity(hwnd, flags, hasAlpha, isGL, opacityLevel);
    } else { // child.
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, swpFlags);
    }
}

/*!
    \class QWindowsGeometryHint
    \brief Stores geometry constraints and provides utility functions.

    Geometry constraints ready to apply to a MINMAXINFO taking frame
    into account.

    \internal
    \ingroup qt-lighthouse-win
*/

#define QWINDOWSIZE_MAX ((1<<24)-1)

QWindowsGeometryHint::QWindowsGeometryHint(const QWindow *w, const QMargins &cm) :
     minimumSize(w->minimumSize()),
     maximumSize(w->maximumSize()),
     customMargins(cm)
{
}

bool QWindowsGeometryHint::validSize(const QSize &s) const
{
    const int width = s.width();
    const int height = s.height();
    return width >= minimumSize.width() && width <= maximumSize.width()
           && height >= minimumSize.height() && height <= maximumSize.height();
}

QMargins QWindowsGeometryHint::frame(DWORD style, DWORD exStyle)
{
    RECT rect = {0,0,0,0};
#ifndef Q_OS_WINCE
    style &= ~(WS_OVERLAPPED); // Not permitted, see docs.
#endif
    if (!AdjustWindowRectEx(&rect, style, FALSE, exStyle))
        qErrnoWarning("%s: AdjustWindowRectEx failed", __FUNCTION__);
    const QMargins result(qAbs(rect.left), qAbs(rect.top),
                          qAbs(rect.right), qAbs(rect.bottom));
    qCDebug(lcQpaWindows).nospace() << __FUNCTION__ << " style= 0x"
        << QString::number(style, 16) << " exStyle=0x" << QString::number(exStyle, 16) << ' ' << rect << ' ' << result;

    return result;
}

bool QWindowsGeometryHint::handleCalculateSize(const QMargins &customMargins, const MSG &msg, LRESULT *result)
{
#ifndef Q_OS_WINCE
    // NCCALCSIZE_PARAMS structure if wParam==TRUE
    if (!msg.wParam || customMargins.isNull())
        return false;
    *result = DefWindowProc(msg.hwnd, msg.message, msg.wParam, msg.lParam);
    NCCALCSIZE_PARAMS *ncp = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg.lParam);
    const RECT oldClientArea = ncp->rgrc[0];
    ncp->rgrc[0].left += customMargins.left();
    ncp->rgrc[0].top += customMargins.top();
    ncp->rgrc[0].right -= customMargins.right();
    ncp->rgrc[0].bottom -= customMargins.bottom();
    result = 0;
    qCDebug(lcQpaWindows).nospace() << __FUNCTION__ << oldClientArea << '+' << customMargins << "-->"
        << ncp->rgrc[0] << ' ' << ncp->rgrc[1] << ' ' << ncp->rgrc[2]
        << ' ' << ncp->lppos->cx << ',' << ncp->lppos->cy;
    return true;
#else
    Q_UNUSED(customMargins)
    Q_UNUSED(msg)
    Q_UNUSED(result)
    return false;
#endif
}

#ifndef Q_OS_WINCE
void QWindowsGeometryHint::applyToMinMaxInfo(HWND hwnd, MINMAXINFO *mmi) const
{
    return applyToMinMaxInfo(GetWindowLong(hwnd, GWL_STYLE),
                             GetWindowLong(hwnd, GWL_EXSTYLE), mmi);
}

void QWindowsGeometryHint::applyToMinMaxInfo(DWORD style, DWORD exStyle, MINMAXINFO *mmi) const
{
    qCDebug(lcQpaWindows).nospace() << '>' << __FUNCTION__ << '<' << " min="
        << minimumSize.width() << ',' << minimumSize.height()
        << " max=" << maximumSize.width() << ',' << maximumSize.height()
        << " in " << *mmi;

    const QMargins margins = QWindowsGeometryHint::frame(style, exStyle);
    const int frameWidth = margins.left() + margins.right() + customMargins.left() + customMargins.right();
    const int frameHeight = margins.top() + margins.bottom() + customMargins.top() + customMargins.bottom();
    if (minimumSize.width() > 0)
        mmi->ptMinTrackSize.x = minimumSize.width() + frameWidth;
    if (minimumSize.height() > 0)
        mmi->ptMinTrackSize.y = minimumSize.height() + frameHeight;

    const int maximumWidth = qMax(maximumSize.width(), minimumSize.width());
    const int maximumHeight = qMax(maximumSize.height(), minimumSize.height());
    if (maximumWidth < QWINDOWSIZE_MAX)
        mmi->ptMaxTrackSize.x = maximumWidth + frameWidth;
    // windows with title bar have an implicit size limit of 112 pixels
    if (maximumHeight < QWINDOWSIZE_MAX)
        mmi->ptMaxTrackSize.y = qMax(maximumHeight + frameHeight, 112);
    qCDebug(lcQpaWindows).nospace() << '<' << __FUNCTION__
        << " frame=" << margins << ' ' << frameWidth << ',' << frameHeight
        << " out " << *mmi;
}
#endif // !Q_OS_WINCE

bool QWindowsGeometryHint::positionIncludesFrame(const QWindow *w)
{
    return qt_window_private(const_cast<QWindow *>(w))->positionPolicy
           == QWindowPrivate::WindowFrameInclusive;
}

/*!
    \class QWindowCreationContext
    \brief Active Context for creating windows.

    There is a phase in window creation (WindowCreationData::create())
    in which events are sent before the system API CreateWindowEx() returns
    the handle. These cannot be handled by the platform window as the association
    of the unknown handle value to the window does not exist yet and as not
    to trigger recursive handle creation, etc.

    In that phase, an instance of  QWindowCreationContext is set on
    QWindowsContext.

    QWindowCreationContext stores the information to answer the initial
    WM_GETMINMAXINFO and obtains the corrected size/position.

    \sa WindowCreationData, QWindowsContext
    \internal
    \ingroup qt-lighthouse-win
*/

QWindowCreationContext::QWindowCreationContext(const QWindow *w,
                                               const QRect &geometry,
                                               const QMargins &cm,
                                               DWORD style_, DWORD exStyle_) :
    geometryHint(w, cm), style(style_), exStyle(exStyle_),
    requestedGeometry(geometry), obtainedGeometry(geometry),
    margins(QWindowsGeometryHint::frame(style, exStyle)), customMargins(cm),
    frameX(CW_USEDEFAULT), frameY(CW_USEDEFAULT),
    frameWidth(CW_USEDEFAULT), frameHeight(CW_USEDEFAULT)
{
    // Geometry of toplevels does not consider window frames.
    // TODO: No concept of WA_wasMoved yet that would indicate a
    // CW_USEDEFAULT unless set. For now, assume that 0,0 means 'default'
    // for toplevels.
    if (geometry.isValid()) {
        frameX = geometry.x();
        frameY = geometry.y();
        const QMargins effectiveMargins = margins + customMargins;
        frameWidth = effectiveMargins.left() + geometry.width() + effectiveMargins.right();
        frameHeight = effectiveMargins.top() + geometry.height() + effectiveMargins.bottom();
        const bool isDefaultPosition = !frameX && !frameY && w->isTopLevel();
        if (!QWindowsGeometryHint::positionIncludesFrame(w) && !isDefaultPosition) {
            frameX -= effectiveMargins.left();
            frameY -= effectiveMargins.top();
        }
    }

    qCDebug(lcQpaWindows).nospace()
        << __FUNCTION__ << ' ' << w << geometry
        << " pos incl. frame" << QWindowsGeometryHint::positionIncludesFrame(w)
        << " frame: " << frameWidth << 'x' << frameHeight << '+'
        << frameX << '+' << frameY
        << " min" << geometryHint.minimumSize << " max" << geometryHint.maximumSize
        << " custom margins " << customMargins;
}

/*!
    \class QWindowsWindow
    \brief Raster or OpenGL Window.

    \list
    \li Raster type: handleWmPaint() is implemented to
       to bitblt the image. The DC can be accessed
       via getDC/Relase DC, which has a special handling
       when within a paint event (in that case, the DC obtained
       from BeginPaint() is returned).

    \li Open GL: The first time QWindowsGLContext accesses
       the handle, it sets up the pixelformat on the DC
       which in turn sets it on the window (see flag
       PixelFormatInitialized).
       handleWmPaint() is empty (although required).
    \endlist

    \internal
    \ingroup qt-lighthouse-win
*/

QWindowsWindow::QWindowsWindow(QWindow *aWindow, const QWindowsWindowData &data) :
    QPlatformWindow(aWindow),
    m_data(data),
    m_flags(WithinCreate),
    m_hdc(0),
    m_windowState(Qt::WindowNoState),
    m_opacity(1.0),
    m_dropTarget(0),
    m_savedStyle(0),
    m_format(aWindow->format()),
#if defined(QT_OPENGL_ES_2) || defined(QT_OPENGL_DYNAMIC)
    m_eglSurface(0),
#endif
#ifdef Q_OS_WINCE
    m_previouslyHidden(false),
#endif
    m_iconSmall(0),
    m_iconBig(0)
{
    // Clear the creation context as the window can be found in QWindowsContext's map.
    QWindowsContext::instance()->setWindowCreationContext(QSharedPointer<QWindowCreationContext>());
    QWindowsContext::instance()->addWindow(m_data.hwnd, this);
    const Qt::WindowType type = aWindow->type();
    if (type == Qt::Desktop)
        return; // No further handling for Qt::Desktop
    if (aWindow->surfaceType() == QWindow::OpenGLSurface) {
        setFlag(OpenGLSurface);
#if defined(QT_OPENGL_ES_2) || defined(QT_OPENGL_DYNAMIC)
        if (QOpenGLContext::openGLModuleType() != QOpenGLContext::LibGL)
            setFlag(OpenGL_ES2);
#endif
    }
    updateDropSite();

#ifndef Q_OS_WINCE
    if (QWindowsContext::instance()->systemInfo() & QWindowsContext::SI_SupportsTouch) {
        if (QWindowsContext::user32dll.registerTouchWindow(m_data.hwnd, 0)) {
            setFlag(TouchRegistered);
        } else {
            qErrnoWarning("RegisterTouchWindow() failed for window '%s'.", qPrintable(aWindow->objectName()));
        }
    }
#endif // !Q_OS_WINCE
    setWindowState(aWindow->windowState());
    const qreal opacity = qt_window_private(aWindow)->opacity;
    if (!qFuzzyCompare(opacity, qreal(1.0)))
        setOpacity(opacity);
    if (aWindow->isTopLevel())
        setWindowIcon(aWindow->icon());
    clearFlag(WithinCreate);
}

QWindowsWindow::~QWindowsWindow()
{
    setFlag(WithinDestroy);
#ifndef Q_OS_WINCE
    if (testFlag(TouchRegistered))
        QWindowsContext::user32dll.unregisterTouchWindow(m_data.hwnd);
#endif // !Q_OS_WINCE
    destroyWindow();
    destroyIcon();
}

void QWindowsWindow::fireExpose(const QRegion &region, bool force)
{
    if (region.isEmpty() && !force)
        clearFlag(Exposed);
    else
        setFlag(Exposed);
    QWindowSystemInterface::handleExposeEvent(window(), region);
}

static inline QWindow *findTransientChild(const QWindow *parent)
{
    foreach (QWindow *w, QGuiApplication::topLevelWindows())
        if (w->transientParent() == parent)
            return w;
    return 0;
}

void QWindowsWindow::destroyWindow()
{
    qCDebug(lcQpaWindows) << __FUNCTION__ << this << window() << m_data.hwnd;
    if (m_data.hwnd) { // Stop event dispatching before Window is destroyed.
        setFlag(WithinDestroy);
        // Clear any transient child relationships as Windows will otherwise destroy them (QTBUG-35499, QTBUG-36666)
        if (QWindow *transientChild = findTransientChild(window()))
            if (QWindowsWindow *tw = QWindowsWindow::baseWindowOf(transientChild))
				tw->clearTransientParent();
        QWindowsContext *context = QWindowsContext::instance();
        if (context->windowUnderMouse() == window())
            context->clearWindowUnderMouse();
        if (hasMouseCapture())
            setMouseGrabEnabled(false);
        setDropSiteEnabled(false);
#if defined(QT_OPENGL_ES_2) || defined(QT_OPENGL_DYNAMIC)
        if (m_eglSurface) {
            qCDebug(lcQpaGl) << __FUNCTION__ << "Freeing EGL surface " << m_eglSurface << window();
            eglDestroySurface(m_staticEglContext->display(), m_eglSurface);
            m_eglSurface = 0;
        }
#endif
#ifdef Q_OS_WINCE
        if ((m_windowState & Qt::WindowFullScreen) && !m_previouslyHidden) {
            HWND handle = FindWindow(L"HHTaskBar", L"");
            if (handle) {
                ShowWindow(handle, SW_SHOW);
            }
        }
#endif // !Q_OS_WINCE
        if (m_data.hwnd != GetDesktopWindow() && window()->type() != Qt::ForeignWindow)
            DestroyWindow(m_data.hwnd);
        context->removeWindow(m_data.hwnd);
        m_data.hwnd = 0;
    }
}

void QWindowsWindow::updateDropSite()
{
    bool enabled = false;
    if (window()->isTopLevel()) {
        switch (window()->type()) {
        case Qt::Window:
        case Qt::Dialog:
        case Qt::Sheet:
        case Qt::Drawer:
        case Qt::Popup:
        case Qt::Tool:
            enabled = true;
            break;
        default:
            break;
        }
    }
    setDropSiteEnabled(enabled);
}

void QWindowsWindow::setDropSiteEnabled(bool dropEnabled)
{
    if (isDropSiteEnabled() == dropEnabled)
        return;
    qCDebug(lcQpaMime) << __FUNCTION__ << window() << dropEnabled;
#if !defined(QT_NO_CLIPBOARD) && !defined(QT_NO_DRAGANDDROP)
    if (dropEnabled) {
        Q_ASSERT(m_data.hwnd);
        m_dropTarget = new QWindowsOleDropTarget(window());
        RegisterDragDrop(m_data.hwnd, m_dropTarget);
        CoLockObjectExternal(m_dropTarget, true, true);
    } else {
        m_dropTarget->Release();
        CoLockObjectExternal(m_dropTarget, false, true);
        RevokeDragDrop(m_data.hwnd);
        m_dropTarget = 0;
    }
#endif // !QT_NO_CLIPBOARD && !QT_NO_DRAGANDDROP
}

// Returns topmost QWindowsWindow ancestor even if there are embedded windows in the chain.
// Returns this window if it is the topmost ancestor.
QWindow *QWindowsWindow::topLevelOf(QWindow *w)
{
    while (QWindow *parent = w->parent())
        w = parent;

    if (const QPlatformWindow *handle = w->handle()) {
        const QWindowsWindow *ww = static_cast<const QWindowsWindow *>(handle);
        if (ww->isEmbedded(0)) {
            HWND parentHWND = GetAncestor(ww->handle(), GA_PARENT);
            const HWND desktopHwnd = GetDesktopWindow();
            const QWindowsContext *ctx = QWindowsContext::instance();
            while (parentHWND && parentHWND != desktopHwnd) {
                if (QWindowsWindow *ancestor = ctx->findPlatformWindow(parentHWND))
                    return topLevelOf(ancestor->window());
                parentHWND = GetAncestor(parentHWND, GA_PARENT);
            }
        }
    }
    return w;
}

QWindowsWindowData
    QWindowsWindowData::create(const QWindow *w,
                                       const QWindowsWindowData &parameters,
                                       const QString &title)
{
    WindowCreationData creationData;
    creationData.fromWindow(w, parameters.flags);
    QWindowsWindowData result = creationData.create(w, parameters, title);
    // Force WM_NCCALCSIZE (with wParam=1) via SWP_FRAMECHANGED for custom margin.
    creationData.initialize(result.hwnd, !parameters.customMargins.isNull(), 1);
    return result;
}

void QWindowsWindow::setVisible(bool visible)
{
    qCDebug(lcQpaWindows) << __FUNCTION__ << this << window() << m_data.hwnd << visible;
    if (m_data.hwnd) {
        if (visible) {
            show_sys();

            // When the window is layered, we won't get WM_PAINT, and "we" are in control
            // over the rendering of the window
            // There is nobody waiting for this, so we don't need to flush afterwards.
            if (isLayered()) {
                QWindow *w = window();
                fireExpose(QRect(0, 0, w->width(), w->height()));
            }

        } else {
            if (hasMouseCapture())
                setMouseGrabEnabled(false);
            hide_sys();
            fireExpose(QRegion());
        }
    }
}

bool QWindowsWindow::isVisible() const
{
    return m_data.hwnd && IsWindowVisible(m_data.hwnd);
}

bool QWindowsWindow::isActive() const
{
    // Check for native windows or children of the active native window.
    if (const HWND activeHwnd = GetForegroundWindow())
        if (m_data.hwnd == activeHwnd || IsChild(activeHwnd, m_data.hwnd))
            return true;
    return false;
}

bool QWindowsWindow::isEmbedded(const QPlatformWindow *parentWindow) const
{
    if (parentWindow) {
        const QWindowsWindow *ww = static_cast<const QWindowsWindow *>(parentWindow);
        const HWND hwnd = ww->handle();
        if (!IsChild(hwnd, m_data.hwnd))
            return false;
    }

    if (!m_data.embedded && parent())
        return parent()->isEmbedded(0);

    return m_data.embedded;
}

QPoint QWindowsWindow::mapToGlobal(const QPoint &pos) const
{
    if (m_data.hwnd)
        return QWindowsGeometryHint::mapToGlobal(m_data.hwnd, pos);
    else
        return pos;
}

QPoint QWindowsWindow::mapFromGlobal(const QPoint &pos) const
{
    if (m_data.hwnd)
        return QWindowsGeometryHint::mapFromGlobal(m_data.hwnd, pos);
    else
        return pos;
}

#ifndef Q_OS_WINCE
static inline HWND transientParentHwnd(HWND hwnd)
{
    if (GetAncestor(hwnd, GA_PARENT) == GetDesktopWindow()) {
        const HWND rootOwnerHwnd = GetAncestor(hwnd, GA_ROOTOWNER);
        if (rootOwnerHwnd != hwnd) // May return itself for toplevels.
            return rootOwnerHwnd;
    }
    return 0;
}
#endif // !Q_OS_WINCE

// Update the transient parent for a toplevel window. The concept does not
// really exist on Windows, the relationship is set by passing a parent along with !WS_CHILD
// to window creation or by setting the parent using  GWL_HWNDPARENT (as opposed to
// SetParent, which would make it a real child).
void QWindowsWindow::updateTransientParent() const
{
#ifndef Q_OS_WINCE
    if (window()->type() == Qt::Popup)
        return; // QTBUG-34503, // a popup stays on top, no parent, see also WindowCreationData::fromWindow().
    // Update transient parent.
    const HWND oldTransientParent = transientParentHwnd(m_data.hwnd);
    HWND newTransientParent = 0;
    if (const QWindow *tp = window()->transientParent())
        if (const QWindowsWindow *tw = QWindowsWindow::baseWindowOf(tp))
            if (!tw->testFlag(WithinDestroy)) // Prevent destruction by parent window (QTBUG-35499, QTBUG-36666)
                newTransientParent = tw->handle();
	if (newTransientParent && newTransientParent != oldTransientParent)
        SetWindowLongPtr(m_data.hwnd, GWL_HWNDPARENT, (LONG_PTR)newTransientParent);
#endif // !Q_OS_WINCE
}

void QWindowsWindow::clearTransientParent() const
{
#ifndef Q_OS_WINCE
	if (window()->type() == Qt::Popup)
		return; // QTBUG-34503, // a popup stays on top, no parent, see also WindowCreationData::fromWindow().
	// Update transient parent.
	const HWND oldTransientParent = transientParentHwnd(m_data.hwnd);
	HWND newTransientParent = 0;
	if (newTransientParent != oldTransientParent)
		SetWindowLongPtr(m_data.hwnd, GWL_HWNDPARENT, (LONG_PTR)newTransientParent);
#endif // !Q_OS_WINCE
}

// partially from QWidgetPrivate::show_sys()
void QWindowsWindow::show_sys() const
{
    int sm = SW_SHOWNORMAL;
    bool fakedMaximize = false;
    const QWindow *w = window();
    const Qt::WindowFlags flags = w->flags();
    const Qt::WindowType type = w->type();
    if (w->isTopLevel()) {
        const Qt::WindowState state = w->windowState();
        if (state & Qt::WindowMinimized) {
            sm = SW_SHOWMINIMIZED;
            if (!isVisible())
                sm = SW_SHOWMINNOACTIVE;
        } else {
            updateTransientParent();
            if (state & Qt::WindowMaximized) {
                sm = SW_SHOWMAXIMIZED;
                // Windows will not behave correctly when we try to maximize a window which does not
                // have minimize nor maximize buttons in the window frame. Windows would then ignore
                // non-available geometry, and rather maximize the widget to the full screen, minus the
                // window frame (caption). So, we do a trick here, by adding a maximize button before
                // maximizing the widget, and then remove the maximize button afterwards.
                if (flags & Qt::WindowTitleHint &&
                        !(flags & (Qt::WindowMinMaxButtonsHint | Qt::FramelessWindowHint))) {
                    fakedMaximize = TRUE;
                    setStyle(style() | WS_MAXIMIZEBOX);
                }
            } // Qt::WindowMaximized
        } // !Qt::WindowMinimized
    }
    if (type == Qt::Popup || type == Qt::ToolTip || type == Qt::Tool)
        sm = SW_SHOWNOACTIVATE;

    if (w->windowState() & Qt::WindowMaximized)
        setFlag(WithinMaximize); // QTBUG-8361

    ShowWindow(m_data.hwnd, sm);

    clearFlag(WithinMaximize);

    if (fakedMaximize) {
        setStyle(style() & ~WS_MAXIMIZEBOX);
        SetWindowPos(m_data.hwnd, 0, 0, 0, 0, 0,
                     SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER
                     | SWP_FRAMECHANGED);
    }
}

// partially from QWidgetPrivate::hide_sys()
void QWindowsWindow::hide_sys() const
{
    const Qt::WindowFlags flags = window()->flags();
    if (flags != Qt::Desktop) {
        if (flags & Qt::Popup)
            ShowWindow(m_data.hwnd, SW_HIDE);
        else
            SetWindowPos(m_data.hwnd,0, 0,0,0,0, SWP_HIDEWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
}

void QWindowsWindow::setParent(const QPlatformWindow *newParent)
{
    qCDebug(lcQpaWindows) << __FUNCTION__ << window() << newParent;

    if (m_data.hwnd)
        setParent_sys(newParent);
}

void QWindowsWindow::setParent_sys(const QPlatformWindow *parent)
{
    // Use GetAncestor instead of GetParent, as GetParent can return owner window for toplevels
    HWND oldParentHWND = GetAncestor(m_data.hwnd, GA_PARENT);
    HWND newParentHWND = 0;
    if (parent) {
        const QWindowsWindow *parentW = static_cast<const QWindowsWindow *>(parent);
        newParentHWND = parentW->handle();

    }

    // NULL handle means desktop window, which also has its proper handle -> disambiguate
    HWND desktopHwnd = GetDesktopWindow();
    if (oldParentHWND == desktopHwnd)
        oldParentHWND = 0;
    if (newParentHWND == desktopHwnd)
        newParentHWND = 0;

    if (newParentHWND != oldParentHWND) {
        const bool wasTopLevel = oldParentHWND == 0;
        const bool isTopLevel = newParentHWND == 0;

        setFlag(WithinSetParent);
        SetParent(m_data.hwnd, newParentHWND);
        clearFlag(WithinSetParent);

        // WS_CHILD/WS_POPUP must be manually set/cleared in addition
        // to dialog frames, etc (see  SetParent() ) if the top level state changes.
        // Force toplevel state as QWindow::isTopLevel cannot be relied upon here.
        if (wasTopLevel != isTopLevel) {
            setDropSiteEnabled(false);
            setWindowFlags_sys(window()->flags(), unsigned(isTopLevel ? WindowCreationData::ForceTopLevel : WindowCreationData::ForceChild));
            updateDropSite();
        }
    }
}

void QWindowsWindow::handleHidden()
{
    fireExpose(QRegion());
}

void QWindowsWindow::handleCompositionSettingsChanged()
{
    const QWindow *w = window();
    if (w->surfaceType() == QWindow::OpenGLSurface && w->format().hasAlpha())
        applyBlurBehindWindow(handle());
}

static QRect normalFrameGeometry(HWND hwnd)
{
#ifndef Q_OS_WINCE
    WINDOWPLACEMENT wp;
    wp.length = sizeof(WINDOWPLACEMENT);
    if (GetWindowPlacement(hwnd, &wp))
        return qrectFromRECT(wp.rcNormalPosition);
#else
    Q_UNUSED(hwnd)
#endif
    return QRect();
}

QRect QWindowsWindow::normalGeometry() const
{
    // Check for fake 'fullscreen' mode.
    const bool fakeFullScreen = m_savedFrameGeometry.isValid() && window()->windowState() == Qt::WindowFullScreen;
    const QRect frame = fakeFullScreen ? m_savedFrameGeometry : normalFrameGeometry(m_data.hwnd);
    const QMargins margins = fakeFullScreen ? QWindowsGeometryHint::frame(m_savedStyle, 0) : frameMargins();
    return frame.isValid() ? frame.marginsRemoved(margins) : frame;
}

void QWindowsWindow::setGeometry(const QRect &rectIn)
{
    QRect rect = rectIn;
    // This means it is a call from QWindow::setFramePosition() and
    // the coordinates include the frame (size is still the contents rectangle).
    if (QWindowsGeometryHint::positionIncludesFrame(window())) {
        const QMargins margins = frameMargins();
        rect.moveTopLeft(rect.topLeft() + QPoint(margins.left(), margins.top()));
    }
    const QSize oldSize = m_data.geometry.size();
    m_data.geometry = rect;
    const QSize newSize = rect.size();
    // Check on hint.
    if (newSize != oldSize) {
        const QWindowsGeometryHint hint(window(), m_data.customMargins);
        if (!hint.validSize(newSize)) {
            qWarning("%s: Attempt to set a size (%dx%d) violating the constraints"
                     "(%dx%d - %dx%d) on window %s/'%s'.", __FUNCTION__,
                     newSize.width(), newSize.height(),
                     hint.minimumSize.width(), hint.minimumSize.height(),
                     hint.maximumSize.width(), hint.maximumSize.height(),
                     window()->metaObject()->className(), qPrintable(window()->objectName()));
        }
    }
    if (m_data.hwnd) {
        // A ResizeEvent with resulting geometry will be sent. If we cannot
        // achieve that size (for example, window title minimal constraint),
        // notify and warn.
        setGeometry_sys(rect);
        if (m_data.geometry != rect) {
            qWarning("%s: Unable to set geometry %dx%d+%d+%d on %s/'%s'."
                     " Resulting geometry:  %dx%d+%d+%d "
                     "(frame: %d, %d, %d, %d, custom margin: %d, %d, %d, %d"
                     ", minimum size: %dx%d, maximum size: %dx%d).",
                     __FUNCTION__,
                     rect.width(), rect.height(), rect.x(), rect.y(),
                     window()->metaObject()->className(), qPrintable(window()->objectName()),
                     m_data.geometry.width(), m_data.geometry.height(),
                     m_data.geometry.x(), m_data.geometry.y(),
                     m_data.frame.left(), m_data.frame.top(),
                     m_data.frame.right(), m_data.frame.bottom(),
                     m_data.customMargins.left(), m_data.customMargins.top(),
                     m_data.customMargins.right(), m_data.customMargins.bottom(),
                     window()->minimumWidth(), window()->minimumHeight(),
                     window()->maximumWidth(), window()->maximumHeight());
        }
    } else {
        QPlatformWindow::setGeometry(rect);
    }
}

void QWindowsWindow::handleMoved()
{
    // Minimize/Set parent can send nonsensical move events.
    if (!IsIconic(m_data.hwnd) && !testFlag(WithinSetParent))
        handleGeometryChange();
}

void QWindowsWindow::handleResized(int wParam)
{
    switch (wParam) {
    case SIZE_MAXHIDE: // Some other window affected.
    case SIZE_MAXSHOW:
        return;
    case SIZE_MINIMIZED:
        handleWindowStateChange(Qt::WindowMinimized);
        return;
    case SIZE_MAXIMIZED:
        handleWindowStateChange(Qt::WindowMaximized);
        handleGeometryChange();
        break;
    case SIZE_RESTORED:
        if (isFullScreen_sys())
            handleWindowStateChange(Qt::WindowFullScreen);
        else if (m_windowState != Qt::WindowNoState && !testFlag(MaximizeToFullScreen))
            handleWindowStateChange(Qt::WindowNoState);
        handleGeometryChange();
        break;
    }
}

void QWindowsWindow::handleGeometryChange()
{
    //Prevent recursive resizes for Windows CE
    if (testFlag(WithinSetStyle))
        return;
    const QRect previousGeometry = m_data.geometry;
    m_data.geometry = geometry_sys();
    QPlatformWindow::setGeometry(m_data.geometry);
    QWindowSystemInterface::handleGeometryChange(window(), m_data.geometry);
    // QTBUG-32121: OpenGL/normal windows (with exception of ANGLE) do not receive
    // expose events when shrinking, synthesize.
    if (!testFlag(OpenGL_ES2) && isExposed()
        && !(m_data.geometry.width() > previousGeometry.width() || m_data.geometry.height() > previousGeometry.height())) {
        fireExpose(QRegion(m_data.geometry), true);
    }
    if (testFlag(SynchronousGeometryChangeEvent))
        QWindowSystemInterface::flushWindowSystemEvents();

    qCDebug(lcQpaEvents) << __FUNCTION__ << this << window() << m_data.geometry;
}

void QWindowsWindow::setGeometry_sys(const QRect &rect) const
{
    const QMargins margins = frameMargins();
    const QRect frameGeometry = rect + margins;

    qCDebug(lcQpaWindows) << '>' << __FUNCTION__ << this << window()
                 << "    \n from " << geometry_sys() << " frame: "
                 << margins << " to " <<rect
                 << " new frame: " << frameGeometry;

    const bool rc = MoveWindow(m_data.hwnd, frameGeometry.x(), frameGeometry.y(),
                               frameGeometry.width(), frameGeometry.height(), true);
    qCDebug(lcQpaWindows) << '<' << __FUNCTION__ << this << window()
        << "    \n resulting " << rc << geometry_sys();
}

QRect QWindowsWindow::frameGeometry_sys() const
{
    // Warning: Returns bogus values when minimized.
    bool isRealTopLevel = window()->isTopLevel() && !m_data.embedded;
    return frameGeometry(m_data.hwnd, isRealTopLevel);
}

QRect QWindowsWindow::geometry_sys() const
{
    return frameGeometry_sys().marginsRemoved(frameMargins());
}

/*!
    Allocates a HDC for the window or returns the temporary one
    obtained from WinAPI BeginPaint within a WM_PAINT event.

    \sa releaseDC()
*/

HDC QWindowsWindow::getDC()
{
    if (!m_hdc)
        m_hdc = GetDC(handle());
    return m_hdc;
}

/*!
    Relases the HDC for the window or does nothing in
    case it was obtained from WinAPI BeginPaint within a WM_PAINT event.

    \sa getDC()
*/

void QWindowsWindow::releaseDC()
{
    if (m_hdc) {
        ReleaseDC(handle(), m_hdc);
        m_hdc = 0;
    }
}

bool QWindowsWindow::handleWmPaint(HWND hwnd, UINT message,
                                         WPARAM, LPARAM)
{
    // Ignore invalid update bounding rectangles
    if (!GetUpdateRect(m_data.hwnd, 0, FALSE))
        return false;
    if (message == WM_ERASEBKGND) // Backing store - ignored.
        return true;
    PAINTSTRUCT ps;

    // Observed painting problems with Aero style disabled (QTBUG-7865).
    if (testFlag(OpenGLSurface) && testFlag(OpenGLDoubleBuffered))
        InvalidateRect(hwnd, 0, false);

    BeginPaint(hwnd, &ps);

    // If the a window is obscured by another window (such as a child window)
    // we still need to send isExposed=true, for compatibility.
    // Our tests depend on it.
    fireExpose(QRegion(qrectFromRECT(ps.rcPaint)), true);
    if (!QWindowsContext::instance()->asyncExpose())
        QWindowSystemInterface::flushWindowSystemEvents();

    EndPaint(hwnd, &ps);
    return true;
}

void QWindowsWindow::setWindowTitle(const QString &title)
{
    qCDebug(lcQpaWindows) << __FUNCTION__ << this << window() <<title;
    if (m_data.hwnd) {
        const QString fullTitle = formatWindowTitle(title, QStringLiteral(" - "));
        SetWindowText(m_data.hwnd, (const wchar_t*)fullTitle.utf16());
    }
}

void QWindowsWindow::setWindowFlags(Qt::WindowFlags flags)
{
    qCDebug(lcQpaWindows) << '>' << __FUNCTION__ << this << window() << "\n    from: "
        << QWindowsWindow::debugWindowFlags(m_data.flags)
        << "\n    to: " << QWindowsWindow::debugWindowFlags(flags);
    const QRect oldGeometry = geometry();
    if (m_data.flags != flags) {
        m_data.flags = flags;
        if (m_data.hwnd) {
            m_data = setWindowFlags_sys(flags);
            updateDropSite();
        }
    }
    // When switching to a frameless window, geometry
    // may change without a WM_MOVE. Report change manually.
    // Do not send synchronously as not to clobber the widget
    // geometry in a sequence of setting flags and geometry.
    const QRect newGeometry = geometry_sys();
    if (oldGeometry != newGeometry)
        handleGeometryChange();

    qCDebug(lcQpaWindows) << '<' << __FUNCTION__ << "\n    returns: "
        << QWindowsWindow::debugWindowFlags(m_data.flags)
        << " geometry " << oldGeometry << "->" << newGeometry;
}

QWindowsWindowData QWindowsWindow::setWindowFlags_sys(Qt::WindowFlags wt,
                                                              unsigned flags) const
{
    WindowCreationData creationData;
    creationData.fromWindow(window(), wt, flags);
    creationData.applyWindowFlags(m_data.hwnd);
    creationData.initialize(m_data.hwnd, true, m_opacity);

    QWindowsWindowData result = m_data;
    result.flags = creationData.flags;
    result.embedded = creationData.embedded;
    setFlag(FrameDirty);
    return result;
}

void QWindowsWindow::handleWindowStateChange(Qt::WindowState state)
{
    qCDebug(lcQpaWindows) << __FUNCTION__ << this << window()
                 << "\n    from " << debugWindowStates(m_windowState)
                 << " to " << debugWindowStates(state);
    setFlag(FrameDirty);
    m_windowState = state;
    QWindowSystemInterface::handleWindowStateChanged(window(), state);
    switch (state) {
    case Qt::WindowMinimized:
        handleHidden();
        QWindowSystemInterface::flushWindowSystemEvents(); // Tell QQuickWindow to stop rendering now.
        break;
    case Qt::WindowMaximized:
    case Qt::WindowFullScreen:
    case Qt::WindowNoState: {
        // QTBUG-17548: We send expose events when receiving WM_Paint, but for
        // layered windows and transient children, we won't receive any WM_Paint.
        QWindow *w = window();
        bool exposeEventsSent = false;
        if (isLayered()) {
            fireExpose(QRegion(0, 0, w->width(), w->height()));
            exposeEventsSent = true;
        }
        foreach (QWindow *child, QGuiApplication::allWindows()) {
            if (child != w && child->isVisible() && child->transientParent() == w) {
                QWindowsWindow *platformWindow = QWindowsWindow::baseWindowOf(child);
                if (platformWindow->isLayered()) {
                    platformWindow->fireExpose(QRegion(0, 0, child->width(), child->height()));
                    exposeEventsSent = true;
                }
            }
        }
        if (exposeEventsSent && !QWindowsContext::instance()->asyncExpose())
            QWindowSystemInterface::flushWindowSystemEvents();
    }
        break;
    default:
        break;
    }
}

void QWindowsWindow::setWindowState(Qt::WindowState state)
{
    if (m_data.hwnd) {
        setWindowState_sys(state);
        m_windowState = state;
    }
}

// Return the effective screen for full screen mode in a virtual desktop.
static const QScreen *effectiveScreen(const QWindow *w)
{
    QPoint center = w->geometry().center();
    if (!w->isTopLevel())
        center = w->mapToGlobal(center);
    const QScreen *screen = w->screen();
    if (!screen->geometry().contains(center))
        foreach (const QScreen *sibling, screen->virtualSiblings())
            if (sibling->geometry().contains(center))
                return sibling;
    return screen;
}

bool QWindowsWindow::isFullScreen_sys() const
{
    return window()->isTopLevel() && geometry_sys() == effectiveScreen(window())->geometry();
}

/*!
    \brief Change the window state.

    \note Window frames change when maximized;
    the top margin shrinks somewhat but that cannot be obtained using
    AdjustWindowRectEx().

    \note Some calls to SetWindowLong require a subsequent call
    to ShowWindow.
*/

void QWindowsWindow::setWindowState_sys(Qt::WindowState newState)
{
    const Qt::WindowState oldState = m_windowState;
    if (oldState == newState)
        return;
    qCDebug(lcQpaWindows) << '>' << __FUNCTION__ << this << window()
        << " from " << debugWindowStates(oldState) << " to " << debugWindowStates(newState);

    const bool visible = isVisible();

    setFlag(FrameDirty);

    if ((oldState == Qt::WindowMaximized) != (newState == Qt::WindowMaximized)) {
        if (visible && !(newState == Qt::WindowMinimized)) {
            setFlag(WithinMaximize);
            if (newState == Qt::WindowFullScreen)
                setFlag(MaximizeToFullScreen);
            ShowWindow(m_data.hwnd, (newState == Qt::WindowMaximized) ? SW_MAXIMIZE : SW_SHOWNOACTIVATE);
            clearFlag(WithinMaximize);
            clearFlag(MaximizeToFullScreen);
        }
    }

    if ((oldState == Qt::WindowFullScreen) != (newState == Qt::WindowFullScreen)) {
#ifdef Q_OS_WINCE
        HWND handle = FindWindow(L"HHTaskBar", L"");
        if (handle) {
            if (newState == Qt::WindowFullScreen) {
                BOOL hidden = ShowWindow(handle, SW_HIDE);
                if (!hidden)
                    m_previouslyHidden = true;
            } else if (!m_previouslyHidden){
                ShowWindow(handle, SW_SHOW);
            }
        }
#endif
        if (newState == Qt::WindowFullScreen) {
#ifndef Q_FLATTEN_EXPOSE
            UINT newStyle = WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_POPUP;
#else
            UINT newStyle = WS_POPUP;
#endif
            // Save geometry and style to be restored when fullscreen
            // is turned off again, since on Windows, it is not a real
            // Window state but emulated by changing geometry and style.
            if (!m_savedStyle) {
                m_savedStyle = style();
#ifndef Q_OS_WINCE
                if (oldState == Qt::WindowMinimized) {
                    const QRect nf = normalFrameGeometry(m_data.hwnd);
                    if (nf.isValid())
                        m_savedFrameGeometry = nf;
                } else {
#endif
                    m_savedFrameGeometry = frameGeometry_sys();
#ifndef Q_OS_WINCE
                }
#endif
            }
            if (m_savedStyle & WS_SYSMENU)
                newStyle |= WS_SYSMENU;
            if (visible)
                newStyle |= WS_VISIBLE;
            setStyle(newStyle);
            // Use geometry of QWindow::screen() within creation or the virtual screen the
            // window is in (QTBUG-31166, QTBUG-30724).
            const QScreen *screen = testFlag(WithinCreate) ? window()->screen() : effectiveScreen(window());
            const QRect r = screen->geometry();
            const UINT swpf = SWP_FRAMECHANGED | SWP_NOACTIVATE;
            const bool wasSync = testFlag(SynchronousGeometryChangeEvent);
            setFlag(SynchronousGeometryChangeEvent);
            SetWindowPos(m_data.hwnd, HWND_TOP, r.left(), r.top(), r.width(), r.height(), swpf);
            if (!wasSync)
                clearFlag(SynchronousGeometryChangeEvent);
            QWindowSystemInterface::handleGeometryChange(window(), r);
            QWindowSystemInterface::flushWindowSystemEvents();
        } else if (newState != Qt::WindowMinimized) {
            // Restore saved state.
            unsigned newStyle = m_savedStyle ? m_savedStyle : style();
            if (visible)
                newStyle |= WS_VISIBLE;
            setStyle(newStyle);

            UINT swpf = SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE;
            if (!m_savedFrameGeometry.isValid())
                swpf |= SWP_NOSIZE | SWP_NOMOVE;
            const bool wasSync = testFlag(SynchronousGeometryChangeEvent);
            setFlag(SynchronousGeometryChangeEvent);
            SetWindowPos(m_data.hwnd, 0, m_savedFrameGeometry.x(), m_savedFrameGeometry.y(),
                         m_savedFrameGeometry.width(), m_savedFrameGeometry.height(), swpf);
            if (!wasSync)
                clearFlag(SynchronousGeometryChangeEvent);
            // preserve maximized state
            if (visible)
                ShowWindow(m_data.hwnd, (newState == Qt::WindowMaximized) ? SW_MAXIMIZE : SW_SHOWNOACTIVATE);
            m_savedStyle = 0;
            m_savedFrameGeometry = QRect();
        }
    }

    if ((oldState == Qt::WindowMinimized) != (newState == Qt::WindowMinimized)) {
        if (visible)
            ShowWindow(m_data.hwnd, (newState == Qt::WindowMinimized) ? SW_MINIMIZE :
                       (newState == Qt::WindowMaximized) ? SW_MAXIMIZE : SW_SHOWNOACTIVATE);
    }
    qCDebug(lcQpaWindows) << '<' << __FUNCTION__ << this << window() << debugWindowStates(newState);
}

void QWindowsWindow::setStyle(unsigned s) const
{
    qCDebug(lcQpaWindows) << __FUNCTION__ << this << window() << debugWinStyle(s);
    setFlag(WithinSetStyle);
    setFlag(FrameDirty);
    SetWindowLongPtr(m_data.hwnd, GWL_STYLE, s);
    clearFlag(WithinSetStyle);
}

void QWindowsWindow::setExStyle(unsigned s) const
{
    qCDebug(lcQpaWindows).nospace() << __FUNCTION__ << ' ' << this << ' ' << window()
        << " 0x" << QByteArray::number(s, 16);
    setFlag(FrameDirty);
    SetWindowLongPtr(m_data.hwnd, GWL_EXSTYLE, s);
}

void QWindowsWindow::raise()
{
    qCDebug(lcQpaWindows) << __FUNCTION__ << this << window();
    SetWindowPos(m_data.hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
}

void QWindowsWindow::lower()
{
    qCDebug(lcQpaWindows) << __FUNCTION__ << this << window();
    if (m_data.hwnd)
        SetWindowPos(m_data.hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
}

void QWindowsWindow::windowEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::WindowBlocked: // Blocked by another modal window.
        setEnabled(false);
        setFlag(BlockedByModal);
        break;
    case QEvent::WindowUnblocked:
        setEnabled(true);
        clearFlag(BlockedByModal);
        break;
    default:
        break;
    }
}

void QWindowsWindow::propagateSizeHints()
{
    qCDebug(lcQpaWindows) << __FUNCTION__ << this << window();
}

QMargins QWindowsWindow::frameMargins() const
{
    // Frames are invalidated by style changes (window state, flags).
    // As they are also required for geometry calculations in resize
    // event sequences, introduce a dirty flag mechanism to be able
    // to cache results.
    if (testFlag(FrameDirty)) {
        m_data.frame = QWindowsGeometryHint::frame(style(), exStyle());
        clearFlag(FrameDirty);
    }
    return m_data.frame + m_data.customMargins;
}

void QWindowsWindow::setOpacity(qreal level)
{
    qCDebug(lcQpaWindows) << __FUNCTION__ << level;
    if (m_opacity != level) {
        m_opacity = level;
        if (m_data.hwnd)
            setWindowOpacity(m_data.hwnd, m_data.flags,
                             window()->format().hasAlpha(), testFlag(OpenGLSurface),
                             level);
    }
}

static inline HRGN createRectRegion(const QRect &r)
{
    return CreateRectRgn(r.left(), r.top(), r.x() + r.width(), r.y() + r.height());
}

static inline void addRectToWinRegion(const QRect &rect, HRGN *winRegion)
{
    if (const HRGN rectRegion = createRectRegion(rect)) {
        HRGN result = CreateRectRgn(0, 0, 0, 0);
        if (CombineRgn(result, *winRegion, rectRegion, RGN_OR)) {
            DeleteObject(*winRegion);
            *winRegion = result;
        }
        DeleteObject(rectRegion);
    }
}

static HRGN qRegionToWinRegion(const QRegion &region)
{
    const QVector<QRect> rects = region.rects();
    if (rects.isEmpty())
        return NULL;
    const int rectCount = rects.size();
    if (rectCount == 1)
        return createRectRegion(region.boundingRect());
    HRGN hRegion = createRectRegion(rects.front());
    for (int i = 1; i < rectCount; ++i)
        addRectToWinRegion(rects.at(i), &hRegion);
    return hRegion;
}

void QWindowsWindow::setMask(const QRegion &region)
{
    if (region.isEmpty()) {
         SetWindowRgn(m_data.hwnd, 0, true);
         return;
    }
    const HRGN winRegion = qRegionToWinRegion(region);

    // Mask is in client area coordinates, so offset it in case we have a frame
    if (window()->isTopLevel()) {
        const QMargins margins = frameMargins();
        OffsetRgn(winRegion, margins.left(), margins.top());
    }

    // SetWindowRgn takes ownership.
    if (!SetWindowRgn(m_data.hwnd, winRegion, true))
        DeleteObject(winRegion);
}

void QWindowsWindow::requestActivateWindow()
{
    qCDebug(lcQpaWindows) << __FUNCTION__ << this << window();
    // 'Active' state handling is based in focus since it needs to work for
    // child windows as well.
    if (m_data.hwnd) {
        SetForegroundWindow(m_data.hwnd);
        SetFocus(m_data.hwnd);
    }
}

bool QWindowsWindow::setKeyboardGrabEnabled(bool grab)
{
    if (!m_data.hwnd) {
        qWarning("%s: No handle", __FUNCTION__);
        return false;
    }
    qCDebug(lcQpaWindows) << __FUNCTION__ << this << window() << grab;

    QWindowsContext *context = QWindowsContext::instance();
    if (grab) {
        context->setKeyGrabber(window());
    } else {
        if (context->keyGrabber() == window())
            context->setKeyGrabber(0);
    }
    return true;
}

bool QWindowsWindow::setMouseGrabEnabled(bool grab)
{
    qCDebug(lcQpaWindows) << __FUNCTION__ << window() << grab;
    if (!m_data.hwnd) {
        qWarning("%s: No handle", __FUNCTION__);
        return false;
    }
    if (!isVisible() && grab) {
        qWarning("%s: Not setting mouse grab for invisible window %s/'%s'",
                 __FUNCTION__, window()->metaObject()->className(),
                 qPrintable(window()->objectName()));
        return false;
    }
    // release grab or an explicit grab overriding autocapture: Clear flag.
    clearFlag(QWindowsWindow::AutoMouseCapture);
    if (hasMouseCapture() != grab) {
        if (grab) {
            SetCapture(m_data.hwnd);
        } else {
            ReleaseCapture();
        }
    }
    return grab;
}

static inline DWORD cornerToWinOrientation(Qt::Corner corner)
{
    switch (corner) {
    case Qt::TopLeftCorner:
        return 0xf004; // SZ_SIZETOPLEFT;
    case Qt::TopRightCorner:
        return 0xf005; // SZ_SIZETOPRIGHT
    case Qt::BottomLeftCorner:
        return 0xf007; // SZ_SIZEBOTTOMLEFT
    case Qt::BottomRightCorner:
        return 0xf008; // SZ_SIZEBOTTOMRIGHT
    }
    return 0;
}

bool QWindowsWindow::startSystemResize(const QPoint &, Qt::Corner corner)
{
    if (!GetSystemMenu(m_data.hwnd, FALSE))
        return false;

    ReleaseCapture();
    PostMessage(m_data.hwnd, WM_SYSCOMMAND, cornerToWinOrientation(corner), 0);
    setFlag(SizeGripOperation);
    return true;
}

void QWindowsWindow::setFrameStrutEventsEnabled(bool enabled)
{
    if (enabled) {
        setFlag(FrameStrutEventsEnabled);
    } else {
        clearFlag(FrameStrutEventsEnabled);
    }
}

#ifndef Q_OS_WINCE // maybe available on some SDKs revisit WM_GETMINMAXINFO
void QWindowsWindow::getSizeHints(MINMAXINFO *mmi) const
{
    const QWindowsGeometryHint hint(window(), m_data.customMargins);
    hint.applyToMinMaxInfo(m_data.hwnd, mmi);

    if ((testFlag(WithinMaximize) || (window()->windowState() == Qt::WindowMinimized))
            && (m_data.flags & Qt::FramelessWindowHint)) {
        // This block fixes QTBUG-8361: Frameless windows shouldn't cover the
        // taskbar when maximized
        const QScreen *screen = effectiveScreen(window());

        // Documentation of MINMAXINFO states that it will only work for the primary screen
        if (screen && screen == QGuiApplication::primaryScreen()) {
            mmi->ptMaxSize.y = screen->availableGeometry().height();

            // Width, because you can have the taskbar on the sides too.
            mmi->ptMaxSize.x = screen->availableGeometry().width();

            // If you have the taskbar on top, or on the left you don't want it at (0,0):
            mmi->ptMaxPosition.x = screen->availableGeometry().x();
            mmi->ptMaxPosition.y = screen->availableGeometry().y();
        } else if (!screen){
            qWarning() << "effectiveScreen() returned a null screen";
        }
    }

    qCDebug(lcQpaWindows) << __FUNCTION__ << window() << *mmi;
}

bool QWindowsWindow::handleNonClientHitTest(const QPoint &globalPos, LRESULT *result) const
{
    // QTBUG-32663, suppress resize cursor for fixed size windows.
    const QWindow *w = window();
    if (!w->isTopLevel() // Task 105852, minimized windows need to respond to user input.
        || (m_windowState != Qt::WindowNoState && m_windowState != Qt::WindowActive)
        || (m_data.flags & Qt::FramelessWindowHint)) {
        return false;
    }
    const QSize minimumSize = w->minimumSize();
    if (minimumSize.isEmpty())
        return false;
    const QSize maximumSize = w->maximumSize();
    const bool fixedWidth = minimumSize.width() == maximumSize.width();
    const bool fixedHeight = minimumSize.height() == maximumSize.height();
    if (!fixedWidth && !fixedHeight)
        return false;
    const QPoint localPos = w->mapFromGlobal(globalPos);
    const QSize size = w->size();
    if (fixedHeight) {
        if (localPos.y() >= size.height()) {
            *result = HTBORDER; // Unspecified border, no resize cursor.
            return true;
        }
        if (localPos.y() < 0) {
            const QMargins margins = frameMargins();
            const int topResizeBarPos = margins.left() - margins.top();
            if (localPos.y() < topResizeBarPos) {
                *result = HTCAPTION; // Extend caption over top resize bar, let's user move the window.
                return true;
            }
        }
    }
    if (fixedWidth && (localPos.x() < 0 || localPos.x() >= size.width())) {
        *result = HTBORDER; // Unspecified border, no resize cursor.
        return true;
    }
    return false;
}

#endif // !Q_OS_WINCE

#ifndef QT_NO_CURSOR
// Return the default cursor (Arrow) from QWindowsCursor's cache.
static inline QWindowsWindowCursor defaultCursor(const QWindow *w)
{
    if (QScreen *screen = w->screen())
        if (const QPlatformScreen *platformScreen = screen->handle())
            if (QPlatformCursor *cursor = platformScreen->cursor())
                return static_cast<QWindowsCursor *>(cursor)->standardWindowCursor(Qt::ArrowCursor);
    return QWindowsWindowCursor(Qt::ArrowCursor);
}

// Check whether to apply a new cursor. Either the window in question is
// currently under mouse, or it is the parent of the window under mouse and
// there is no other window with an explicitly set cursor in-between.
static inline bool applyNewCursor(const QWindow *w)
{
    const QWindow *underMouse = QWindowsContext::instance()->windowUnderMouse();
    if (underMouse == w)
        return true;
    for (const QWindow *p = underMouse; p ; p = p->parent()) {
        if (p == w)
            return true;
        if (!QWindowsWindow::baseWindowOf(p)->cursor().isNull())
            return false;
    }
    return false;
}
#endif // !QT_NO_CURSOR

/*!
    \brief Applies to cursor property set on the window to the global cursor.

    \sa QWindowsCursor
*/

void QWindowsWindow::applyCursor()
{
#ifndef QT_NO_CURSOR
    if (m_cursor.isNull()) { // Recurse up to parent with non-null cursor. Set default for toplevel.
        if (const QWindow *p = window()->parent()) {
            QWindowsWindow::baseWindowOf(p)->applyCursor();
        } else {
            SetCursor(defaultCursor(window()).handle());
        }
    } else {
        SetCursor(m_cursor.handle());
    }
#endif
}

void QWindowsWindow::setCursor(const QWindowsWindowCursor &c)
{
#ifndef QT_NO_CURSOR
    if (c.handle() != m_cursor.handle()) {
        const bool apply = applyNewCursor(window());
        qCDebug(lcQpaWindows) <<window() << __FUNCTION__
            << "Shape=" << c.cursor().shape() << " doApply=" << apply;
        m_cursor = c;
        if (apply)
            applyCursor();
    }
#endif
}

/*!
    \brief Find a child window using flags from  ChildWindowFromPointEx.
*/

QWindowsWindow *QWindowsWindow::childAtScreenPoint(const QPoint &screenPoint,
                                                           unsigned cwexflags) const
{
    if (m_data.hwnd)
        return QWindowsContext::instance()->findPlatformWindowAt(m_data.hwnd, screenPoint, cwexflags);
    return 0;
}

QWindowsWindow *QWindowsWindow::childAt(const QPoint &clientPoint, unsigned cwexflags) const
{
    if (m_data.hwnd)
        return childAtScreenPoint(QWindowsGeometryHint::mapToGlobal(m_data.hwnd, clientPoint),
                                  cwexflags);
    return 0;
}

#ifndef Q_OS_WINCE
void QWindowsWindow::setAlertState(bool enabled)
{
    if (isAlertState() == enabled)
        return;
    if (enabled) {
        alertWindow(0);
        setFlag(AlertState);
    } else {
        stopAlertWindow();
        clearFlag(AlertState);
    }
}

void QWindowsWindow::alertWindow(int durationMs)
{
    DWORD timeOutMs = GetCaretBlinkTime();
    if (!timeOutMs || timeOutMs == INFINITE)
        timeOutMs = 250;

    FLASHWINFO info;
    info.cbSize = sizeof(info);
    info.hwnd = m_data.hwnd;
    info.dwFlags = FLASHW_TRAY;
    info.dwTimeout = timeOutMs;
    info.uCount = durationMs == 0 ? 10 : durationMs / timeOutMs;
    FlashWindowEx(&info);
}

void QWindowsWindow::stopAlertWindow()
{
    FLASHWINFO info;
    info.cbSize = sizeof(info);
    info.hwnd = m_data.hwnd;
    info.dwFlags = FLASHW_STOP;
    info.dwTimeout = 0;
    info.uCount = 0;
    FlashWindowEx(&info);
}
#endif // !Q_OS_WINCE

bool QWindowsWindow::isEnabled() const
{
    return (style() & WS_DISABLED) == 0;
}

void QWindowsWindow::setEnabled(bool enabled)
{
    const unsigned oldStyle = style();
    unsigned newStyle = oldStyle;
    if (enabled) {
        newStyle &= ~WS_DISABLED;
    } else {
        newStyle |= WS_DISABLED;
    }
    if (newStyle != oldStyle)
        setStyle(newStyle);
}

#if defined(QT_OPENGL_ES_2) || defined(QT_OPENGL_DYNAMIC)
EGLSurface QWindowsWindow::ensureEglSurfaceHandle(const QWindowsWindow::QWindowsEGLStaticContextPtr &staticContext, EGLConfig config)
{
    if (!m_eglSurface) {
        m_staticEglContext = staticContext;
        m_eglSurface = eglCreateWindowSurface(staticContext->display(), config, (EGLNativeWindowType)m_data.hwnd, NULL);
        if (m_eglSurface == EGL_NO_SURFACE)
            qWarning("%s: Could not create the egl surface for %s/'%s' (eglCreateWindowSurface failed): error = 0x%x\n",
                     Q_FUNC_INFO, window()->metaObject()->className(),
                     qPrintable(window()->objectName()), eglGetError());

            qCDebug(lcQpaGl) << __FUNCTION__<<"Created EGL surface "<< m_eglSurface <<window();
    }
    return m_eglSurface;
}
#endif // QT_OPENGL_ES_2

QByteArray QWindowsWindow::debugWindowFlags(Qt::WindowFlags wf)
{
    const int iwf = int(wf);
    QByteArray rc = "0x";
    rc += QByteArray::number(iwf, 16);
    rc += " [";

    switch ((iwf & Qt::WindowType_Mask)) {
    case Qt::Widget:
        rc += " Widget";
        break;
    case Qt::Window:
        rc += " Window";
        break;
    case Qt::Dialog:
        rc += " Dialog";
        break;
    case Qt::Sheet:
        rc += " Sheet";
        break;
    case Qt::Popup:
        rc += " Popup";
        break;
    case Qt::Tool:
        rc += " Tool";
        break;
    case Qt::ToolTip:
        rc += " ToolTip";
        break;
    case Qt::SplashScreen:
        rc += " SplashScreen";
        break;
    case Qt::Desktop:
        rc += " Desktop";
        break;
    case Qt::SubWindow:
        rc += " SubWindow";
        break;
    }
    if (iwf & Qt::MSWindowsFixedSizeDialogHint) rc += " MSWindowsFixedSizeDialogHint";
    if (iwf & Qt::MSWindowsOwnDC) rc += " MSWindowsOwnDC";
    if (iwf & Qt::FramelessWindowHint) rc += " FramelessWindowHint";
    if (iwf & Qt::WindowTitleHint) rc += " WindowTitleHint";
    if (iwf & Qt::WindowSystemMenuHint) rc += " WindowSystemMenuHint";
    if (iwf & Qt::WindowMinimizeButtonHint) rc += " WindowMinimizeButtonHint";
    if (iwf & Qt::WindowMaximizeButtonHint) rc += " WindowMaximizeButtonHint";
    if (iwf & Qt::WindowContextHelpButtonHint) rc += " WindowContextHelpButtonHint";
    if (iwf & Qt::WindowShadeButtonHint) rc += " WindowShadeButtonHint";
    if (iwf & Qt::WindowStaysOnTopHint) rc += " WindowStaysOnTopHint";
    if (iwf & Qt::CustomizeWindowHint) rc += " CustomizeWindowHint";
    if (iwf & Qt::WindowStaysOnBottomHint) rc += " WindowStaysOnBottomHint";
    if (iwf & Qt::WindowCloseButtonHint) rc += " WindowCloseButtonHint";
    rc += ']';
    return rc;
}

static HICON createHIcon(const QIcon &icon, int xSize, int ySize)
{
    if (!icon.isNull()) {
        const QPixmap pm = icon.pixmap(icon.actualSize(QSize(xSize, ySize)));
        if (!pm.isNull())
            return qt_pixmapToWinHICON(pm);
    }
    return 0;
}

void QWindowsWindow::setWindowIcon(const QIcon &icon)
{
    if (m_data.hwnd) {
        destroyIcon();

        m_iconSmall = createHIcon(icon, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
        m_iconBig = createHIcon(icon, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));

        if (m_iconBig) {
            SendMessage(m_data.hwnd, WM_SETICON, 0 /* ICON_SMALL */, (LPARAM)m_iconSmall);
            SendMessage(m_data.hwnd, WM_SETICON, 1 /* ICON_BIG */, (LPARAM)m_iconBig);
        } else {
            SendMessage(m_data.hwnd, WM_SETICON, 0 /* ICON_SMALL */, (LPARAM)m_iconSmall);
            SendMessage(m_data.hwnd, WM_SETICON, 1 /* ICON_BIG */, (LPARAM)m_iconSmall);
        }
    }
}

/*!
    \brief Sets custom margins to be added to the default margins determined by
    the windows style in the handling of the WM_NCCALCSIZE message.

    This is currently used to give the Aero-style QWizard a smaller top margin.
    The property can be set using QPlatformNativeInterface::setWindowProperty() or,
    before platform window creation, by setting a dynamic property
    on the QWindow (see QWindowsIntegration::createPlatformWindow()).
*/

void QWindowsWindow::setCustomMargins(const QMargins &newCustomMargins)
{
    if (newCustomMargins != m_data.customMargins) {
        const QMargins oldCustomMargins = m_data.customMargins;
        m_data.customMargins = newCustomMargins;
         // Re-trigger WM_NCALCSIZE with wParam=1 by passing SWP_FRAMECHANGED
        const QRect currentFrameGeometry = frameGeometry_sys();
        const QPoint topLeft = currentFrameGeometry.topLeft();
        QRect newFrame = currentFrameGeometry.marginsRemoved(oldCustomMargins) + m_data.customMargins;
        newFrame.moveTo(topLeft);
        setFlag(FrameDirty);
        qCDebug(lcQpaWindows) << __FUNCTION__ << oldCustomMargins << "->" << newCustomMargins
            << currentFrameGeometry << "->" << newFrame;
        SetWindowPos(m_data.hwnd, 0, newFrame.x(), newFrame.y(), newFrame.width(), newFrame.height(), SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

QT_END_NAMESPACE
