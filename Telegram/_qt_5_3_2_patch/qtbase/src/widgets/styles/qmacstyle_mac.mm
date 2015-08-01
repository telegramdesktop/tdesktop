/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtWidgets module of the Qt Toolkit.
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

/*
  Note: The qdoc comments for QMacStyle are contained in
  .../doc/src/qstyles.qdoc.
*/

#include <Cocoa/Cocoa.h>

#include "qmacstyle_mac_p.h"
#include "qmacstyle_mac_p_p.h"

#define QMAC_QAQUASTYLE_SIZE_CONSTRAIN
//#define DEBUG_SIZE_CONSTRAINT

#include <private/qcore_mac_p.h>
#include <private/qcombobox_p.h>
#include <private/qpainter_p.h>
#include <qapplication.h>
#include <qbitmap.h>
#include <qcheckbox.h>
#include <qcombobox.h>
#include <qdialogbuttonbox.h>
#include <qdockwidget.h>
#include <qevent.h>
#include <qfocusframe.h>
#include <qformlayout.h>
#include <qgroupbox.h>
#include <qhash.h>
#include <qheaderview.h>
#include <qlineedit.h>
#include <qmainwindow.h>
#include <qmdisubwindow.h>
#include <qmenubar.h>
#include <qpaintdevice.h>
#include <qpainter.h>
#include <qpixmapcache.h>
#include <qpointer.h>
#include <qprogressbar.h>
#include <qpushbutton.h>
#include <qradiobutton.h>
#include <qrubberband.h>
#include <qscrollbar.h>
#include <qsizegrip.h>
#include <qstyleoption.h>
#include <qtoolbar.h>
#include <qtoolbutton.h>
#include <qtreeview.h>
#include <qtableview.h>
#include <qwizard.h>
#include <qdebug.h>
#include <qlibrary.h>
#include <qdatetimeedit.h>
#include <qmath.h>
#include <QtWidgets/qgraphicsproxywidget.h>
#include <QtWidgets/qgraphicsview.h>
#include <private/qstylehelper_p.h>
#include <private/qstyleanimation_p.h>
#include <qpa/qplatformfontdatabase.h>
#include <qpa/qplatformtheme.h>

QT_USE_NAMESPACE

@interface QT_MANGLE_NAMESPACE(NotificationReceiver) : NSObject {
QMacStylePrivate *mPrivate;
}
- (id)initWithPrivate:(QMacStylePrivate *)priv;
- (void)scrollBarStyleDidChange:(NSNotification *)notification;
@end

QT_NAMESPACE_ALIAS_OBJC_CLASS(NotificationReceiver);

@implementation NotificationReceiver
- (id)initWithPrivate:(QMacStylePrivate *)priv
{
    self = [super init];
    mPrivate = priv;
    return self;
}

- (void)scrollBarStyleDidChange:(NSNotification *)notification
{
    Q_UNUSED(notification);
    QEvent event(QEvent::StyleChange);
    QMutableSetIterator<QPointer<QObject> > it(QMacStylePrivate::scrollBars);
    while (it.hasNext()) {
        if (!it.next())
            it.remove();
        else
            QCoreApplication::sendEvent(it.value(), &event);
    }
}
@end

QT_BEGIN_NAMESPACE

// The following constants are used for adjusting the size
// of push buttons so that they are drawn inside their bounds.
const int QMacStylePrivate::PushButtonLeftOffset = 6;
const int QMacStylePrivate::PushButtonTopOffset = 4;
const int QMacStylePrivate::PushButtonRightOffset = 12;
const int QMacStylePrivate::PushButtonBottomOffset = 12;
const int QMacStylePrivate::MiniButtonH = 26;
const int QMacStylePrivate::SmallButtonH = 30;
const int QMacStylePrivate::BevelButtonW = 50;
const int QMacStylePrivate::BevelButtonH = 22;
const int QMacStylePrivate::PushButtonContentPadding = 6;

QSet<QPointer<QObject> > QMacStylePrivate::scrollBars;

static uint qHash(const QPointer<QObject> &ptr)
{
    return qHash(ptr.data());
}

// These colors specify the titlebar gradient colors on
// Leopard. Ideally we should get them from the system.
static const QColor titlebarGradientActiveBegin(220, 220, 220);
static const QColor titlebarGradientActiveEnd(151, 151, 151);
static const QColor titlebarSeparatorLineActive(111, 111, 111);
static const QColor titlebarGradientInactiveBegin(241, 241, 241);
static const QColor titlebarGradientInactiveEnd(207, 207, 207);
static const QColor titlebarSeparatorLineInactive(131, 131, 131);

// Gradient colors used for the dock widget title bar and
// non-unifed tool bar bacground.
static const QColor mainWindowGradientBegin(240, 240, 240);
static const QColor mainWindowGradientEnd(200, 200, 200);

static const int DisclosureOffset = 4;

// Resolve these at run-time, since the functions was moved in Leopard.
typedef HIRect * (*PtrHIShapeGetBounds)(HIShapeRef, HIRect *);
static PtrHIShapeGetBounds ptrHIShapeGetBounds = 0;

static int closeButtonSize = 12;
static bool isVerticalTabs(const QTabBar::Shape shape) {
    return (shape == QTabBar::RoundedEast
                || shape == QTabBar::TriangularEast
                || shape == QTabBar::RoundedWest
                || shape == QTabBar::TriangularWest);
}

static bool isInMacUnifiedToolbarArea(QWindow *window, int windowY)
{
    QPlatformNativeInterface *nativeInterface = QGuiApplication::platformNativeInterface();
    QPlatformNativeInterface::NativeResourceForIntegrationFunction function =
        nativeInterface->nativeResourceFunctionForIntegration("testContentBorderPosition");
    if (!function)
        return false; // Not Cocoa platform plugin.

    typedef bool (*TestContentBorderPositionFunction)(QWindow *, int);
    return (reinterpret_cast<TestContentBorderPositionFunction>(function))(window, windowY);
}


void drawTabCloseButton(QPainter *p, bool hover, bool active, bool selected)
{
    // draw background circle
    p->setRenderHints(QPainter::Antialiasing);
    QRect rect(0, 0, closeButtonSize, closeButtonSize);
    QColor background;
    if (hover) {
        background = QColor(124, 124, 124);
    } else {
        if (active) {
            if (selected)
                background = QColor(104, 104, 104);
            else
                background = QColor(83, 83, 83);
        } else {
            if (selected)
                background = QColor(144, 144, 144);
            else
                background = QColor(114, 114, 114);
        }
    }
    p->setPen(Qt::transparent);
    p->setBrush(background);
    p->drawEllipse(rect);

    // draw cross
    int min = 3;
    int max = 9;
    QPen crossPen;
    crossPen.setColor(QColor(194, 194, 194));
    crossPen.setWidthF(1.3);
    crossPen.setCapStyle(Qt::FlatCap);
    p->setPen(crossPen);
    p->drawLine(min, min, max, max);
    p->drawLine(min, max, max, min);
}

QRect rotateTabPainter(QPainter *p, QTabBar::Shape shape, QRect tabRect)
{
    if (isVerticalTabs(shape)) {
        int newX, newY, newRot;
        if (shape == QTabBar::RoundedEast
            || shape == QTabBar::TriangularEast) {
            newX = tabRect.width();
            newY = tabRect.y();
            newRot = 90;
        } else {
            newX = 0;
            newY = tabRect.y() + tabRect.height();
            newRot = -90;
        }
        tabRect.setRect(0, 0, tabRect.height(), tabRect.width());
        QMatrix m;
        m.translate(newX, newY);
        m.rotate(newRot);
        p->setMatrix(m, true);
    }
    return tabRect;
}

void drawTabShape(QPainter *p, const QStyleOptionTabV3 *tabOpt, bool isUnified)
{
    QRect r = tabOpt->rect;
    p->translate(tabOpt->rect.x(), tabOpt->rect.y());
    r.moveLeft(0);
    r.moveTop(0);
    QRect tabRect = rotateTabPainter(p, tabOpt->shape, r);

    int width = tabRect.width();
    int height = 20;
    bool active = (tabOpt->state & QStyle::State_Active);
    bool selected = (tabOpt->state & QStyle::State_Selected);

    if (selected) {
        QRect rect(1, 0, width - 2, height);

        // fill body
        if (tabOpt->documentMode && isUnified) {
            p->save();
            p->setCompositionMode(QPainter::CompositionMode_Source);
            p->fillRect(rect, QColor(Qt::transparent));
            p->restore();
        } else if (active) {
            int d = (QSysInfo::MacintoshVersion >= QSysInfo::MV_10_6) ? 16 : 0;
            p->fillRect(rect, QColor(151 + d, 151 + d, 151 + d));
        } else {
            int d = (QSysInfo::MacintoshVersion >= QSysInfo::MV_10_6) ? 9 : 0;
            QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
            gradient.setColorAt(0, QColor(207 + d, 207 + d, 207 + d));
            gradient.setColorAt(0.5, QColor(206 + d, 206 + d, 206 + d));
            gradient.setColorAt(1, QColor(201 + d, 201 + d, 201 + d));
            p->fillRect(rect, gradient);
        }

        // draw border
        QColor borderSides;
        QColor borderBottom;
        if (active) {
            borderSides = QColor(88, 88, 88);
            borderBottom = QColor(88, 88, 88);
        } else {
            borderSides = QColor(121, 121, 121);
            borderBottom = QColor(116, 116, 116);
        }

        p->setPen(borderSides);

        int bottom = height;
        // left line
        p->drawLine(0, 1, 0, bottom-2);
        // right line
        p->drawLine(width-1, 1, width-1, bottom-2);

        // bottom line
        if (active) {
            p->setPen(QColor(168, 168, 168));
            p->drawLine(3, bottom-1, width-3, bottom-1);
        }
        p->setPen(borderBottom);
        p->drawLine(2, bottom, width-2, bottom);

        int w = 3;
        QRectF rectangleLeft(1, height - w, w, w);
        QRectF rectangleRight(width - 2, height - 1, w, w);
        int startAngle = 180 * 16;
        int spanAngle = 90 * 16;
        p->setRenderHint(QPainter::Antialiasing);
        p->drawArc(rectangleLeft, startAngle, spanAngle);
        p->drawArc(rectangleRight, startAngle, -spanAngle);
    } else {
        // when the mouse is over non selected tabs they get a new color
        bool hover = (tabOpt->state & QStyle::State_MouseOver);
        if (hover) {
            QRect rect(1, 2, width - 1, height - 1);
            p->fillRect(rect, QColor(110, 110, 110));
        }

        // seperator lines between tabs
        bool west = (tabOpt->shape == QTabBar::RoundedWest || tabOpt->shape == QTabBar::TriangularWest);
        bool drawOnRight = !west;
        if ((!drawOnRight && tabOpt->selectedPosition != QStyleOptionTab::NextIsSelected)
            || (drawOnRight && tabOpt->selectedPosition != QStyleOptionTab::NextIsSelected)) {
            QColor borderColor;
            QColor borderHighlightColor;
            if (active) {
                borderColor = QColor(64, 64, 64);
                borderHighlightColor = QColor(140, 140, 140);
            } else {
                borderColor = QColor(135, 135, 135);
                borderHighlightColor = QColor(178, 178, 178);
            }

            int x = drawOnRight ? width : 0;

            // tab seperator line
            p->setPen(borderColor);
            p->drawLine(x, 2, x, height + 1);

            // tab seperator highlight
            p->setPen(borderHighlightColor);
            p->drawLine(x-1, 2, x-1, height + 1);
            p->drawLine(x+1, 2, x+1, height + 1);
        }
    }
}

void drawTabBase(QPainter *p, const QStyleOptionTabBarBaseV2 *tbb, const QWidget *w)
{
    QRect r = tbb->rect;
    if (isVerticalTabs(tbb->shape)) {
        r.setWidth(w->width());
    } else {
        r.setHeight(w->height());
    }
    QRect tabRect = rotateTabPainter(p, tbb->shape, r);
    int width = tabRect.width();
    int height = tabRect.height();
    bool active = (tbb->state & QStyle::State_Active);

    // top border lines
    QColor borderHighlightTop;
    QColor borderTop;
    if (active) {
        borderTop = QColor(64, 64, 64);
        borderHighlightTop = QColor(174, 174, 174);
    } else {
        borderTop = QColor(135, 135, 135);
        borderHighlightTop = QColor(207, 207, 207);
    }
    p->setPen(borderHighlightTop);
    p->drawLine(tabRect.x(), 0, width, 0);
    p->setPen(borderTop);
    p->drawLine(tabRect.x(), 1, width, 1);

    // center block
    QRect centralRect(tabRect.x(), 2, width, height - 2);
    if (active) {
        QColor mainColor = QColor(120, 120, 120);
        p->fillRect(centralRect, mainColor);
    } else {
        QLinearGradient gradient(centralRect.topLeft(), centralRect.bottomLeft());
        gradient.setColorAt(0, QColor(165, 165, 165));
        gradient.setColorAt(0.5, QColor(164, 164, 164));
        gradient.setColorAt(1, QColor(158, 158, 158));
        p->fillRect(centralRect, gradient);
    }

    // bottom border lines
    QColor borderHighlightBottom;
    QColor borderBottom;
    if (active) {
        borderHighlightBottom = QColor(153, 153, 153);
        borderBottom = QColor(64, 64, 64);
    } else {
        borderHighlightBottom = QColor(177, 177, 177);
        borderBottom = QColor(127, 127, 127);
    }
    p->setPen(borderHighlightBottom);
    p->drawLine(tabRect.x(), height - 2, width, height - 2);
    p->setPen(borderBottom);
    p->drawLine(tabRect.x(), height - 1, width, height - 1);
}

static int getControlSize(const QStyleOption *option, const QWidget *widget)
{
    switch (QMacStyle::widgetSizePolicy(widget, option)) {
    case QMacStyle::SizeSmall:
        return QAquaSizeSmall;
    case QMacStyle::SizeMini:
        return QAquaSizeMini;
    default:
        break;
    }
    return QAquaSizeLarge;
}


static inline bool isTreeView(const QWidget *widget)
{
    return (widget && widget->parentWidget() &&
            (qobject_cast<const QTreeView *>(widget->parentWidget())
             ));
}

static inline ThemeTabDirection getTabDirection(QTabBar::Shape shape)
{
    ThemeTabDirection ttd;
    switch (shape) {
    case QTabBar::RoundedSouth:
    case QTabBar::TriangularSouth:
        ttd = kThemeTabSouth;
        break;
    default:  // Added to remove the warning, since all values are taken care of, really!
    case QTabBar::RoundedNorth:
    case QTabBar::TriangularNorth:
        ttd = kThemeTabNorth;
        break;
    case QTabBar::RoundedWest:
    case QTabBar::TriangularWest:
        ttd = kThemeTabWest;
        break;
    case QTabBar::RoundedEast:
    case QTabBar::TriangularEast:
        ttd = kThemeTabEast;
        break;
    }
    return ttd;
}

static QString qt_mac_removeMnemonics(const QString &original)
{
    QString returnText(original.size(), 0);
    int finalDest = 0;
    int currPos = 0;
    int l = original.length();
    while (l) {
        if (original.at(currPos) == QLatin1Char('&')
            && (l == 1 || original.at(currPos + 1) != QLatin1Char('&'))) {
            ++currPos;
            --l;
            if (l == 0)
                break;
        }
        returnText[finalDest] = original.at(currPos);
        ++currPos;
        ++finalDest;
        --l;
    }
    returnText.truncate(finalDest);
    return returnText;
}

static CGContextRef qt_mac_cg_context(const QPaintDevice *pdev);

namespace {
class QMacCGContext
{
    CGContextRef context;
public:
    QMacCGContext(QPainter *p);
    inline QMacCGContext() { context = 0; }
    inline QMacCGContext(const QPaintDevice *pdev) {
        context = qt_mac_cg_context(pdev);
    }
    inline QMacCGContext(CGContextRef cg, bool takeOwnership=false) {
        context = cg;
        if (!takeOwnership)
            CGContextRetain(context);
    }
    inline QMacCGContext(const QMacCGContext &copy) : context(0) { *this = copy; }
    inline ~QMacCGContext() {
        if (context)
            CGContextRelease(context);
    }
    inline bool isNull() const { return context; }
    inline operator CGContextRef() { return context; }
    inline QMacCGContext &operator=(const QMacCGContext &copy) {
        if (context)
            CGContextRelease(context);
        context = copy.context;
        CGContextRetain(context);
        return *this;
    }
    inline QMacCGContext &operator=(CGContextRef cg) {
        if (context)
            CGContextRelease(context);
        context = cg;
        CGContextRetain(context); //we do not take ownership
        return *this;
    }
};
} // anonymous namespace

static QColor qcolorFromCGColor(CGColorRef cgcolor)
{
    QColor pc;
    CGColorSpaceModel model = CGColorSpaceGetModel(CGColorGetColorSpace(cgcolor));
    const CGFloat *components = CGColorGetComponents(cgcolor);
    if (model == kCGColorSpaceModelRGB) {
        pc.setRgbF(components[0], components[1], components[2], components[3]);
    } else if (model == kCGColorSpaceModelCMYK) {
        pc.setCmykF(components[0], components[1], components[2], components[3]);
    } else if (model == kCGColorSpaceModelMonochrome) {
        pc.setRgbF(components[0], components[0], components[0], components[1]);
    } else {
        // Colorspace we can't deal with.
        qWarning("Qt: qcolorFromCGColor: cannot convert from colorspace model: %d", model);
        Q_ASSERT(false);
    }
    return pc;
}

OSStatus qt_mac_shape2QRegionHelper(int inMessage, HIShapeRef, const CGRect *inRect, void *inRefcon)
{
    QRegion *region = static_cast<QRegion *>(inRefcon);
    if (!region)
        return paramErr;

    switch (inMessage) {
    case kHIShapeEnumerateRect:
        *region += QRect(inRect->origin.x, inRect->origin.y,
                         inRect->size.width, inRect->size.height);
        break;
    case kHIShapeEnumerateInit:
        // Assume the region is already setup correctly
    case kHIShapeEnumerateTerminate:
    default:
        break;
    }
    return noErr;
}


/*!
    \internal
     Create's a mutable shape, it's the caller's responsibility to release.
     WARNING: this function clamps the coordinates to SHRT_MIN/MAX on 10.4 and below.
*/
HIMutableShapeRef qt_mac_toHIMutableShape(const QRegion &region)
{
    HIMutableShapeRef shape = HIShapeCreateMutable();
    if (region.rectCount() < 2 ) {
        QRect qtRect = region.boundingRect();
        CGRect cgRect = CGRectMake(qtRect.x(), qtRect.y(), qtRect.width(), qtRect.height());
        HIShapeUnionWithRect(shape, &cgRect);
    } else {
        foreach (const QRect &qtRect, region.rects()) {
            CGRect cgRect = CGRectMake(qtRect.x(), qtRect.y(), qtRect.width(), qtRect.height());
            HIShapeUnionWithRect(shape, &cgRect);
        }
    }
    return shape;
}

QRegion qt_mac_fromHIShapeRef(HIShapeRef shape)
{
    QRegion returnRegion;
    //returnRegion.detach();
    HIShapeEnumerate(shape, kHIShapeParseFromTopLeft, qt_mac_shape2QRegionHelper, &returnRegion);
    return returnRegion;
}

CGColorSpaceRef m_genericColorSpace = 0;
static QHash<CGDirectDisplayID, CGColorSpaceRef> m_displayColorSpaceHash;
bool m_postRoutineRegistered = false;

static CGColorSpaceRef qt_mac_displayColorSpace(const QWidget *widget);
static CGColorSpaceRef qt_mac_genericColorSpace()
{
#if 0
    if (!m_genericColorSpace) {
        if (QSysInfo::MacintoshVersion >= QSysInfo::MV_10_4) {
            m_genericColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
        } else
        {
            m_genericColorSpace = CGColorSpaceCreateDeviceRGB();
        }
        if (!m_postRoutineRegistered) {
            m_postRoutineRegistered = true;
            qAddPostRoutine(QCoreGraphicsPaintEngine::cleanUpMacColorSpaces);
        }
    }
    return m_genericColorSpace;
#else
    // Just return the main display colorspace for the moment.
    return qt_mac_displayColorSpace(0);
#endif
}

static void qt_mac_cleanUpMacColorSpaces()
{
    if (m_genericColorSpace) {
        CFRelease(m_genericColorSpace);
        m_genericColorSpace = 0;
    }
    QHash<CGDirectDisplayID, CGColorSpaceRef>::const_iterator it = m_displayColorSpaceHash.constBegin();
    while (it != m_displayColorSpaceHash.constEnd()) {
        if (it.value())
            CFRelease(it.value());
        ++it;
    }
    m_displayColorSpaceHash.clear();
}

/*
    Ideally, we should pass the widget in here, and use CGGetDisplaysWithRect() etc.
    to support multiple displays correctly.
*/
static CGColorSpaceRef qt_mac_displayColorSpace(const QWidget *widget)
{
    CGColorSpaceRef colorSpace;

    CGDirectDisplayID displayID;
    if (widget == 0) {
        displayID = CGMainDisplayID();
    } else {
        displayID = CGMainDisplayID();
        /*
        ### get correct display
        const QRect &qrect = widget->window()->geometry();
        CGRect rect = CGRectMake(qrect.x(), qrect.y(), qrect.width(), qrect.height());
        CGDisplayCount throwAway;
        CGDisplayErr dErr = CGGetDisplaysWithRect(rect, 1, &displayID, &throwAway);
        if (dErr != kCGErrorSuccess)
            return macDisplayColorSpace(0); // fall back on main display
        */
    }
    if ((colorSpace = m_displayColorSpaceHash.value(displayID)))
        return colorSpace;

    colorSpace = CGDisplayCopyColorSpace(displayID);
    if (colorSpace == 0)
        colorSpace = CGColorSpaceCreateDeviceRGB();

    m_displayColorSpaceHash.insert(displayID, colorSpace);
    if (!m_postRoutineRegistered) {
        m_postRoutineRegistered = true;
        qAddPostRoutine(qt_mac_cleanUpMacColorSpaces);
    }
    return colorSpace;
}

bool qt_macWindowIsTextured(const QWidget *window)
{
    if (QWindow *w = window->windowHandle())
        if (w->handle())
            if (NSWindow *nswindow = static_cast<NSWindow*>(QGuiApplication::platformNativeInterface()->nativeResourceForWindow(QByteArrayLiteral("NSWindow"), w)))
                return ([nswindow styleMask] & NSTexturedBackgroundWindowMask) ? true : false;
    return false;
}

static bool qt_macWindowMainWindow(const QWidget *window)
{
    if (QWindow *w = window->windowHandle()) {
        if (w->handle()) {
            if (NSWindow *nswindow = static_cast<NSWindow*>(QGuiApplication::platformNativeInterface()->nativeResourceForWindow(QByteArrayLiteral("nswindow"), w))) {
                return [nswindow isMainWindow];
            }
        }
    }
    return false;
}

/*****************************************************************************
  QMacCGStyle globals
 *****************************************************************************/
const int qt_mac_hitheme_version = 0; //the HITheme version we speak
const int macItemFrame         = 2;    // menu item frame width
const int macItemHMargin       = 3;    // menu item hor text margin
const int macItemVMargin       = 2;    // menu item ver text margin
const int macRightBorder       = 12;   // right border on mac
const ThemeWindowType QtWinType = kThemeDocumentWindow; // Window type we use for QTitleBar.
QPixmap *qt_mac_backgroundPattern = 0; // stores the standard widget background.

/*****************************************************************************
  QMacCGStyle utility functions
 *****************************************************************************/
static inline int qt_mac_hitheme_tab_version()
{
    return 1;
}

static inline HIRect qt_hirectForQRect(const QRect &convertRect, const QRect &rect = QRect())
{
    return CGRectMake(convertRect.x() + rect.x(), convertRect.y() + rect.y(),
                      convertRect.width() - rect.width(), convertRect.height() - rect.height());
}

static inline const QRect qt_qrectForHIRect(const HIRect &hirect)
{
    return QRect(QPoint(int(hirect.origin.x), int(hirect.origin.y)),
                 QSize(int(hirect.size.width), int(hirect.size.height)));
}

inline bool qt_mac_is_metal(const QWidget *w)
{
    for (; w; w = w->parentWidget()) {
        if (w->testAttribute(Qt::WA_MacBrushedMetal))
            return true;
        if (w->isWindow() && w->testAttribute(Qt::WA_WState_Created)) {  // If not created will fall through to the opaque check and be fine anyway.
            return qt_macWindowIsTextured(w);
        }
        if (w->d_func()->isOpaque)
            break;
    }
    return false;
}

static int qt_mac_aqua_get_metric(ThemeMetric met)
{
    SInt32 ret;
    GetThemeMetric(met, &ret);
    return ret;
}

static QSize qt_aqua_get_known_size(QStyle::ContentsType ct, const QWidget *widg, QSize szHint,
                                    QAquaWidgetSize sz)
{
    QSize ret(-1, -1);
    if (sz != QAquaSizeSmall && sz != QAquaSizeLarge && sz != QAquaSizeMini) {
        qDebug("Not sure how to return this...");
        return ret;
    }
    if ((widg && widg->testAttribute(Qt::WA_SetFont)) || !QApplication::desktopSettingsAware()) {
        // If you're using a custom font and it's bigger than the default font,
        // then no constraints for you. If you are smaller, we can try to help you out
        QFont font = qt_app_fonts_hash()->value(widg->metaObject()->className(), QFont());
        if (widg->font().pointSize() > font.pointSize())
            return ret;
    }

    if (ct == QStyle::CT_CustomBase && widg) {
        if (qobject_cast<const QPushButton *>(widg))
            ct = QStyle::CT_PushButton;
        else if (qobject_cast<const QRadioButton *>(widg))
            ct = QStyle::CT_RadioButton;
        else if (qobject_cast<const QCheckBox *>(widg))
            ct = QStyle::CT_CheckBox;
        else if (qobject_cast<const QComboBox *>(widg))
            ct = QStyle::CT_ComboBox;
        else if (qobject_cast<const QToolButton *>(widg))
            ct = QStyle::CT_ToolButton;
        else if (qobject_cast<const QSlider *>(widg))
            ct = QStyle::CT_Slider;
        else if (qobject_cast<const QProgressBar *>(widg))
            ct = QStyle::CT_ProgressBar;
        else if (qobject_cast<const QLineEdit *>(widg))
            ct = QStyle::CT_LineEdit;
        else if (qobject_cast<const QHeaderView *>(widg))
            ct = QStyle::CT_HeaderSection;
        else if (qobject_cast<const QMenuBar *>(widg))
            ct = QStyle::CT_MenuBar;
        else if (qobject_cast<const QSizeGrip *>(widg))
            ct = QStyle::CT_SizeGrip;
        else
            return ret;
    }

    switch (ct) {
    case QStyle::CT_PushButton: {
        const QPushButton *psh = qobject_cast<const QPushButton *>(widg);
        // If this comparison is false, then the widget was not a push button.
        // This is bad and there's very little we can do since we were requested to find a
        // sensible size for a widget that pretends to be a QPushButton but is not.
        if(psh) {
            QString buttonText = qt_mac_removeMnemonics(psh->text());
            if (buttonText.contains(QLatin1Char('\n')))
                ret = QSize(-1, -1);
            else if (sz == QAquaSizeLarge)
                ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricPushButtonHeight));
            else if (sz == QAquaSizeSmall)
                ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricSmallPushButtonHeight));
            else if (sz == QAquaSizeMini)
                ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricMiniPushButtonHeight));

            if (!psh->icon().isNull()){
                // If the button got an icon, and the icon is larger than the
                // button, we can't decide on a default size
                ret.setWidth(-1);
                if (ret.height() < psh->iconSize().height())
                    ret.setHeight(-1);
            }
            else if (buttonText == QLatin1String("OK") || buttonText == QLatin1String("Cancel")){
                // Aqua Style guidelines restrict the size of OK and Cancel buttons to 68 pixels.
                // However, this doesn't work for German, therefore only do it for English,
                // I suppose it would be better to do some sort of lookups for languages
                // that like to have really long words.
                ret.setWidth(77 - 8);
            }
        } else {
            // The only sensible thing to do is to return whatever the style suggests...
            if (sz == QAquaSizeLarge)
                ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricPushButtonHeight));
            else if (sz == QAquaSizeSmall)
                ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricSmallPushButtonHeight));
            else if (sz == QAquaSizeMini)
                ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricMiniPushButtonHeight));
            else
                // Since there's no default size we return the large size...
                ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricPushButtonHeight));
         }
#if 0 //Not sure we are applying the rules correctly for RadioButtons/CheckBoxes --Sam
    } else if (ct == QStyle::CT_RadioButton) {
        QRadioButton *rdo = static_cast<QRadioButton *>(widg);
        // Exception for case where multiline radio button text requires no size constrainment
        if (rdo->text().find('\n') != -1)
            return ret;
        if (sz == QAquaSizeLarge)
            ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricRadioButtonHeight));
        else if (sz == QAquaSizeSmall)
            ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricSmallRadioButtonHeight));
        else if (sz == QAquaSizeMini)
            ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricMiniRadioButtonHeight));
    } else if (ct == QStyle::CT_CheckBox) {
        if (sz == QAquaSizeLarge)
            ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricCheckBoxHeight));
        else if (sz == QAquaSizeSmall)
            ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricSmallCheckBoxHeight));
        else if (sz == QAquaSizeMini)
            ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricMiniCheckBoxHeight));
#endif
        break;
    }
    case QStyle::CT_SizeGrip:
        if (sz == QAquaSizeLarge || sz == QAquaSizeSmall) {
            HIRect r;
            HIPoint p = { 0, 0 };
            HIThemeGrowBoxDrawInfo gbi;
            gbi.version = 0;
            gbi.state = kThemeStateActive;
            gbi.kind = kHIThemeGrowBoxKindNormal;
            gbi.direction = QApplication::isRightToLeft() ? kThemeGrowLeft | kThemeGrowDown
                                                          : kThemeGrowRight | kThemeGrowDown;
            gbi.size = sz == QAquaSizeSmall ? kHIThemeGrowBoxSizeSmall : kHIThemeGrowBoxSizeNormal;
            if (HIThemeGetGrowBoxBounds(&p, &gbi, &r) == noErr) {
                int width = 0;
                // Snow Leopard and older get a size grip, as well as QMdiSubWindows.
                if (QSysInfo::MacintoshVersion <= QSysInfo::MV_10_6
#ifndef QT_NO_MDIAREA
                    || (widg && widg->parentWidget() && qobject_cast<QMdiSubWindow *>(widg->parentWidget()))
#endif
                   )
                    width = r.size.width;
                ret = QSize(width, r.size.height);
            }
        }
        break;
    case QStyle::CT_ComboBox:
        switch (sz) {
        case QAquaSizeLarge:
            ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricPopupButtonHeight));
            break;
        case QAquaSizeSmall:
            ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricSmallPopupButtonHeight));
            break;
        case QAquaSizeMini:
            ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricMiniPopupButtonHeight));
            break;
        default:
            break;
        }
        break;
    case QStyle::CT_ToolButton:
        if (sz == QAquaSizeSmall) {
            int width = 0, height = 0;
            if (szHint == QSize(-1, -1)) { //just 'guess'..
                const QToolButton *bt = qobject_cast<const QToolButton *>(widg);
                // If this conversion fails then the widget was not what it claimed to be.
                if(bt) {
                    if (!bt->icon().isNull()) {
                        QSize iconSize = bt->iconSize();
                        QSize pmSize = bt->icon().actualSize(QSize(32, 32), QIcon::Normal);
                        width = qMax(width, qMax(iconSize.width(), pmSize.width()));
                        height = qMax(height, qMax(iconSize.height(), pmSize.height()));
                    }
                    if (!bt->text().isNull() && bt->toolButtonStyle() != Qt::ToolButtonIconOnly) {
                        int text_width = bt->fontMetrics().width(bt->text()),
                           text_height = bt->fontMetrics().height();
                        if (bt->toolButtonStyle() == Qt::ToolButtonTextUnderIcon) {
                            width = qMax(width, text_width);
                            height += text_height;
                        } else {
                            width += text_width;
                            width = qMax(height, text_height);
                        }
                    }
                } else {
                    // Let's return the size hint...
                    width = szHint.width();
                    height = szHint.height();
                }
            } else {
                width = szHint.width();
                height = szHint.height();
            }
            width =  qMax(20, width +  5); //border
            height = qMax(20, height + 5); //border
            ret = QSize(width, height);
        }
        break;
    case QStyle::CT_Slider: {
        int w = -1;
        const QSlider *sld = qobject_cast<const QSlider *>(widg);
        // If this conversion fails then the widget was not what it claimed to be.
        if(sld) {
            if (sz == QAquaSizeLarge) {
                if (sld->orientation() == Qt::Horizontal) {
                    w = qt_mac_aqua_get_metric(kThemeMetricHSliderHeight);
                    if (sld->tickPosition() != QSlider::NoTicks)
                        w += qt_mac_aqua_get_metric(kThemeMetricHSliderTickHeight);
                } else {
                    w = qt_mac_aqua_get_metric(kThemeMetricVSliderWidth);
                    if (sld->tickPosition() != QSlider::NoTicks)
                        w += qt_mac_aqua_get_metric(kThemeMetricVSliderTickWidth);
                }
            } else if (sz == QAquaSizeSmall) {
                if (sld->orientation() == Qt::Horizontal) {
                    w = qt_mac_aqua_get_metric(kThemeMetricSmallHSliderHeight);
                    if (sld->tickPosition() != QSlider::NoTicks)
                        w += qt_mac_aqua_get_metric(kThemeMetricSmallHSliderTickHeight);
                } else {
                    w = qt_mac_aqua_get_metric(kThemeMetricSmallVSliderWidth);
                    if (sld->tickPosition() != QSlider::NoTicks)
                        w += qt_mac_aqua_get_metric(kThemeMetricSmallVSliderTickWidth);
                }
            } else if (sz == QAquaSizeMini) {
                if (sld->orientation() == Qt::Horizontal) {
                    w = qt_mac_aqua_get_metric(kThemeMetricMiniHSliderHeight);
                    if (sld->tickPosition() != QSlider::NoTicks)
                        w += qt_mac_aqua_get_metric(kThemeMetricMiniHSliderTickHeight);
                } else {
                    w = qt_mac_aqua_get_metric(kThemeMetricMiniVSliderWidth);
                    if (sld->tickPosition() != QSlider::NoTicks)
                        w += qt_mac_aqua_get_metric(kThemeMetricMiniVSliderTickWidth);
                }
            }
        } else {
            // This is tricky, we were requested to find a size for a slider which is not
            // a slider. We don't know if this is vertical or horizontal or if we need to
            // have tick marks or not.
            // For this case we will return an horizontal slider without tick marks.
            w = qt_mac_aqua_get_metric(kThemeMetricHSliderHeight);
            w += qt_mac_aqua_get_metric(kThemeMetricHSliderTickHeight);
        }
        if (sld->orientation() == Qt::Horizontal)
            ret.setHeight(w);
        else
            ret.setWidth(w);
        break;
    }
    case QStyle::CT_ProgressBar: {
        int finalValue = -1;
        Qt::Orientation orient = Qt::Horizontal;
        if (const QProgressBar *pb = qobject_cast<const QProgressBar *>(widg))
            orient = pb->orientation();

        if (sz == QAquaSizeLarge)
            finalValue = qt_mac_aqua_get_metric(kThemeMetricLargeProgressBarThickness)
                            + qt_mac_aqua_get_metric(kThemeMetricProgressBarShadowOutset);
        else
            finalValue = qt_mac_aqua_get_metric(kThemeMetricNormalProgressBarThickness)
                            + qt_mac_aqua_get_metric(kThemeMetricSmallProgressBarShadowOutset);
        if (orient == Qt::Horizontal)
            ret.setHeight(finalValue);
        else
            ret.setWidth(finalValue);
        break;
    }
    case QStyle::CT_LineEdit:
        if (!widg || !qobject_cast<QComboBox *>(widg->parentWidget())) {
            //should I take into account the font dimentions of the lineedit? -Sam
            if (sz == QAquaSizeLarge)
                ret = QSize(-1, 21);
            else
                ret = QSize(-1, 19);
        }
        break;
    case QStyle::CT_HeaderSection:
        if (isTreeView(widg))
           ret = QSize(-1, qt_mac_aqua_get_metric(kThemeMetricListHeaderHeight));
        break;
    case QStyle::CT_MenuBar:
        if (sz == QAquaSizeLarge) {
            ret = QSize(-1, [[NSApp mainMenu] menuBarHeight]);
            // In the qt_mac_set_native_menubar(false) case,
            // we come it here with a zero-height main menu,
            // preventing the in-window menu from displaying.
            // Use 22 pixels for the height, by observation.
            if (ret.height() <= 0)
                ret.setHeight(22);
        }
        break;
    default:
        break;
    }
    return ret;
}


#if defined(QMAC_QAQUASTYLE_SIZE_CONSTRAIN) || defined(DEBUG_SIZE_CONSTRAINT)
static QAquaWidgetSize qt_aqua_guess_size(const QWidget *widg, QSize large, QSize small, QSize mini)
{
    if (large == QSize(-1, -1)) {
        if (small != QSize(-1, -1))
            return QAquaSizeSmall;
        if (mini != QSize(-1, -1))
            return QAquaSizeMini;
        return QAquaSizeUnknown;
    } else if (small == QSize(-1, -1)) {
        if (mini != QSize(-1, -1))
            return QAquaSizeMini;
        return QAquaSizeLarge;
    } else if (mini == QSize(-1, -1)) {
        return QAquaSizeLarge;
    }

#ifndef QT_NO_MAINWINDOW
    if (qobject_cast<QDockWidget *>(widg->window()) || qEnvironmentVariableIsSet("QWIDGET_ALL_SMALL")) {
        //if (small.width() != -1 || small.height() != -1)
        return QAquaSizeSmall;
    } else if (qEnvironmentVariableIsSet("QWIDGET_ALL_MINI")) {
        return QAquaSizeMini;
    }
#endif

#if 0
    /* Figure out which size we're closer to, I just hacked this in, I haven't
       tested it as it would probably look pretty strange to have some widgets
       big and some widgets small in the same window?? -Sam */
    int large_delta=0;
    if (large.width() != -1) {
        int delta = large.width() - widg->width();
        large_delta += delta * delta;
    }
    if (large.height() != -1) {
        int delta = large.height() - widg->height();
        large_delta += delta * delta;
    }
    int small_delta=0;
    if (small.width() != -1) {
        int delta = small.width() - widg->width();
        small_delta += delta * delta;
    }
    if (small.height() != -1) {
        int delta = small.height() - widg->height();
        small_delta += delta * delta;
    }
    int mini_delta=0;
    if (mini.width() != -1) {
        int delta = mini.width() - widg->width();
        mini_delta += delta * delta;
    }
    if (mini.height() != -1) {
        int delta = mini.height() - widg->height();
        mini_delta += delta * delta;
    }
    if (mini_delta < small_delta && mini_delta < large_delta)
        return QAquaSizeMini;
    else if (small_delta < large_delta)
        return QAquaSizeSmall;
#endif
    return QAquaSizeLarge;
}
#endif

static void qt_drawFocusRingOnPath(CGContextRef cg, NSBezierPath *focusRingPath)
{
    CGContextSaveGState(cg);
    [NSGraphicsContext setCurrentContext:[NSGraphicsContext
         graphicsContextWithGraphicsPort:(CGContextRef)cg flipped:NO]];
    [NSGraphicsContext saveGraphicsState];
    NSSetFocusRingStyle(NSFocusRingOnly);
    [focusRingPath setClip]; // Clear clip path to avoid artifacts when rendering the cursor at zero pos
    [focusRingPath fill];
    [NSGraphicsContext restoreGraphicsState];
    CGContextRestoreGState(cg);
}

QAquaWidgetSize QMacStylePrivate::aquaSizeConstrain(const QStyleOption *option, const QWidget *widg,
                                       QStyle::ContentsType ct, QSize szHint, QSize *insz) const
{
#if defined(QMAC_QAQUASTYLE_SIZE_CONSTRAIN) || defined(DEBUG_SIZE_CONSTRAINT)
    if (option) {
        if (option->state & QStyle::State_Small)
            return QAquaSizeSmall;
        if (option->state & QStyle::State_Mini)
            return QAquaSizeMini;
    }

    if (!widg) {
        if (insz)
            *insz = QSize();
        if (qEnvironmentVariableIsSet("QWIDGET_ALL_SMALL"))
            return QAquaSizeSmall;
        if (qEnvironmentVariableIsSet("QWIDGET_ALL_MINI"))
            return QAquaSizeMini;
        return QAquaSizeUnknown;
    }

    Q_Q(const QMacStyle);
    QSize large = qt_aqua_get_known_size(ct, widg, szHint, QAquaSizeLarge),
          small = qt_aqua_get_known_size(ct, widg, szHint, QAquaSizeSmall),
          mini  = qt_aqua_get_known_size(ct, widg, szHint, QAquaSizeMini);
    bool guess_size = false;
    QAquaWidgetSize ret = QAquaSizeUnknown;
    QMacStyle::WidgetSizePolicy wsp = q->widgetSizePolicy(widg);
    if (wsp == QMacStyle::SizeDefault)
        guess_size = true;
    else if (wsp == QMacStyle::SizeMini)
        ret = QAquaSizeMini;
    else if (wsp == QMacStyle::SizeSmall)
        ret = QAquaSizeSmall;
    else if (wsp == QMacStyle::SizeLarge)
        ret = QAquaSizeLarge;
    if (guess_size)
        ret = qt_aqua_guess_size(widg, large, small, mini);

    QSize *sz = 0;
    if (ret == QAquaSizeSmall)
        sz = &small;
    else if (ret == QAquaSizeLarge)
        sz = &large;
    else if (ret == QAquaSizeMini)
        sz = &mini;
    if (insz)
        *insz = sz ? *sz : QSize(-1, -1);
#ifdef DEBUG_SIZE_CONSTRAINT
    if (sz) {
        const char *size_desc = "Unknown";
        if (sz == &small)
            size_desc = "Small";
        else if (sz == &large)
            size_desc = "Large";
        else if (sz == &mini)
            size_desc = "Mini";
        qDebug("%s - %s: %s taken (%d, %d) [%d, %d]",
               widg ? widg->objectName().toLatin1().constData() : "*Unknown*",
               widg ? widg->metaObject()->className() : "*Unknown*", size_desc, widg->width(), widg->height(),
               sz->width(), sz->height());
    }
#endif
    return ret;
#else
    if (insz)
        *insz = QSize();
    Q_UNUSED(widg);
    Q_UNUSED(ct);
    Q_UNUSED(szHint);
    return QAquaSizeUnknown;
#endif
}

/**
    Returns the free space awailable for contents inside the
    button (and not the size of the contents itself)
*/
HIRect QMacStylePrivate::pushButtonContentBounds(const QStyleOptionButton *btn,
                                                 const HIThemeButtonDrawInfo *bdi) const
{
    HIRect outerBounds = qt_hirectForQRect(btn->rect);
    // Adjust the bounds to correct for
    // carbon not calculating the content bounds fully correct
    if (bdi->kind == kThemePushButton || bdi->kind == kThemePushButtonSmall){
        outerBounds.origin.y += QMacStylePrivate::PushButtonTopOffset;
        outerBounds.size.height -= QMacStylePrivate::PushButtonBottomOffset;
    } else if (bdi->kind == kThemePushButtonMini) {
        outerBounds.origin.y += QMacStylePrivate::PushButtonTopOffset;
    }

    HIRect contentBounds;
    HIThemeGetButtonContentBounds(&outerBounds, bdi, &contentBounds);
    return contentBounds;
}

/**
    Calculates the size of the button contents.
    This includes both the text and the icon.
*/
QSize QMacStylePrivate::pushButtonSizeFromContents(const QStyleOptionButton *btn) const
{
    Q_Q(const QMacStyle);
    QSize csz(0, 0);
    QSize iconSize = btn->icon.isNull() ? QSize(0, 0)
                : (btn->iconSize + QSize(QMacStylePrivate::PushButtonContentPadding, 0));
    QRect textRect = btn->text.isEmpty() ? QRect(0, 0, 1, 1)
                : btn->fontMetrics.boundingRect(QRect(), Qt::AlignCenter, btn->text);
    csz.setWidth(iconSize.width() + textRect.width()
             + ((btn->features & QStyleOptionButton::HasMenu)
                            ? q->proxy()->pixelMetric(QStyle::PM_MenuButtonIndicator, btn, 0) : 0));
    csz.setHeight(qMax(iconSize.height(), textRect.height()));
    return csz;
}

/**
    Checks if the actual contents of btn fits inside the free content bounds of
    'buttonKindToCheck'. Meant as a helper function for 'initHIThemePushButton'
    for determining which button kind to use for drawing.
*/
bool QMacStylePrivate::contentFitsInPushButton(const QStyleOptionButton *btn,
                                               HIThemeButtonDrawInfo *bdi,
                                               ThemeButtonKind buttonKindToCheck) const
{
    ThemeButtonKind tmp = bdi->kind;
    bdi->kind = buttonKindToCheck;
    QSize contentSize = pushButtonSizeFromContents(btn);
    QRect freeContentRect = qt_qrectForHIRect(pushButtonContentBounds(btn, bdi));
    bdi->kind = tmp;
    return freeContentRect.contains(QRect(freeContentRect.x(), freeContentRect.y(),
                                    contentSize.width(), contentSize.height()));
}

/**
    Creates a HIThemeButtonDrawInfo structure that specifies the correct button
    kind and other details to use for drawing the given push button. Which
    button kind depends on the size of the button, the size of the contents,
    explicit user style settings, etc.
*/
void QMacStylePrivate::initHIThemePushButton(const QStyleOptionButton *btn,
                                             const QWidget *widget,
                                             const ThemeDrawState tds,
                                             HIThemeButtonDrawInfo *bdi) const
{
    ThemeDrawState tdsModified = tds;
    if (btn->state & QStyle::State_On)
        tdsModified = kThemeStatePressed;
    bdi->version = qt_mac_hitheme_version;
    bdi->state = tdsModified;
    bdi->value = kThemeButtonOff;

    if (tds == kThemeStateInactive)
        bdi->state = kThemeStateActive;
    if (btn->state & QStyle::State_HasFocus)
        bdi->adornment = kThemeAdornmentFocus;
    else
        bdi->adornment = kThemeAdornmentNone;


    if (btn->features & (QStyleOptionButton::Flat)) {
        bdi->kind = kThemeBevelButton;
    } else {
        switch (aquaSizeConstrain(btn, widget)) {
        case QAquaSizeSmall:
            bdi->kind = kThemePushButtonSmall;
            break;
        case QAquaSizeMini:
            bdi->kind = kThemePushButtonMini;
            break;
        case QAquaSizeLarge:
            // ... We should honor if the user is explicit about using the
            // large button. But right now Qt will specify the large button
            // as default rather than QAquaSizeUnknown.
            // So we treat it like QAquaSizeUnknown
            // to get the dynamic choosing of button kind.
        case QAquaSizeUnknown:
            // Choose the button kind that closest match the button rect, but at the
            // same time displays the button contents without clipping.
            bdi->kind = kThemeBevelButton;
            if (btn->rect.width() >= QMacStylePrivate::BevelButtonW && btn->rect.height() >= QMacStylePrivate::BevelButtonH){
                if (widget && widget->testAttribute(Qt::WA_MacVariableSize)) {
                    if (btn->rect.height() <= QMacStylePrivate::MiniButtonH){
                        if (contentFitsInPushButton(btn, bdi, kThemePushButtonMini))
                            bdi->kind = kThemePushButtonMini;
                    } else if (btn->rect.height() <= QMacStylePrivate::SmallButtonH){
                        if (contentFitsInPushButton(btn, bdi, kThemePushButtonSmall))
                            bdi->kind = kThemePushButtonSmall;
                    } else if (contentFitsInPushButton(btn, bdi, kThemePushButton)) {
                        bdi->kind = kThemePushButton;
                    }
                } else {
                    bdi->kind = kThemePushButton;
                }
            }
        }
    }
}

bool qt_mac_buttonIsRenderedFlat(const QPushButton *pushButton, const QStyleOptionButton *option)
{
    QMacStyle *macStyle = qobject_cast<QMacStyle *>(pushButton->style());
    if (!macStyle)
        return false;
    HIThemeButtonDrawInfo bdi;
    macStyle->d_func()->initHIThemePushButton(option, pushButton, kThemeStateActive, &bdi);
    return bdi.kind == kThemeBevelButton;
}

/**
    Creates a HIThemeButtonDrawInfo structure that specifies the correct button
    kind and other details to use for drawing the given combobox. Which button
    kind depends on the size of the combo, wether or not it is editable,
    explicit user style settings, etc.
*/
void QMacStylePrivate::initComboboxBdi(const QStyleOptionComboBox *combo, HIThemeButtonDrawInfo *bdi,
                                    const QWidget *widget, const ThemeDrawState &tds) const
{
    bdi->version = qt_mac_hitheme_version;
    bdi->adornment = kThemeAdornmentArrowLeftArrow;
    bdi->value = kThemeButtonOff;
    if (combo->state & QStyle::State_HasFocus)
        bdi->adornment = kThemeAdornmentFocus;
    if (combo->activeSubControls & QStyle::SC_ComboBoxArrow)
        bdi->state = kThemeStatePressed;
    else if (tds == kThemeStateInactive)
        bdi->state = kThemeStateActive;
    else
        bdi->state = tds;

    QAquaWidgetSize aSize = aquaSizeConstrain(combo, widget);
    switch (aSize) {
    case QAquaSizeMini:
        bdi->kind = combo->editable ? ThemeButtonKind(kThemeComboBoxMini)
            : ThemeButtonKind(kThemePopupButtonMini);
        break;
    case QAquaSizeSmall:
        bdi->kind = combo->editable ? ThemeButtonKind(kThemeComboBoxSmall)
            : ThemeButtonKind(kThemePopupButtonSmall);
        break;
    case QAquaSizeUnknown:
    case QAquaSizeLarge:
        // Unless the user explicitly specified large buttons, determine the
        // kind by looking at the combox size.
        // ... specifying small and mini-buttons it not a current feature of
        // Qt (e.g. QWidget::getAttribute(WA_ButtonSize)). But when it is, add
        // an extra check here before using the mini and small buttons.
        int h = combo->rect.size().height();
        if (combo->editable){
            if (qobject_cast<const QDateTimeEdit *>(widget)) {
                // Except when, you know, we get a QDateTimeEdit with calendarPopup
                // enabled. And then things get weird, basically because it's a
                // transvestite spinbox with editable combobox tendencies. Meaning
                // that it wants to look a combobox, except that it isn't one, so it
                // doesn't get all those extra free margins around. (Don't know whose
                // idea those margins were, but now it looks like we're stuck with
                // them forever). So anyway, the height threshold should be smaller
                // in this case, or the style gets confused when it needs to render
                // or return any subcontrol size of the poor thing.
                if (h < 9)
                    bdi->kind = kThemeComboBoxMini;
                else if (h < 22)
                    bdi->kind = kThemeComboBoxSmall;
                else
                    bdi->kind = kThemeComboBox;
            } else {
                if (h < 21)
                    bdi->kind = kThemeComboBoxMini;
                else if (h < 26)
                    bdi->kind = kThemeComboBoxSmall;
                else
                    bdi->kind = kThemeComboBox;
            }
        } else {
            // Even if we specify that we want the kThemePopupButton, Carbon
            // will use the kThemePopupButtonSmall if the size matches. So we
            // do the same size check explicit to have the size of the inner
            // text field be correct. Therefore, do this even if the user specifies
            // the use of LargeButtons explicit.
            if (h < 21)
                bdi->kind = kThemePopupButtonMini;
            else if (h < 26)
                bdi->kind = kThemePopupButtonSmall;
            else
                bdi->kind = kThemePopupButton;
        }
        break;
    }
}

/**
    Carbon draws comboboxes (and other views) outside the rect given as argument. Use this function to obtain
    the corresponding inner rect for drawing the same combobox so that it stays inside the given outerBounds.
*/
HIRect QMacStylePrivate::comboboxInnerBounds(const HIRect &outerBounds, int buttonKind)
{
    HIRect innerBounds = outerBounds;
    // Carbon draw parts of the view outside the rect.
    // So make the rect a bit smaller to compensate
    // (I wish HIThemeGetButtonBackgroundBounds worked)
    switch (buttonKind){
    case kThemePopupButton:
        innerBounds.origin.x += 2;
        innerBounds.origin.y += 2;
        innerBounds.size.width -= 5;
        innerBounds.size.height -= 6;
        break;
    case kThemePopupButtonSmall:
        innerBounds.origin.x += 3;
        innerBounds.origin.y += 3;
        innerBounds.size.width -= 6;
        innerBounds.size.height -= 7;
        break;
    case kThemePopupButtonMini:
        innerBounds.origin.x += 2;
        innerBounds.origin.y += 2;
        innerBounds.size.width -= 5;
        innerBounds.size.height -= 6;
        break;
    case kThemeComboBox:
        innerBounds.origin.x += 3;
        innerBounds.origin.y += 2;
        innerBounds.size.width -= 6;
        innerBounds.size.height -= 8;
        break;
    case kThemeComboBoxSmall:
        innerBounds.origin.x += 3;
        innerBounds.origin.y += 3;
        innerBounds.size.width -= 7;
        innerBounds.size.height -= 8;
        break;
    case kThemeComboBoxMini:
        innerBounds.origin.x += 3;
        innerBounds.origin.y += 3;
        innerBounds.size.width -= 4;
        innerBounds.size.height -= 8;
        break;
    default:
        break;
    }
    return innerBounds;
}

/**
    Inside a combobox Qt places a line edit widget. The size of this widget should depend on the kind
    of combobox we choose to draw. This function calculates and returns this size.
*/
QRect QMacStylePrivate::comboboxEditBounds(const QRect &outerBounds, const HIThemeButtonDrawInfo &bdi)
{
    QRect ret = outerBounds;
    switch (bdi.kind){
    case kThemeComboBox:
        ret.adjust(5, 5, -22, -5);
        break;
    case kThemeComboBoxSmall:
        ret.adjust(4, 5, -18, 0);
        ret.setHeight(16);
        break;
    case kThemeComboBoxMini:
        ret.adjust(4, 5, -16, 0);
        ret.setHeight(13);
        break;
    case kThemePopupButton:
        ret.adjust(10, 2, -23, -4);
        break;
    case kThemePopupButtonSmall:
        ret.adjust(9, 3, -20, -3);
        break;
    case kThemePopupButtonMini:
        ret.adjust(8, 3, -19, 0);
        ret.setHeight(13);
        break;
    }
    return ret;
}

/**
    Carbon comboboxes don't scale (sight). If the size of the combo suggest a scaled version,
    create it manually by drawing a small Carbon combo onto a pixmap (use pixmap cache), chop
    it up, and copy it back onto the widget. Othervise, draw the combobox supplied by Carbon directly.
*/
void QMacStylePrivate::drawCombobox(const HIRect &outerBounds, const HIThemeButtonDrawInfo &bdi, QPainter *p)
{
    if (!(bdi.kind == kThemeComboBox && outerBounds.size.height > 28)){
        // We have an unscaled combobox, or popup-button; use Carbon directly.
        HIRect innerBounds = QMacStylePrivate::comboboxInnerBounds(outerBounds, bdi.kind);
        HIThemeDrawButton(&innerBounds, &bdi, QMacCGContext(p), kHIThemeOrientationNormal, 0);
    } else {
        QPixmap buffer;
        QString key = QString(QLatin1String("$qt_cbox%1-%2")).arg(int(bdi.state)).arg(int(bdi.adornment));
        if (!QPixmapCache::find(key, buffer)) {
            HIRect innerBoundsSmallCombo = {{3, 3}, {29, 25}};
            buffer = QPixmap(35, 28);
            buffer.fill(Qt::transparent);
            QPainter buffPainter(&buffer);
            HIThemeDrawButton(&innerBoundsSmallCombo, &bdi, QMacCGContext(&buffPainter), kHIThemeOrientationNormal, 0);
            buffPainter.end();
            QPixmapCache::insert(key, buffer);
        }

        const int bwidth = 20;
        const int fwidth = 10;
        const int fheight = 10;
        int w = qRound(outerBounds.size.width);
        int h = qRound(outerBounds.size.height);
        int bstart = w - bwidth;
        int blower = fheight + 1;
        int flower = h - fheight;
        int sheight = flower - fheight;
        int center = qRound(outerBounds.size.height + outerBounds.origin.y) / 2;

        // Draw upper and lower gap
        p->drawPixmap(fwidth, 0, bstart - fwidth, fheight, buffer, fwidth, 0, 1, fheight);
        p->drawPixmap(fwidth, flower, bstart - fwidth, fheight, buffer, fwidth, buffer.height() - fheight, 1, fheight);
        // Draw left and right gap. Right gap is drawn top and bottom separatly
        p->drawPixmap(0, fheight, fwidth, sheight, buffer, 0, fheight, fwidth, 1);
        p->drawPixmap(bstart, fheight, bwidth, center - fheight, buffer, buffer.width() - bwidth, fheight - 1, bwidth, 1);
        p->drawPixmap(bstart, center, bwidth, sheight / 2, buffer, buffer.width() - bwidth, fheight + 6, bwidth, 1);
        // Draw arrow
        p->drawPixmap(bstart, center - 4, bwidth - 3, 6, buffer, buffer.width() - bwidth, fheight, bwidth - 3, 6);
        // Draw corners
        p->drawPixmap(0, 0, fwidth, fheight, buffer, 0, 0, fwidth, fheight);
        p->drawPixmap(bstart, 0, bwidth, fheight, buffer, buffer.width() - bwidth, 0, bwidth, fheight);
        p->drawPixmap(0, flower, fwidth, fheight, buffer, 0, buffer.height() - fheight, fwidth, fheight);
        p->drawPixmap(bstart, h - blower, bwidth, blower, buffer, buffer.width() - bwidth, buffer.height() - blower, bwidth, blower);
    }
}

/**
    Carbon tableheaders don't scale (sight). So create it manually by drawing a small Carbon header
    onto a pixmap (use pixmap cache), chop it up, and copy it back onto the widget.
*/
void QMacStylePrivate::drawTableHeader(const HIRect &outerBounds,
    bool drawTopBorder, bool drawLeftBorder, const HIThemeButtonDrawInfo &bdi, QPainter *p)
{
    static SInt32 headerHeight = 0;
    static OSStatus err = GetThemeMetric(kThemeMetricListHeaderHeight, &headerHeight);
    Q_UNUSED(err);

    QPixmap buffer;
    QString key = QString(QLatin1String("$qt_tableh%1-%2-%3")).arg(int(bdi.state)).arg(int(bdi.adornment)).arg(int(bdi.value));
    if (!QPixmapCache::find(key, buffer)) {
        HIRect headerNormalRect = {{0., 0.}, {16., CGFloat(headerHeight)}};
        buffer = QPixmap(headerNormalRect.size.width, headerNormalRect.size.height);
        buffer.fill(Qt::transparent);
        QPainter buffPainter(&buffer);
        HIThemeDrawButton(&headerNormalRect, &bdi, QMacCGContext(&buffPainter), kHIThemeOrientationNormal, 0);
        buffPainter.end();
        QPixmapCache::insert(key, buffer);
    }
    const int buttonw = qRound(outerBounds.size.width);
    const int buttonh = qRound(outerBounds.size.height);
    const int framew = 1;
    const int frameh_n = 4;
    const int frameh_s = 3;
    const int transh = buffer.height() - frameh_n - frameh_s;
    int center = buttonh - frameh_s - int(transh / 2.0f) + 1; // Align bottom;

    int skipTopBorder = 0;
    if (!drawTopBorder)
        skipTopBorder = 1;

    p->translate(outerBounds.origin.x, outerBounds.origin.y);

    p->drawPixmap(QRect(QRect(0, -skipTopBorder, buttonw - framew , frameh_n)), buffer, QRect(framew, 0, 1, frameh_n));
    p->drawPixmap(QRect(0, buttonh - frameh_s, buttonw - framew, frameh_s), buffer, QRect(framew, buffer.height() - frameh_s, 1, frameh_s));
    // Draw upper and lower center blocks
    p->drawPixmap(QRect(0, frameh_n - skipTopBorder, buttonw - framew, center - frameh_n + skipTopBorder), buffer, QRect(framew, frameh_n, 1, 1));
    p->drawPixmap(QRect(0, center, buttonw - framew, buttonh - center - frameh_s), buffer, QRect(framew, buffer.height() - frameh_s, 1, 1));
    // Draw right center block borders
    p->drawPixmap(QRect(buttonw - framew, frameh_n - skipTopBorder, framew, center - frameh_n), buffer, QRect(buffer.width() - framew, frameh_n, framew, 1));
    p->drawPixmap(QRect(buttonw - framew, center, framew, buttonh - center - 1), buffer, QRect(buffer.width() - framew, buffer.height() - frameh_s, framew, 1));
    // Draw right corners
    p->drawPixmap(QRect(buttonw - framew, -skipTopBorder, framew, frameh_n), buffer, QRect(buffer.width() - framew, 0, framew, frameh_n));
    p->drawPixmap(QRect(buttonw - framew, buttonh - frameh_s, framew, frameh_s), buffer, QRect(buffer.width() - framew, buffer.height() - frameh_s, framew, frameh_s));
    // Draw center transition block
    p->drawPixmap(QRect(0, center - qRound(transh / 2.0f), buttonw - framew, buffer.height() - frameh_n - frameh_s), buffer, QRect(framew, frameh_n + 1, 1, transh));
    // Draw right center transition block border
    p->drawPixmap(QRect(buttonw - framew, center - qRound(transh / 2.0f), framew, buffer.height() - frameh_n - frameh_s), buffer, QRect(buffer.width() - framew, frameh_n + 1, framew, transh));
    if (drawLeftBorder){
        // Draw left center block borders
        p->drawPixmap(QRect(0, frameh_n - skipTopBorder, framew, center - frameh_n + skipTopBorder), buffer, QRect(0, frameh_n, framew, 1));
        p->drawPixmap(QRect(0, center, framew, buttonh - center - 1), buffer, QRect(0, buffer.height() - frameh_s, framew, 1));
        // Draw left corners
        p->drawPixmap(QRect(0, -skipTopBorder, framew, frameh_n), buffer, QRect(0, 0, framew, frameh_n));
        p->drawPixmap(QRect(0, buttonh - frameh_s, framew, frameh_s), buffer, QRect(0, buffer.height() - frameh_s, framew, frameh_s));
        // Draw left center transition block border
        p->drawPixmap(QRect(0, center - qRound(transh / 2.0f), framew, buffer.height() - frameh_n - frameh_s), buffer, QRect(0, frameh_n + 1, framew, transh));
    }

    p->translate(-outerBounds.origin.x, -outerBounds.origin.y);
}

/*
    Returns cutoff sizes for scroll bars.
    thumbIndicatorCutoff is the smallest size where the thumb indicator is drawn.
    scrollButtonsCutoff is the smallest size where the up/down buttons is drawn.
*/
enum ScrollBarCutoffType { thumbIndicatorCutoff = 0, scrollButtonsCutoff = 1 };
static int scrollButtonsCutoffSize(ScrollBarCutoffType cutoffType, QMacStyle::WidgetSizePolicy widgetSize)
{
    // Mini scroll bars do not exist as of version 10.4.
    if (widgetSize ==  QMacStyle::SizeMini)
        return 0;

    const int sizeIndex = (widgetSize == QMacStyle::SizeSmall) ? 1 : 0;
    static const int sizeTable[2][2] = { { 61, 56 }, { 49, 44 } };
    return sizeTable[sizeIndex][cutoffType];
}

void QMacStylePrivate::getSliderInfo(QStyle::ComplexControl cc, const QStyleOptionSlider *slider,
                          HIThemeTrackDrawInfo *tdi, const QWidget *needToRemoveMe) const
{
    memset(tdi, 0, sizeof(HIThemeTrackDrawInfo)); // We don't get it all for some reason or another...
    tdi->version = qt_mac_hitheme_version;
    tdi->reserved = 0;
    tdi->filler1 = 0;
    bool isScrollbar = (cc == QStyle::CC_ScrollBar);
    switch (aquaSizeConstrain(slider, needToRemoveMe)) {
    case QAquaSizeUnknown:
    case QAquaSizeLarge:
        if (isScrollbar)
            tdi->kind = kThemeMediumScrollBar;
        else
            tdi->kind = kThemeMediumSlider;
        break;
    case QAquaSizeMini:
        if (isScrollbar)
            tdi->kind = kThemeSmallScrollBar; // should be kThemeMiniScrollBar, but not implemented
        else
            tdi->kind = kThemeMiniSlider;
        break;
    case QAquaSizeSmall:
        if (isScrollbar)
            tdi->kind = kThemeSmallScrollBar;
        else
            tdi->kind = kThemeSmallSlider;
        break;
    }
    tdi->bounds = qt_hirectForQRect(slider->rect);
    tdi->min = slider->minimum;
    tdi->max = slider->maximum;
    tdi->value = slider->sliderPosition;
    tdi->attributes = kThemeTrackShowThumb;
    if (slider->upsideDown)
        tdi->attributes |= kThemeTrackRightToLeft;
    if (slider->orientation == Qt::Horizontal) {
        tdi->attributes |= kThemeTrackHorizontal;
        if (isScrollbar && slider->direction == Qt::RightToLeft) {
            if (!slider->upsideDown)
                tdi->attributes |= kThemeTrackRightToLeft;
            else
                tdi->attributes &= ~kThemeTrackRightToLeft;
        }
    }

    // Tiger broke reverse scroll bars so put them back and "fake it"
    if (isScrollbar && (tdi->attributes & kThemeTrackRightToLeft)) {
        tdi->attributes &= ~kThemeTrackRightToLeft;
        tdi->value = tdi->max - slider->sliderPosition;
    }

    tdi->enableState = (slider->state & QStyle::State_Enabled) ? kThemeTrackActive
                                                             : kThemeTrackDisabled;
    if (!isScrollbar) {
        if (slider->state & QStyle::QStyle::State_HasFocus)
            tdi->attributes |= kThemeTrackHasFocus;
        if (slider->tickPosition == QSlider::NoTicks || slider->tickPosition == QSlider::TicksBothSides)
            tdi->trackInfo.slider.thumbDir = kThemeThumbPlain;
        else if (slider->tickPosition == QSlider::TicksAbove)
            tdi->trackInfo.slider.thumbDir = kThemeThumbUpward;
        else
            tdi->trackInfo.slider.thumbDir = kThemeThumbDownward;
    } else {
        tdi->trackInfo.scrollbar.viewsize = slider->pageStep;
    }
}

void QMacStylePrivate::setAutoDefaultButton(QObject *button) const
{
    if (autoDefaultButton != button) {
        if (QStyleAnimation *anim = animation(autoDefaultButton)) {
            anim->updateTarget();
            stopAnimation(autoDefaultButton);
        }
        autoDefaultButton = button;
    }
    if (autoDefaultButton && !animation(autoDefaultButton))
        startAnimation(new QStyleAnimation(autoDefaultButton));
}

QMacStylePrivate::QMacStylePrivate()
    : mouseDown(false)
{
    defaultButtonStart = CFAbsoluteTimeGetCurrent();
    memset(&buttonState, 0, sizeof(ButtonState));

    if (ptrHIShapeGetBounds == 0) {
        QLibrary library(QLatin1String("/System/Library/Frameworks/Carbon.framework/Carbon"));
        library.setLoadHints(QLibrary::ExportExternalSymbolsHint);
        ptrHIShapeGetBounds = reinterpret_cast<PtrHIShapeGetBounds>(library.resolve("HIShapeGetBounds"));
    }

}

ThemeDrawState QMacStylePrivate::getDrawState(QStyle::State flags)
{
    ThemeDrawState tds = kThemeStateActive;
    if (flags & QStyle::State_Sunken) {
        tds = kThemeStatePressed;
    } else if (flags & QStyle::State_Active) {
        if (!(flags & QStyle::State_Enabled))
            tds = kThemeStateUnavailable;
    } else {
        if (flags & QStyle::State_Enabled)
            tds = kThemeStateInactive;
        else
            tds = kThemeStateUnavailableInactive;
    }
    return tds;
}

void QMacStylePrivate::drawColorlessButton(const HIRect &macRect, HIThemeButtonDrawInfo *bdi,
                                           QPainter *p, const QStyleOption *opt) const
{
    int xoff = 0,
        yoff = 0,
        extraWidth = 0,
        extraHeight = 0,
        finalyoff = 0;

    const bool combo = opt->type == QStyleOption::SO_ComboBox;
    const bool button = opt->type == QStyleOption::SO_Button;
    const bool pressed = bdi->state == kThemeStatePressed;

    if (button && pressed) {
        if (bdi->kind == kThemePushButton) {
            extraHeight = 2;
        } else if (bdi->kind == kThemePushButtonSmall) {
            xoff = 1;
            extraWidth = 2;
            extraHeight = 5;
        }
    }

    int devicePixelRatio = p->device()->devicePixelRatio();
    int width = devicePixelRatio * (int(macRect.size.width) + extraWidth);
    int height = devicePixelRatio * (int(macRect.size.height) + extraHeight);

    if (width <= 0 || height <= 0)
        return;   // nothing to draw

    QString key = QLatin1String("$qt_mac_style_ctb_") + QString::number(bdi->kind) + QLatin1Char('_')
                  + QString::number(bdi->value) + QLatin1Char('_')
                  + (button ? QString::number(bdi->state) + QLatin1Char('_') : QString())
                  + QLatin1Char('_') + QString::number(width) + QLatin1Char('_') + QString::number(height);
    QPixmap pm;
    if (!QPixmapCache::find(key, pm)) {
        QPixmap activePixmap(width, height);
        activePixmap.setDevicePixelRatio(devicePixelRatio);
        activePixmap.fill(Qt::transparent);
        {
            if (combo){
                // Carbon combos don't scale. Therefore we draw it
                // ourselves, if a scaled version is needed.
                QPainter tmpPainter(&activePixmap);
                QMacStylePrivate::drawCombobox(macRect, *bdi, &tmpPainter);
            } else {
                QMacCGContext cg(&activePixmap);
                HIRect newRect = CGRectMake(xoff, yoff, macRect.size.width, macRect.size.height);
                if (button && pressed)
                    bdi->state = kThemeStateActive;
                HIThemeDrawButton(&newRect, bdi, cg, kHIThemeOrientationNormal, 0);
            }
        }

        if (!combo && !button && bdi->value == kThemeButtonOff) {
            pm = activePixmap;
        } else if (combo || button) {
            QImage image = activePixmap.toImage();

            for (int y = 0; y < height; ++y) {
                QRgb *scanLine = reinterpret_cast<QRgb *>(image.scanLine(y));

                for (int x = 0; x < width; ++x) {
                    QRgb &pixel = scanLine[x];

                    int darkest = qRed(pixel);
                    int mid = qGreen(pixel);
                    int lightest = qBlue(pixel);

                    if (darkest > mid)
                        qSwap(darkest, mid);
                    if (mid > lightest)
                        qSwap(mid, lightest);
                    if (darkest > mid)
                        qSwap(darkest, mid);

                    int gray = (mid + 2 * lightest) / 3;
                    if (pressed)
                        gray *= 0.88;
                    pixel = qRgba(gray, gray, gray, qAlpha(pixel));
                }
            }
            pm = QPixmap::fromImage(image);
        } else {
            QImage activeImage = activePixmap.toImage();
            QImage colorlessImage;
            {
                QPixmap colorlessPixmap(width, height);
                colorlessPixmap.setDevicePixelRatio(devicePixelRatio);
                colorlessPixmap.fill(Qt::transparent);

                QMacCGContext cg(&colorlessPixmap);
                HIRect newRect = CGRectMake(xoff, yoff, macRect.size.width, macRect.size.height);
                int oldValue = bdi->value;
                bdi->value = kThemeButtonOff;
                HIThemeDrawButton(&newRect, bdi, cg, kHIThemeOrientationNormal, 0);
                bdi->value = oldValue;
                colorlessImage = colorlessPixmap.toImage();
            }

            for (int y = 0; y < height; ++y) {
                QRgb *colorlessScanLine = reinterpret_cast<QRgb *>(colorlessImage.scanLine(y));
                const QRgb *activeScanLine = reinterpret_cast<const QRgb *>(activeImage.scanLine(y));

                for (int x = 0; x < width; ++x) {
                    QRgb &colorlessPixel = colorlessScanLine[x];
                    QRgb activePixel = activeScanLine[x];

                    if (activePixel != colorlessPixel) {
                        int max = qMax(qMax(qRed(activePixel), qGreen(activePixel)),
                                       qBlue(activePixel));
                        QRgb newPixel = qRgba(max, max, max, qAlpha(activePixel));
                        if (qGray(newPixel) < qGray(colorlessPixel)
                                || qAlpha(newPixel) > qAlpha(colorlessPixel))
                            colorlessPixel = newPixel;
                    }
                }
            }
            pm = QPixmap::fromImage(colorlessImage);
        }
        QPixmapCache::insert(key, pm);
    }
    p->drawPixmap(int(macRect.origin.x) - xoff, int(macRect.origin.y) + finalyoff, width / devicePixelRatio, height / devicePixelRatio , pm);
}

QMacStyle::QMacStyle()
    : QCommonStyle(*new QMacStylePrivate)
{
    Q_D(QMacStyle);
#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_7
    if (QSysInfo::MacintoshVersion >= QSysInfo::MV_10_7) {
        d->receiver = [[NotificationReceiver alloc] initWithPrivate:d];
        NotificationReceiver *receiver = static_cast<NotificationReceiver *>(d->receiver);

        [[NSNotificationCenter defaultCenter] addObserver:receiver
            selector:@selector(scrollBarStyleDidChange:)
            name:NSPreferredScrollerStyleDidChangeNotification
            object:nil];

        d->nsscroller = [[NSScroller alloc] init];
    }
#endif
    d->indicatorBranchButtonCell = nil;
}

QMacStyle::~QMacStyle()
{
#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_7
    Q_D(QMacStyle);
    if (QSysInfo::MacintoshVersion >= QSysInfo::MV_10_7) {
        [reinterpret_cast<NSScroller*>(d->nsscroller) release];

        NotificationReceiver *receiver = static_cast<NotificationReceiver *>(d->receiver);
        [[NSNotificationCenter defaultCenter] removeObserver:receiver];
        [receiver release];
    }
#endif

    delete qt_mac_backgroundPattern;
    qt_mac_backgroundPattern = 0;
}

/*! \internal
    Generates the standard widget background pattern.
*/
QPixmap QMacStylePrivate::generateBackgroundPattern() const
{
    QPixmap px(4, 4);
    QMacCGContext cg(&px);
    HIThemeSetFill(kThemeBrushDialogBackgroundActive, 0, cg, kHIThemeOrientationNormal);
    const CGRect cgRect = CGRectMake(0, 0, px.width(), px.height());
    CGContextFillRect(cg, cgRect);
    return px;
}

/*! \internal
    Fills the given \a rect with the pattern stored in \a brush. As an optimization,
    HIThemeSetFill us used directly if we are filling with the standard background.
*/
void qt_mac_fill_background(QPainter *painter, const QRegion &rgn, const QBrush &brush)
{
#if 0
    QPoint dummy;
    const QPaintDevice *target = painter->device();
    const QPaintDevice *redirected = QPainter::redirected(target, &dummy);
    //const bool usePainter = redirected && redirected != target;

    if (!usePainter && qt_mac_backgroundPattern
        && qt_mac_backgroundPattern->cacheKey() == brush.texture().cacheKey()) {

        painter->setClipRegion(rgn);

        QCFType<CGContextRef> cg = qt_mac_cg_context(target);
        CGContextSaveGState(cg);
        HIThemeSetFill(kThemeBrushDialogBackgroundActive, 0, cg, kHIThemeOrientationInverted);

        const QVector<QRect> &rects = rgn.rects();
        for (int i = 0; i < rects.size(); ++i) {
            const QRect rect(rects.at(i));
            // Anchor the pattern to the top so it stays put when the window is resized.
            CGContextSetPatternPhase(cg, CGSizeMake(rect.width(), rect.height()));
            CGRect mac_rect = CGRectMake(rect.x(), rect.y(), rect.width(), rect.height());
            CGContextFillRect(cg, mac_rect);
        }

        CGContextRestoreGState(cg);
    } else {
#endif
        const QRect rect(rgn.boundingRect());
        painter->setClipRegion(rgn);
        painter->drawTiledPixmap(rect, brush.texture(), rect.topLeft());
//    }
}

void QMacStyle::polish(QPalette &pal)
{
    Q_D(QMacStyle);
    if (!qt_mac_backgroundPattern) {
        if (!qApp)
            return;
        qt_mac_backgroundPattern = new QPixmap(d->generateBackgroundPattern());
    }


    QCFString theme;
    const OSErr err = CopyThemeIdentifier(&theme);
    if (err == noErr && CFStringCompare(theme, kThemeAppearanceAquaGraphite, 0) == kCFCompareEqualTo) {
        pal.setBrush(QPalette::All, QPalette::AlternateBase, QColor(240, 240, 240));
    } else {
        pal.setBrush(QPalette::All, QPalette::AlternateBase, QColor(237, 243, 254));
    }
}

void QMacStyle::polish(QApplication *)
{
}

void QMacStyle::unpolish(QApplication *)
{
}

void QMacStyle::polish(QWidget* w)
{
    if (qt_mac_is_metal(w) && !w->testAttribute(Qt::WA_SetPalette)) {
        // Set a clear brush so that the metal shines through.
        QPalette pal = w->palette();
        QBrush background(Qt::transparent);
        pal.setBrush(QPalette::All, QPalette::Window, background);
        pal.setBrush(QPalette::All, QPalette::Button, background);
        w->setPalette(pal);
        w->setAttribute(Qt::WA_SetPalette, false);
    }

    if (qobject_cast<QMenu*>(w) || qobject_cast<QComboBoxPrivateContainer *>(w)) {
        w->setWindowOpacity(QSysInfo::MacintoshVersion >= QSysInfo::MV_10_5 ? 0.985 : 0.94);
        if (!w->testAttribute(Qt::WA_SetPalette)) {
            QPixmap px(64, 64);
            px.fill(Qt::white);
            HIThemeMenuDrawInfo mtinfo;
            mtinfo.version = qt_mac_hitheme_version;
            mtinfo.menuType = kThemeMenuTypePopUp;
            // HIRect rect = CGRectMake(0, 0, px.width(), px.height());
            // ###
            //HIThemeDrawMenuBackground(&rect, &mtinfo, QCFType<CGContextRef>(qt_mac_cg_context(&px)),
            //                          kHIThemeOrientationNormal);
            QPalette pal = w->palette();
            QBrush background(px);
            pal.setBrush(QPalette::All, QPalette::Window, background);
            pal.setBrush(QPalette::All, QPalette::Button, background);
            w->setPalette(pal);
            w->setAttribute(Qt::WA_SetPalette, false);
        }
    }

    if (QTabBar *tb = qobject_cast<QTabBar*>(w)) {
        if (tb->documentMode()) {
            w->setAttribute(Qt::WA_Hover);
            w->setFont(qt_app_fonts_hash()->value("QSmallFont", QFont()));
            QPalette p = w->palette();
            p.setColor(QPalette::WindowText, QColor(17, 17, 17));
            w->setPalette(p);
        }
    }

    QCommonStyle::polish(w);

    if (QRubberBand *rubber = qobject_cast<QRubberBand*>(w)) {
        rubber->setWindowOpacity(0.25);
        rubber->setAttribute(Qt::WA_PaintOnScreen, false);
        rubber->setAttribute(Qt::WA_NoSystemBackground, false);
    }

    if (qobject_cast<QScrollBar*>(w)) {
        w->setAttribute(Qt::WA_OpaquePaintEvent, false);
        w->setAttribute(Qt::WA_Hover, true);
        w->setMouseTracking(true);
    }
}

void QMacStyle::unpolish(QWidget* w)
{
    if ((qobject_cast<QMenu*>(w) || qt_mac_is_metal(w)) && !w->testAttribute(Qt::WA_SetPalette)) {
        QPalette pal = qApp->palette(w);
        w->setPalette(pal);
        w->setAttribute(Qt::WA_SetPalette, false);
        w->setWindowOpacity(1.0);
    }

    if (QComboBox *combo = qobject_cast<QComboBox *>(w)) {
        if (!combo->isEditable()) {
            if (QWidget *widget = combo->findChild<QComboBoxPrivateContainer *>())
                widget->setWindowOpacity(1.0);
        }
    }

    if (QRubberBand *rubber = qobject_cast<QRubberBand*>(w)) {
        rubber->setWindowOpacity(1.0);
        rubber->setAttribute(Qt::WA_PaintOnScreen, true);
        rubber->setAttribute(Qt::WA_NoSystemBackground, true);
    }

    if (QFocusFrame *frame = qobject_cast<QFocusFrame *>(w))
        frame->setAttribute(Qt::WA_NoSystemBackground, true);

    QCommonStyle::unpolish(w);

    if (qobject_cast<QScrollBar*>(w)) {
        w->setAttribute(Qt::WA_OpaquePaintEvent, true);
        w->setAttribute(Qt::WA_Hover, false);
        w->setMouseTracking(false);
    }
}

int QMacStyle::pixelMetric(PixelMetric metric, const QStyleOption *opt, const QWidget *widget) const
{
    Q_D(const QMacStyle);
    int controlSize = getControlSize(opt, widget);
    SInt32 ret = 0;

    switch (metric) {
    case PM_TabCloseIndicatorWidth:
    case PM_TabCloseIndicatorHeight:
        ret = closeButtonSize;
        break;
    case PM_ToolBarIconSize:
        ret = proxy()->pixelMetric(PM_LargeIconSize);
        break;
    case PM_FocusFrameVMargin:
    case PM_FocusFrameHMargin:
        GetThemeMetric(kThemeMetricFocusRectOutset, &ret);
        break;
    case PM_DialogButtonsSeparator:
        ret = -5;
        break;
    case PM_DialogButtonsButtonHeight: {
        QSize sz;
        ret = d->aquaSizeConstrain(opt, 0, QStyle::CT_PushButton, QSize(-1, -1), &sz);
        if (sz == QSize(-1, -1))
            ret = 32;
        else
            ret = sz.height();
        break; }
    case PM_DialogButtonsButtonWidth: {
        QSize sz;
        ret = d->aquaSizeConstrain(opt, 0, QStyle::CT_PushButton, QSize(-1, -1), &sz);
        if (sz == QSize(-1, -1))
            ret = 70;
        else
            ret = sz.width();
        break; }

    case PM_MenuBarHMargin:
        ret = 8;
        break;

    case PM_MenuBarVMargin:
        ret = 0;
        break;

    case PM_MenuBarPanelWidth:
        ret = 0;
        break;

    case QStyle::PM_MenuDesktopFrameWidth:
        ret = 5;
        break;

    case PM_CheckBoxLabelSpacing:
    case PM_RadioButtonLabelSpacing:
        ret = 2;
        break;
    case PM_MenuScrollerHeight:
#if 0
        SInt16 ash, asw;
        GetThemeMenuItemExtra(kThemeMenuItemScrollUpArrow, &ash, &asw);
        ret = ash;
#else
        ret = 15; // I hate having magic numbers in here...
#endif
        break;
    case PM_DefaultFrameWidth:
#ifndef QT_NO_MAINWINDOW
        if (widget && (widget->isWindow() || !widget->parentWidget()
                || (qobject_cast<const QMainWindow*>(widget->parentWidget())
                   && static_cast<QMainWindow *>(widget->parentWidget())->centralWidget() == widget))
                && qobject_cast<const QAbstractScrollArea *>(widget))
            ret = 0;
        else
#endif
        // The combo box popup has no frame.
        if (qstyleoption_cast<const QStyleOptionComboBox *>(opt) != 0)
            ret = 0;
        else
            ret = 1;
        break;
    case PM_MaximumDragDistance:
        ret = -1;
        break;
    case PM_ScrollBarSliderMin:
        ret = 24;
        break;
    case PM_SpinBoxFrameWidth:
        GetThemeMetric(kThemeMetricEditTextFrameOutset, &ret);
        switch (d->aquaSizeConstrain(opt, widget)) {
        default:
            ret += 2;
            break;
        case QAquaSizeMini:
            ret += 1;
            break;
        }
        break;
    case PM_ButtonShiftHorizontal:
    case PM_ButtonShiftVertical:
        ret = 0;
        break;
    case PM_SliderLength:
        ret = 17;
        break;
        // Returns the number of pixels to use for the business part of the
        // slider (i.e., the non-tickmark portion). The remaining space is shared
        // equally between the tickmark regions.
    case PM_SliderControlThickness:
        if (const QStyleOptionSlider *sl = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
            int space = (sl->orientation == Qt::Horizontal) ? sl->rect.height() : sl->rect.width();
            int ticks = sl->tickPosition;
            int n = 0;
            if (ticks & QSlider::TicksAbove)
                ++n;
            if (ticks & QSlider::TicksBelow)
                ++n;
            if (!n) {
                ret = space;
                break;
            }

            int thick = 6;        // Magic constant to get 5 + 16 + 5
            if (ticks != QSlider::TicksBothSides && ticks != QSlider::NoTicks)
                thick += proxy()->pixelMetric(PM_SliderLength, sl, widget) / 4;

            space -= thick;
            if (space > 0)
                thick += (space * 2) / (n + 2);
            ret = thick;
        } else {
            ret = 0;
        }
        break;
    case PM_SmallIconSize:
        ret = int(QStyleHelper::dpiScaled(16.));
        break;

    case PM_LargeIconSize:
        ret = int(QStyleHelper::dpiScaled(32.));
        break;

    case PM_IconViewIconSize:
        ret = proxy()->pixelMetric(PM_LargeIconSize, opt, widget);
        break;

    case PM_ButtonDefaultIndicator:
        ret = 0;
        break;
    case PM_TitleBarHeight:
        if (const QStyleOptionTitleBar *tb = qstyleoption_cast<const QStyleOptionTitleBar *>(opt)) {
            HIThemeWindowDrawInfo wdi;
            wdi.version = qt_mac_hitheme_version;
            wdi.state = kThemeStateActive;
            wdi.windowType = QtWinType;
            if (tb->titleBarState)
                wdi.attributes = kThemeWindowHasFullZoom | kThemeWindowHasCloseBox
                                  | kThemeWindowHasCollapseBox;
            else if (tb->titleBarFlags & Qt::WindowSystemMenuHint)
                wdi.attributes = kThemeWindowHasCloseBox;
            else
                wdi.attributes = 0;
            wdi.titleHeight = tb->rect.height();
            wdi.titleWidth = tb->rect.width();
            QCFType<HIShapeRef> region;
            HIRect hirect = qt_hirectForQRect(tb->rect);
            if (hirect.size.width <= 0)
                hirect.size.width = 100;
            if (hirect.size.height <= 0)
                hirect.size.height = 30;

            HIThemeGetWindowShape(&hirect, &wdi, kWindowTitleBarRgn, &region);
            HIRect rect;
            ptrHIShapeGetBounds(region, &rect);
            ret = int(rect.size.height);
        }
        break;
    case PM_TabBarTabVSpace:
        ret = 4;
        break;
    case PM_TabBarTabShiftHorizontal:
    case PM_TabBarTabShiftVertical:
        ret = 0;
        break;
    case PM_TabBarBaseHeight:
        ret = 0;
        break;
    case PM_TabBarTabOverlap:
        ret = 0;
        break;
    case PM_TabBarBaseOverlap:
        switch (d->aquaSizeConstrain(opt, widget)) {
        case QAquaSizeUnknown:
        case QAquaSizeLarge:
            ret = 11;
            break;
        case QAquaSizeSmall:
            ret = 8;
            break;
        case QAquaSizeMini:
            ret = 7;
            break;
        }
        break;
    case PM_ScrollBarExtent: {
#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_7
        if (QSysInfo::MacintoshVersion >= QSysInfo::MV_10_7 &&
           [NSScroller preferredScrollerStyle] == NSScrollerStyleOverlay) {
            switch (d->aquaSizeConstrain(opt, widget)) {
            case QAquaSizeUnknown:
            case QAquaSizeLarge:
                ret = QSysInfo::macVersion() >= QSysInfo::MV_10_8 ? 16 : 9;
                break;
            case QAquaSizeMini:
            case QAquaSizeSmall:
                ret =  QSysInfo::macVersion() >= QSysInfo::MV_10_8 ? 14 : 7;
                break;
            }
            break;
        }
#endif
        switch (d->aquaSizeConstrain(opt, widget)) {
        case QAquaSizeUnknown:
        case QAquaSizeLarge:
            GetThemeMetric(kThemeMetricScrollBarWidth, &ret);
            break;
        case QAquaSizeMini:
        case QAquaSizeSmall:
            GetThemeMetric(kThemeMetricSmallScrollBarWidth, &ret);
            break;
        }
        break; }
    case PM_IndicatorHeight: {
        switch (d->aquaSizeConstrain(opt, widget)) {
        case QAquaSizeUnknown:
        case QAquaSizeLarge:
            GetThemeMetric(kThemeMetricCheckBoxHeight, &ret);
            break;
        case QAquaSizeMini:
            GetThemeMetric(kThemeMetricMiniCheckBoxHeight, &ret);
            break;
        case QAquaSizeSmall:
            GetThemeMetric(kThemeMetricSmallCheckBoxHeight, &ret);
            break;
        }
        break; }
    case PM_IndicatorWidth: {
        switch (d->aquaSizeConstrain(opt, widget)) {
        case QAquaSizeUnknown:
        case QAquaSizeLarge:
            GetThemeMetric(kThemeMetricCheckBoxWidth, &ret);
            break;
        case QAquaSizeMini:
            GetThemeMetric(kThemeMetricMiniCheckBoxWidth, &ret);
            break;
        case QAquaSizeSmall:
            GetThemeMetric(kThemeMetricSmallCheckBoxWidth, &ret);
            break;
        }
        ++ret;
        break; }
    case PM_ExclusiveIndicatorHeight: {
        switch (d->aquaSizeConstrain(opt, widget)) {
        case QAquaSizeUnknown:
        case QAquaSizeLarge:
            GetThemeMetric(kThemeMetricRadioButtonHeight, &ret);
            break;
        case QAquaSizeMini:
            GetThemeMetric(kThemeMetricMiniRadioButtonHeight, &ret);
            break;
        case QAquaSizeSmall:
            GetThemeMetric(kThemeMetricSmallRadioButtonHeight, &ret);
            break;
        }
        break; }
    case PM_ExclusiveIndicatorWidth: {
        switch (d->aquaSizeConstrain(opt, widget)) {
        case QAquaSizeUnknown:
        case QAquaSizeLarge:
            GetThemeMetric(kThemeMetricRadioButtonWidth, &ret);
            break;
        case QAquaSizeMini:
            GetThemeMetric(kThemeMetricMiniRadioButtonWidth, &ret);
            break;
        case QAquaSizeSmall:
            GetThemeMetric(kThemeMetricSmallRadioButtonWidth, &ret);
            break;
        }
        ++ret;
        break; }
    case PM_MenuVMargin:
        ret = 4;
        break;
    case PM_MenuPanelWidth:
        ret = 0;
        break;
    case PM_ToolTipLabelFrameWidth:
        ret = 0;
        break;
    case PM_SizeGripSize: {
        QAquaWidgetSize aSize;
        if (widget && widget->window()->windowType() == Qt::Tool)
            aSize = QAquaSizeSmall;
        else
            aSize = QAquaSizeLarge;
        const QSize size = qt_aqua_get_known_size(CT_SizeGrip, widget, QSize(), aSize);
        ret = size.width();
        break; }
    case PM_MdiSubWindowFrameWidth:
        ret = 1;
        break;
    case PM_DockWidgetFrameWidth:
        ret = 0;
        break;
    case PM_DockWidgetTitleMargin:
        ret = 0;
        break;
    case PM_DockWidgetSeparatorExtent:
        ret = 1;
        break;
    case PM_ToolBarHandleExtent:
        ret = 11;
        break;
    case PM_ToolBarItemMargin:
        ret = 0;
        break;
    case PM_ToolBarItemSpacing:
        ret = 4;
        break;
    case PM_SplitterWidth:
        ret = qMax(7, QApplication::globalStrut().width());
        break;
    case PM_LayoutLeftMargin:
    case PM_LayoutTopMargin:
    case PM_LayoutRightMargin:
    case PM_LayoutBottomMargin:
        {
            bool isWindow = false;
            if (opt) {
                isWindow = (opt->state & State_Window);
            } else if (widget) {
                isWindow = widget->isWindow();
            }

            if (isWindow) {
                bool isMetal = widget && widget->testAttribute(Qt::WA_MacBrushedMetal);
                if (isMetal) {
                    if (metric == PM_LayoutTopMargin) {
                        return_SIZE(9 /* AHIG */, 6 /* guess */, 6 /* guess */);
                    } else if (metric == PM_LayoutBottomMargin) {
                        return_SIZE(18 /* AHIG */, 15 /* guess */, 13 /* guess */);
                    } else {
                        return_SIZE(14 /* AHIG */, 11 /* guess */, 9 /* guess */);
                    }
                } else {
                    /*
                        AHIG would have (20, 8, 10) here but that makes
                        no sense. It would also have 14 for the top margin
                        but this contradicts both Builder and most
                        applications.
                    */
                    return_SIZE(20, 10, 10);    // AHIG
                }
            } else {
                // hack to detect QTabWidget
                if (widget && widget->parentWidget()
                        && widget->parentWidget()->sizePolicy().controlType() == QSizePolicy::TabWidget) {
                    if (metric == PM_LayoutTopMargin) {
                        /*
                            Builder would have 14 (= 20 - 6) instead of 12,
                            but that makes the tab look disproportionate.
                        */
                        return_SIZE(12, 6, 6);  // guess
                    } else {
                        return_SIZE(20 /* Builder */, 8 /* guess */, 8 /* guess */);
                    }
                } else {
                    /*
                        Child margins are highly inconsistent in AHIG and Builder.
                    */
                    return_SIZE(12, 8, 6);    // guess
                }
            }
        }
    case PM_LayoutHorizontalSpacing:
    case PM_LayoutVerticalSpacing:
        return -1;
    case QStyle::PM_TabBarTabHSpace:
        switch (d->aquaSizeConstrain(opt, widget)) {
        case QAquaSizeLarge:
        case QAquaSizeUnknown:
            ret = QCommonStyle::pixelMetric(metric, opt, widget);
            break;
        case QAquaSizeSmall:
            ret = 20;
            break;
        case QAquaSizeMini:
            ret = 16;
            break;
        }
        break;
    case PM_MenuHMargin:
        ret = 0;
        break;
    case PM_ToolBarExtensionExtent:
        ret = 21;
        break;
    case PM_ToolBarFrameWidth:
        ret = 1;
        break;
    case PM_ScrollView_ScrollBarOverlap:
#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_7
        ret = (QSysInfo::MacintoshVersion >= QSysInfo::MV_10_7 &&
               [NSScroller preferredScrollerStyle] == NSScrollerStyleOverlay) ?
               pixelMetric(PM_ScrollBarExtent, opt, widget) : 0;
#else
        ret = 0;
#endif
        break;
    default:
        ret = QCommonStyle::pixelMetric(metric, opt, widget);
        break;
    }
    return ret;
}

QPalette QMacStyle::standardPalette() const
{
    QPalette pal = QCommonStyle::standardPalette();
    pal.setColor(QPalette::Disabled, QPalette::Dark, QColor(191, 191, 191));
    pal.setColor(QPalette::Active, QPalette::Dark, QColor(191, 191, 191));
    pal.setColor(QPalette::Inactive, QPalette::Dark, QColor(191, 191, 191));
    return pal;
}

int QMacStyle::styleHint(StyleHint sh, const QStyleOption *opt, const QWidget *w,
                         QStyleHintReturn *hret) const
{
    SInt32 ret = 0;
    switch (sh) {
    case SH_Slider_SnapToValue:
    case SH_PrintDialog_RightAlignButtons:
    case SH_FontDialog_SelectAssociatedText:
    case SH_MenuBar_MouseTracking:
    case SH_Menu_MouseTracking:
    case SH_ComboBox_ListMouseTracking:
    case SH_MainWindow_SpaceBelowMenuBar:
    case SH_ItemView_ChangeHighlightOnFocus:
        ret = 1;
        break;
    case SH_ToolBox_SelectedPageTitleBold:
        ret = 0;
        break;
    case SH_DialogButtonBox_ButtonsHaveIcons:
        ret = 0;
        break;
    case SH_Menu_SelectionWrap:
        ret = false;
        break;
    case SH_Menu_KeyboardSearch:
        ret = true;
        break;
    case SH_Menu_SpaceActivatesItem:
        ret = true;
        break;
    case SH_Slider_AbsoluteSetButtons:
        ret = Qt::LeftButton|Qt::MidButton;
        break;
    case SH_Slider_PageSetButtons:
        ret = 0;
        break;
    case SH_ScrollBar_ContextMenu:
        ret = false;
        break;
    case SH_TitleBar_AutoRaise:
        ret = true;
        break;
    case SH_Menu_AllowActiveAndDisabled:
        ret = false;
        break;
    case SH_Menu_SubMenuPopupDelay:
        ret = 100;
        break;
    case SH_ScrollBar_LeftClickAbsolutePosition: {
        NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
        bool result = [defaults boolForKey:@"AppleScrollerPagingBehavior"];
        if(QApplication::keyboardModifiers() & Qt::AltModifier)
            ret = !result;
        else
            ret = result;
        break; }
    case SH_TabBar_PreferNoArrows:
        ret = true;
        break;
        /*
    case SH_DialogButtons_DefaultButton:
        ret = QDialogButtons::Reject;
        break;
        */
    case SH_GroupBox_TextLabelVerticalAlignment:
        ret = Qt::AlignTop;
        break;
    case SH_ScrollView_FrameOnlyAroundContents:
        ret = QCommonStyle::styleHint(sh, opt, w, hret);
        break;
    case SH_Menu_FillScreenWithScroll:
        ret = false;
        break;
    case SH_Menu_Scrollable:
        ret = true;
        break;
    case SH_RichText_FullWidthSelection:
        ret = true;
        break;
    case SH_BlinkCursorWhenTextSelected:
        ret = false;
        break;
    case SH_ScrollBar_StopMouseOverSlider:
        ret = true;
        break;
    case SH_ListViewExpand_SelectMouseType:
        ret = QEvent::MouseButtonRelease;
        break;
    case SH_TabBar_SelectMouseType:
        if (const QStyleOptionTabBarBaseV2 *opt2 = qstyleoption_cast<const QStyleOptionTabBarBaseV2 *>(opt)) {
            ret = opt2->documentMode ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease;
        } else {
            ret = QEvent::MouseButtonRelease;
        }
        break;
    case SH_ComboBox_Popup:
        if (const QStyleOptionComboBox *cmb = qstyleoption_cast<const QStyleOptionComboBox *>(opt))
            ret = !cmb->editable;
        else
            ret = 0;
        break;
    case SH_Workspace_FillSpaceOnMaximize:
        ret = true;
        break;
    case SH_Widget_ShareActivation:
        ret = true;
        break;
    case SH_Header_ArrowAlignment:
        ret = Qt::AlignRight;
        break;
    case SH_TabBar_Alignment: {
        if (const QTabWidget *tab = qobject_cast<const QTabWidget*>(w)) {
            if (tab->documentMode()) {
                ret = Qt::AlignLeft;
                break;
            }
        }
        if (const QTabBar *tab = qobject_cast<const QTabBar*>(w)) {
            if (tab->documentMode()) {
                ret = Qt::AlignLeft;
                break;
            }
        }
        ret = Qt::AlignCenter;
        } break;
    case SH_UnderlineShortcut:
        ret = false;
        break;
    case SH_ToolTipLabel_Opacity:
        ret = 242; // About 95%
        break;
    case SH_Button_FocusPolicy:
        ret = Qt::TabFocus;
        break;
    case SH_EtchDisabledText:
        ret = false;
        break;
    case SH_FocusFrame_Mask: {
        ret = true;
        if(QStyleHintReturnMask *mask = qstyleoption_cast<QStyleHintReturnMask*>(hret)) {
            const uchar fillR = 192, fillG = 191, fillB = 190;
            QImage img;

            QSize pixmapSize = opt->rect.size();
            if (pixmapSize.isValid()) {
                QPixmap pix(pixmapSize);
                pix.fill(QColor(fillR, fillG, fillB));
                QPainter pix_paint(&pix);
                proxy()->drawControl(CE_FocusFrame, opt, &pix_paint, w);
                pix_paint.end();
                img = pix.toImage();
            }

            const QRgb *sptr = (QRgb*)img.bits(), *srow;
            const int sbpl = img.bytesPerLine();
            const int w = sbpl/4, h = img.height();

            QImage img_mask(img.width(), img.height(), QImage::Format_ARGB32);
            QRgb *dptr = (QRgb*)img_mask.bits(), *drow;
            const int dbpl = img_mask.bytesPerLine();

            for (int y = 0; y < h; ++y) {
                srow = sptr+((y*sbpl)/4);
                drow = dptr+((y*dbpl)/4);
                for (int x = 0; x < w; ++x) {
                    const int redDiff = qRed(*srow) - fillR;
                    const int greenDiff = qGreen(*srow) - fillG;
                    const int blueDiff = qBlue(*srow) - fillB;
                    const int diff = (redDiff * redDiff) + (greenDiff * greenDiff) + (blueDiff * blueDiff);
                    (*drow++) = (diff < 10) ? 0xffffffff : 0xff000000;
                    ++srow;
                }
            }
            QBitmap qmask = QBitmap::fromImage(img_mask);
            mask->region = QRegion(qmask);
        }
        break; }
    case SH_TitleBar_NoBorder:
        ret = 1;
        break;
    case SH_RubberBand_Mask:
        ret = 0;
        break;
    case SH_ComboBox_LayoutDirection:
        ret = Qt::LeftToRight;
        break;
    case SH_ItemView_EllipsisLocation:
        ret = Qt::AlignHCenter;
        break;
    case SH_ItemView_ShowDecorationSelected:
        ret = true;
        break;
    case SH_TitleBar_ModifyNotification:
        ret = false;
        break;
    case SH_ScrollBar_RollBetweenButtons:
        ret = true;
        break;
    case SH_WindowFrame_Mask:
        ret = 1;
        if (QStyleHintReturnMask *mask = qstyleoption_cast<QStyleHintReturnMask *>(hret)) {
            mask->region = opt->rect;
            mask->region -= QRect(opt->rect.left(), opt->rect.top(), 5, 1);
            mask->region -= QRect(opt->rect.left(), opt->rect.top() + 1, 3, 1);
            mask->region -= QRect(opt->rect.left(), opt->rect.top() + 2, 2, 1);
            mask->region -= QRect(opt->rect.left(), opt->rect.top() + 3, 1, 2);

            mask->region -= QRect(opt->rect.right() - 4, opt->rect.top(), 5, 1);
            mask->region -= QRect(opt->rect.right() - 2, opt->rect.top() + 1, 3, 1);
            mask->region -= QRect(opt->rect.right() - 1, opt->rect.top() + 2, 2, 1);
            mask->region -= QRect(opt->rect.right() , opt->rect.top() + 3, 1, 2);
        }
        break;
    case SH_TabBar_ElideMode:
        ret = Qt::ElideRight;
        break;
    case SH_DialogButtonLayout:
        ret = QDialogButtonBox::MacLayout;
        break;
    case SH_FormLayoutWrapPolicy:
        ret = QFormLayout::DontWrapRows;
        break;
    case SH_FormLayoutFieldGrowthPolicy:
        ret = QFormLayout::FieldsStayAtSizeHint;
        break;
    case SH_FormLayoutFormAlignment:
        ret = Qt::AlignHCenter | Qt::AlignTop;
        break;
    case SH_FormLayoutLabelAlignment:
        ret = Qt::AlignRight;
        break;
    case SH_ComboBox_PopupFrameStyle:
        ret = QFrame::NoFrame | QFrame::Plain;
        break;
    case SH_MessageBox_TextInteractionFlags:
        ret = Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse | Qt::TextSelectableByKeyboard;
        break;
    case SH_SpellCheckUnderlineStyle:
        ret = QTextCharFormat::DashUnderline;
        break;
    case SH_MessageBox_CenterButtons:
        ret = false;
        break;
    case SH_MenuBar_AltKeyNavigation:
        ret = false;
        break;
    case SH_ItemView_MovementWithoutUpdatingSelection:
        ret = false;
        break;
    case SH_FocusFrame_AboveWidget:
        ret = true;
        break;
    case SH_WizardStyle:
        ret = QWizard::MacStyle;
        break;
    case SH_ItemView_ArrowKeysNavigateIntoChildren:
        ret = false;
        break;
    case SH_Menu_FlashTriggeredItem:
        ret = true;
        break;
    case SH_Menu_FadeOutOnHide:
        ret = true;
        break;
    case SH_Menu_Mask:
        if (opt) {
            if (QStyleHintReturnMask *mask = qstyleoption_cast<QStyleHintReturnMask*>(hret)) {
                ret = true;
                HIRect menuRect = CGRectMake(opt->rect.x(), opt->rect.y() + 4,
                                             opt->rect.width(), opt->rect.height() - 8);
                HIThemeMenuDrawInfo mdi;
                mdi.version = 0;
                if (w && qobject_cast<QMenu *>(w->parentWidget()))
                    mdi.menuType = kThemeMenuTypeHierarchical;
                else
                    mdi.menuType = kThemeMenuTypePopUp;
                QCFType<HIShapeRef> shape;
                HIThemeGetMenuBackgroundShape(&menuRect, &mdi, &shape);

                mask->region = qt_mac_fromHIShapeRef(shape);
            }
        }
        break;
    case SH_ItemView_PaintAlternatingRowColorsForEmptyArea:
        ret = true;
        break;
    case SH_TabBar_CloseButtonPosition:
        ret = QTabBar::LeftSide;
        break;
    case SH_DockWidget_ButtonsHaveFrame:
        ret = false;
        break;
    case SH_ScrollBar_Transient:
        if ((qobject_cast<const QScrollBar *>(w) && w->parent() &&
                qobject_cast<QAbstractScrollArea*>(w->parent()->parent())) ||
                (opt && QStyleHelper::hasAncestor(opt->styleObject, QAccessible::ScrollBar))) {
            ret = QSysInfo::MacintoshVersion >= QSysInfo::MV_10_7;
#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_7
            if (ret)
                ret = [NSScroller preferredScrollerStyle] == NSScrollerStyleOverlay;
#endif
        }
        break;
    default:
        ret = QCommonStyle::styleHint(sh, opt, w, hret);
        break;
    }
    return ret;
}

QPixmap QMacStyle::generatedIconPixmap(QIcon::Mode iconMode, const QPixmap &pixmap,
                                       const QStyleOption *opt) const
{
    switch (iconMode) {
    case QIcon::Disabled: {
        QImage img = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        int imgh = img.height();
        int imgw = img.width();
        QRgb pixel;
        for (int y = 0; y < imgh; ++y) {
            for (int x = 0; x < imgw; ++x) {
                pixel = img.pixel(x, y);
                img.setPixel(x, y, qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel),
                                         qAlpha(pixel) / 2));
            }
        }
        return QPixmap::fromImage(img);
    }
    default:
        ;
    }
    return QCommonStyle::generatedIconPixmap(iconMode, pixmap, opt);
}


QPixmap QMacStyle::standardPixmap(StandardPixmap standardPixmap, const QStyleOption *opt,
                                  const QWidget *widget) const
{
    // The default implementation of QStyle::standardIconImplementation() is to call standardPixmap()
    // I don't want infinite recursion so if we do get in that situation, just return the Window's
    // standard pixmap instead (since there is no mac-specific icon then). This should be fine until
    // someone changes how Windows standard
    // pixmap works.
    static bool recursionGuard = false;

    if (recursionGuard)
        return QCommonStyle::standardPixmap(standardPixmap, opt, widget);

    recursionGuard = true;
    QIcon icon = standardIcon(standardPixmap, opt, widget);
    recursionGuard = false;
    int size;
    switch (standardPixmap) {
        default:
            size = 32;
            break;
        case SP_MessageBoxCritical:
        case SP_MessageBoxQuestion:
        case SP_MessageBoxInformation:
        case SP_MessageBoxWarning:
            size = 64;
            break;
    }
    return icon.pixmap(size, size);
}

void QMacStyle::setFocusRectPolicy(QWidget *w, FocusRectPolicy policy)
{
    switch (policy) {
    case FocusDefault:
        break;
    case FocusEnabled:
    case FocusDisabled:
        w->setAttribute(Qt::WA_MacShowFocusRect, policy == FocusEnabled);
        break;
    }
}

QMacStyle::FocusRectPolicy QMacStyle::focusRectPolicy(const QWidget *w)
{
    return w->testAttribute(Qt::WA_MacShowFocusRect) ? FocusEnabled : FocusDisabled;
}

void QMacStyle::setWidgetSizePolicy(const QWidget *widget, WidgetSizePolicy policy)
{
    QWidget *wadget = const_cast<QWidget *>(widget);
    wadget->setAttribute(Qt::WA_MacNormalSize, policy == SizeLarge);
    wadget->setAttribute(Qt::WA_MacSmallSize, policy == SizeSmall);
    wadget->setAttribute(Qt::WA_MacMiniSize, policy == SizeMini);
}

QMacStyle::WidgetSizePolicy QMacStyle::widgetSizePolicy(const QWidget *widget, const QStyleOption *opt)
{
    while (widget) {
        if (widget->testAttribute(Qt::WA_MacMiniSize)) {
            return SizeMini;
        } else if (widget->testAttribute(Qt::WA_MacSmallSize)) {
            return SizeSmall;
        } else if (widget->testAttribute(Qt::WA_MacNormalSize)) {
            return SizeLarge;
        }
        widget = widget->parentWidget();
    }

    if (opt && opt->state & State_Mini)
        return SizeMini;
    else if (opt && opt->state & State_Small)
        return SizeSmall;

    return SizeDefault;
}

void QMacStyle::drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p,
                              const QWidget *w) const
{
    Q_D(const QMacStyle);
    ThemeDrawState tds = d->getDrawState(opt->state);
    QMacCGContext cg(p);
    switch (pe) {
    case PE_IndicatorArrowUp:
    case PE_IndicatorArrowDown:
    case PE_IndicatorArrowRight:
    case PE_IndicatorArrowLeft: {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        int xOffset = opt->direction == Qt::LeftToRight ? 2 : -1;
        QMatrix matrix;
        matrix.translate(opt->rect.center().x() + xOffset, opt->rect.center().y() + 2);
        QPainterPath path;
        switch(pe) {
        default:
        case PE_IndicatorArrowDown:
            break;
        case PE_IndicatorArrowUp:
            matrix.rotate(180);
            break;
        case PE_IndicatorArrowLeft:
            matrix.rotate(90);
            break;
        case PE_IndicatorArrowRight:
            matrix.rotate(-90);
            break;
        }
        path.moveTo(0, 5);
        path.lineTo(-4, -3);
        path.lineTo(4, -3);
        p->setMatrix(matrix);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 0, 0, 135));
        p->drawPath(path);
        p->restore();
        break; }
    case PE_FrameTabBarBase:
        if (const QStyleOptionTabBarBaseV2 *tbb
                = qstyleoption_cast<const QStyleOptionTabBarBaseV2 *>(opt)) {
            if (tbb->documentMode) {
                p->save();
                drawTabBase(p, tbb, w);
                p->restore();
                return;
            }

            QRegion region(tbb->rect);
            region -= tbb->tabBarRect;
            p->save();
            p->setClipRegion(region);
            QStyleOptionTabWidgetFrame twf;
            twf.QStyleOption::operator=(*tbb);
            twf.shape  = tbb->shape;
            switch (getTabDirection(twf.shape)) {
            case kThemeTabNorth:
                twf.rect = twf.rect.adjusted(0, 0, 0, 10);
                break;
            case kThemeTabSouth:
                twf.rect = twf.rect.adjusted(0, -10, 0, 0);
                break;
            case kThemeTabWest:
                twf.rect = twf.rect.adjusted(0, 0, 10, 0);
                break;
            case kThemeTabEast:
                twf.rect = twf.rect.adjusted(0, -10, 0, 0);
                break;
            }
            proxy()->drawPrimitive(PE_FrameTabWidget, &twf, p, w);
            p->restore();
        }
        break;
    case PE_PanelTipLabel:
        p->fillRect(opt->rect, opt->palette.brush(QPalette::ToolTipBase));
        break;
    case PE_FrameGroupBox:
        if (const QStyleOptionFrame *groupBox = qstyleoption_cast<const QStyleOptionFrame *>(opt)) {
            const QStyleOptionFrameV2 *frame2 = qstyleoption_cast<const QStyleOptionFrameV2 *>(opt);
            if (frame2 && frame2->features & QStyleOptionFrameV2::Flat) {
                QCommonStyle::drawPrimitive(pe, groupBox, p, w);
            } else {
                HIThemeGroupBoxDrawInfo gdi;
                gdi.version = qt_mac_hitheme_version;
                gdi.state = tds;
                if (w && qobject_cast<QGroupBox *>(w->parentWidget()))
                    gdi.kind = kHIThemeGroupBoxKindSecondary;
                else
                    gdi.kind = kHIThemeGroupBoxKindPrimary;
                HIRect hirect = qt_hirectForQRect(opt->rect);
                HIThemeDrawGroupBox(&hirect, &gdi, cg, kHIThemeOrientationNormal);
            }
        }
        break;
    case PE_IndicatorToolBarSeparator: {
            QPainterPath path;
            if (opt->state & State_Horizontal) {
                int xpoint = opt->rect.center().x();
                path.moveTo(xpoint + 0.5, opt->rect.top() + 1);
                path.lineTo(xpoint + 0.5, opt->rect.bottom());
            } else {
                int ypoint = opt->rect.center().y();
                path.moveTo(opt->rect.left() + 2 , ypoint + 0.5);
                path.lineTo(opt->rect.right() + 1, ypoint + 0.5);
            }
            QPainterPathStroker theStroker;
            theStroker.setCapStyle(Qt::FlatCap);
            theStroker.setDashPattern(QVector<qreal>() << 1 << 2);
            path = theStroker.createStroke(path);
            p->fillPath(path, QColor(0, 0, 0, 119));
        }
        break;
    case PE_FrameWindow:
        break;
    case PE_IndicatorDockWidgetResizeHandle: {
            // The docwidget resize handle is drawn as a one-pixel wide line.
            p->save();
            if (opt->state & State_Horizontal) {
                p->setPen(QColor(160, 160, 160));
                p->drawLine(opt->rect.topLeft(), opt->rect.topRight());
            } else {
                p->setPen(QColor(145, 145, 145));
                p->drawLine(opt->rect.topRight(), opt->rect.bottomRight());
            }
            p->restore();
        } break;
    case PE_IndicatorToolBarHandle: {
            p->save();
            QPainterPath path;
            int x = opt->rect.x() + 6;
            int y = opt->rect.y() + 7;
            static const int RectHeight = 2;
            if (opt->state & State_Horizontal) {
                while (y < opt->rect.height() - RectHeight - 5) {
                    path.moveTo(x, y);
                    path.addEllipse(x, y, RectHeight, RectHeight);
                    y += 6;
                }
            } else {
                while (x < opt->rect.width() - RectHeight - 5) {
                    path.moveTo(x, y);
                    path.addEllipse(x, y, RectHeight, RectHeight);
                    x += 6;
                }
            }
            p->setPen(Qt::NoPen);
            QColor dark = opt->palette.dark().color().darker();
            dark.setAlphaF(0.50);
            p->fillPath(path, dark);
            p->restore();

            break;
        }
    case PE_IndicatorHeaderArrow:
        if (const QStyleOptionHeader *header = qstyleoption_cast<const QStyleOptionHeader *>(opt)) {
            // In HITheme, up is down, down is up and hamburgers eat people.
            if (header->sortIndicator != QStyleOptionHeader::None)
                proxy()->drawPrimitive(
                    (header->sortIndicator == QStyleOptionHeader::SortDown) ?
                    PE_IndicatorArrowUp : PE_IndicatorArrowDown, header, p, w);
        }
        break;
    case PE_IndicatorMenuCheckMark: {
        const int checkw = 8;
        const int checkh = 8;
        const int xoff = qMax(0, (opt->rect.width() - checkw) / 2);
        const int yoff = qMax(0, (opt->rect.width() - checkh) / 2);
        const int x1 = xoff + opt->rect.x();
        const int y1 = yoff + opt->rect.y() + checkw/2;
        const int x2 = xoff + opt->rect.x() + checkw/4;
        const int y2 = yoff + opt->rect.y() + checkh;
        const int x3 = xoff + opt->rect.x() + checkw;
        const int y3 = yoff + opt->rect.y();

        QVector<QLineF> a(2);
        a << QLineF(x1, y1, x2, y2);
        a << QLineF(x2, y2, x3, y3);
        if (opt->palette.currentColorGroup() == QPalette::Active) {
            if (opt->state & State_On)
                p->setPen(QPen(opt->palette.highlightedText().color(), 3));
            else
                p->setPen(QPen(opt->palette.text().color(), 3));
        } else {
            p->setPen(QPen(QColor(100, 100, 100), 3));
        }
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->drawLines(a);
        p->restore();
        break; }
    case PE_IndicatorViewItemCheck:
    case PE_IndicatorRadioButton:
    case PE_IndicatorCheckBox: {
        bool drawColorless = tds == kThemeStateInactive;
        HIThemeButtonDrawInfo bdi;
        bdi.version = qt_mac_hitheme_version;
        bdi.state = tds;
        if (drawColorless)
            bdi.state = kThemeStateActive;
        bdi.adornment = kThemeDrawIndicatorOnly;
        if (opt->state & State_HasFocus)
            bdi.adornment |= kThemeAdornmentFocus;
        bool isRadioButton = (pe == PE_IndicatorRadioButton);
        switch (d->aquaSizeConstrain(opt, w)) {
        case QAquaSizeUnknown:
        case QAquaSizeLarge:
            if (isRadioButton)
                bdi.kind = kThemeRadioButton;
            else
                bdi.kind = kThemeCheckBox;
            break;
        case QAquaSizeMini:
            if (isRadioButton)
                bdi.kind = kThemeMiniRadioButton;
            else
                bdi.kind = kThemeMiniCheckBox;
            break;
        case QAquaSizeSmall:
            if (isRadioButton)
                bdi.kind = kThemeSmallRadioButton;
            else
                bdi.kind = kThemeSmallCheckBox;
            break;
        }
        if (opt->state & State_NoChange)
            bdi.value = kThemeButtonMixed;
        else if (opt->state & State_On)
            bdi.value = kThemeButtonOn;
        else
            bdi.value = kThemeButtonOff;
        HIRect macRect = qt_hirectForQRect(opt->rect);
        if (!drawColorless)
            HIThemeDrawButton(&macRect, &bdi, cg, kHIThemeOrientationNormal, 0);
        else
            d->drawColorlessButton(macRect, &bdi, p, opt);
        break; }
    case PE_FrameFocusRect:
        // Use the our own focus widget stuff.
        break;
    case PE_IndicatorBranch: {
        if (!(opt->state & State_Children))
            break;
        if (!d->indicatorBranchButtonCell)
            const_cast<QMacStylePrivate *>(d)->indicatorBranchButtonCell = (void *)[[NSButtonCell alloc] init];
        NSButtonCell *triangleCell = (NSButtonCell *)d->indicatorBranchButtonCell;
        [triangleCell setButtonType:NSOnOffButton];
        [triangleCell setState:(opt->state & State_Open) ? NSOnState : NSOffState];
        [triangleCell setBezelStyle:NSDisclosureBezelStyle];
        [triangleCell setBackgroundStyle:((opt->state & State_Selected) && w->hasFocus()) ? NSBackgroundStyleDark : NSBackgroundStyleLight];

        CGContextSaveGState(cg);
        [NSGraphicsContext saveGraphicsState];

        [NSGraphicsContext setCurrentContext:[NSGraphicsContext
                                              graphicsContextWithGraphicsPort:(CGContextRef)cg flipped:NO]];

        QRect qtRect = opt->rect.adjusted(DisclosureOffset, 0, -DisclosureOffset, 0);
        CGRect rect = CGRectMake(qtRect.x() + 1, qtRect.y(), qtRect.width(), qtRect.height());
        CGContextTranslateCTM(cg, rect.origin.x, rect.origin.y + rect.size.height);
        CGContextScaleCTM(cg, 1, -1);
        CGContextTranslateCTM(cg, -rect.origin.x, -rect.origin.y);

        [triangleCell drawBezelWithFrame:NSRectFromCGRect(rect) inView:[triangleCell controlView]];

        [NSGraphicsContext restoreGraphicsState];
        CGContextRestoreGState(cg);
        break; }

    case PE_Frame: {
        QPen oldPen = p->pen();
        p->setPen(opt->palette.base().color().darker(140));
        p->drawRect(opt->rect.adjusted(0, 0, -1, -1));
        p->setPen(opt->palette.base().color().darker(180));
        p->drawLine(opt->rect.topLeft(), opt->rect.topRight());
        p->setPen(oldPen);
        break; }

    case PE_FrameLineEdit:
        if (const QStyleOptionFrame *frame = qstyleoption_cast<const QStyleOptionFrame *>(opt)) {
            if (frame->state & State_Sunken) {
                QColor baseColor(frame->palette.background().color());
                HIThemeFrameDrawInfo fdi;
                fdi.version = qt_mac_hitheme_version;
                fdi.state = tds;
                SInt32 frame_size;
                fdi.kind = frame->features & QStyleOptionFrame::Rounded ? kHIThemeFrameTextFieldRound :
                                                                          kHIThemeFrameTextFieldSquare;
                GetThemeMetric(kThemeMetricEditTextFrameOutset, &frame_size);
                if ((frame->state & State_ReadOnly) || !(frame->state & State_Enabled))
                    fdi.state = kThemeStateInactive;
                else if (fdi.state == kThemeStatePressed)
                    // This pressed state doesn't make sense for a line edit frame.
                    // And Yosemite agrees with us. Otherwise it starts showing yellow pixels.
                    fdi.state = kThemeStateActive;
                fdi.isFocused = (frame->state & State_HasFocus);
                int lw = frame->lineWidth;
                if (lw <= 0)
                    lw = proxy()->pixelMetric(PM_DefaultFrameWidth, frame, w);
                { //clear to base color
                    p->save();
                    p->setPen(QPen(baseColor, lw));
                    p->setBrush(Qt::NoBrush);
                    p->drawRect(frame->rect);
                    p->restore();
                }
                HIRect hirect = qt_hirectForQRect(frame->rect,
                                                  QRect(frame_size, frame_size,
                                                        frame_size * 2, frame_size * 2));

                HIThemeDrawFrame(&hirect, &fdi, cg, kHIThemeOrientationNormal);
            } else {
                QCommonStyle::drawPrimitive(pe, opt, p, w);
            }
        }
        break;
    case PE_PanelLineEdit:
        QCommonStyle::drawPrimitive(pe, opt, p, w);
        // Draw the focus frame for widgets other than QLineEdit (e.g. for line edits in Webkit).
        // Focus frame is drawn outside the rectangle passed in the option-rect.
        if (const QStyleOptionFrame *panel = qstyleoption_cast<const QStyleOptionFrame *>(opt)) {
            if ((opt->state & State_HasFocus) && !qobject_cast<const QLineEdit*>(w)) {
                int vmargin = pixelMetric(QStyle::PM_FocusFrameVMargin);
                int hmargin = pixelMetric(QStyle::PM_FocusFrameHMargin);
                QStyleOptionFrame focusFrame = *panel;
                focusFrame.rect = panel->rect.adjusted(-hmargin, -vmargin, hmargin, vmargin);
                drawControl(CE_FocusFrame, &focusFrame, p, w);
            }
        }

        break;
    case PE_FrameTabWidget:
        if (const QStyleOptionTabWidgetFrame *twf
                = qstyleoption_cast<const QStyleOptionTabWidgetFrame *>(opt)) {
            HIRect hirect = qt_hirectForQRect(twf->rect);
            HIThemeTabPaneDrawInfo tpdi;
            tpdi.version = qt_mac_hitheme_tab_version();
            tpdi.state = tds;
            tpdi.direction = getTabDirection(twf->shape);
            tpdi.size = kHIThemeTabSizeNormal;
            tpdi.kind = kHIThemeTabKindNormal;
            tpdi.adornment = kHIThemeTabPaneAdornmentNormal;
            HIThemeDrawTabPane(&hirect, &tpdi, cg, kHIThemeOrientationNormal);
        }
        break;
    case PE_PanelScrollAreaCorner: {
        const QBrush brush(opt->palette.brush(QPalette::Base));
        p->fillRect(opt->rect, brush);
        p->setPen(QPen(QColor(217, 217, 217)));
        p->drawLine(opt->rect.topLeft(), opt->rect.topRight());
        p->drawLine(opt->rect.topLeft(), opt->rect.bottomLeft());
        } break;
    case PE_FrameStatusBarItem:
        break;
    case PE_IndicatorTabClose: {
        bool hover = (opt->state & State_MouseOver);
        bool selected = (opt->state & State_Selected);
        bool active = (opt->state & State_Active);
        drawTabCloseButton(p, hover, active, selected);
        } break;
    case PE_PanelStatusBar: {
        // Fill the status bar with the titlebar gradient.
        QLinearGradient linearGrad(0, opt->rect.top(), 0, opt->rect.bottom());
        if (w ? qt_macWindowMainWindow(w->window()) : (opt->state & QStyle::State_Active)) {
            linearGrad.setColorAt(0, titlebarGradientActiveBegin);
            linearGrad.setColorAt(1, titlebarGradientActiveEnd);
        } else {
            linearGrad.setColorAt(0, titlebarGradientInactiveBegin);
            linearGrad.setColorAt(1, titlebarGradientInactiveEnd);
        }
        p->fillRect(opt->rect, linearGrad);

        // Draw the black separator line at the top of the status bar.
        if (w ? qt_macWindowMainWindow(w->window()) : (opt->state & QStyle::State_Active))
            p->setPen(titlebarSeparatorLineActive);
        else
            p->setPen(titlebarSeparatorLineInactive);
        p->drawLine(opt->rect.left(), opt->rect.top(), opt->rect.right(), opt->rect.top());

        break;
    }

    default:
        QCommonStyle::drawPrimitive(pe, opt, p, w);
        break;
    }
}

static inline QPixmap darkenPixmap(const QPixmap &pixmap)
{
    QImage img = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    int imgh = img.height();
    int imgw = img.width();
    int h, s, v, a;
    QRgb pixel;
    for (int y = 0; y < imgh; ++y) {
        for (int x = 0; x < imgw; ++x) {
            pixel = img.pixel(x, y);
            a = qAlpha(pixel);
            QColor hsvColor(pixel);
            hsvColor.getHsv(&h, &s, &v);
            s = qMin(100, s * 2);
            v = v / 2;
            hsvColor.setHsv(h, s, v);
            pixel = hsvColor.rgb();
            img.setPixel(x, y, qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel), a));
        }
    }
    return QPixmap::fromImage(img);
}



void QMacStyle::drawControl(ControlElement ce, const QStyleOption *opt, QPainter *p,
                            const QWidget *w) const
{
    Q_D(const QMacStyle);
    ThemeDrawState tds = d->getDrawState(opt->state);
    QMacCGContext cg(p);
    switch (ce) {
    case CE_HeaderSection:
        if (const QStyleOptionHeader *header = qstyleoption_cast<const QStyleOptionHeader *>(opt)) {
            HIThemeButtonDrawInfo bdi;
            bdi.version = qt_mac_hitheme_version;
            State flags = header->state;
            QRect ir = header->rect;
            bdi.kind = kThemeListHeaderButton;
            bdi.adornment = kThemeAdornmentNone;
            bdi.state = kThemeStateActive;

            if (flags & State_On)
                bdi.value = kThemeButtonOn;
            else
                bdi.value = kThemeButtonOff;

            if (header->orientation == Qt::Horizontal){
                switch (header->position) {
                case QStyleOptionHeader::Beginning:
                    ir.adjust(-1, -1, 0, 0);
                    break;
                case QStyleOptionHeader::Middle:
                    ir.adjust(-1, -1, 0, 0);
                    break;
                case QStyleOptionHeader::OnlyOneSection:
                case QStyleOptionHeader::End:
                    ir.adjust(-1, -1, 1, 0);
                    break;
                default:
                    break;
                }

                if (header->position != QStyleOptionHeader::Beginning
                    && header->position != QStyleOptionHeader::OnlyOneSection) {
                    bdi.adornment = header->direction == Qt::LeftToRight
                        ? kThemeAdornmentHeaderButtonLeftNeighborSelected
                        : kThemeAdornmentHeaderButtonRightNeighborSelected;
                }
            }

            if (flags & State_Active) {
                if (!(flags & State_Enabled))
                    bdi.state = kThemeStateUnavailable;
                else if (flags & State_Sunken)
                    bdi.state = kThemeStatePressed;
            } else {
                if (flags & State_Enabled)
                    bdi.state = kThemeStateInactive;
                else
                    bdi.state = kThemeStateUnavailableInactive;
            }

            if (header->sortIndicator != QStyleOptionHeader::None) {
                bdi.value = kThemeButtonOn;
                if (header->sortIndicator == QStyleOptionHeader::SortDown)
                    bdi.adornment = kThemeAdornmentHeaderButtonSortUp;
            }
            if (flags & State_HasFocus)
                bdi.adornment = kThemeAdornmentFocus;

            ir = visualRect(header->direction, header->rect, ir);
            HIRect bounds = qt_hirectForQRect(ir);

            bool noVerticalHeader = true;
            if (w)
                if (const QTableView *table = qobject_cast<const QTableView *>(w->parentWidget()))
                    noVerticalHeader = !table->verticalHeader()->isVisible();

            bool drawTopBorder = header->orientation == Qt::Horizontal;
            bool drawLeftBorder = header->orientation == Qt::Vertical
                || header->position == QStyleOptionHeader::OnlyOneSection
                || (header->position == QStyleOptionHeader::Beginning && noVerticalHeader);
            d->drawTableHeader(bounds, drawTopBorder, drawLeftBorder, bdi, p);
        }
        break;
    case CE_HeaderLabel:
        if (const QStyleOptionHeader *header = qstyleoption_cast<const QStyleOptionHeader *>(opt)) {
            p->save();
            QRect textr = header->rect;
            if (!header->icon.isNull()) {
                QIcon::Mode mode = QIcon::Disabled;
                if (opt->state & State_Enabled)
                    mode = QIcon::Normal;
                QPixmap pixmap = header->icon.pixmap(proxy()->pixelMetric(PM_SmallIconSize), mode);

                QRect pixr = header->rect;
                pixr.setY(header->rect.center().y() - (pixmap.height() / pixmap.devicePixelRatio() - 1) / 2);
                proxy()->drawItemPixmap(p, pixr, Qt::AlignVCenter, pixmap);
                textr.translate(pixmap.width() / pixmap.devicePixelRatio() + 2, 0);
            }

            p->setFont(qt_app_fonts_hash()->value("QSmallFont", QFont()));
            proxy()->drawItemText(p, textr, header->textAlignment | Qt::AlignVCenter, header->palette,
                                       header->state & State_Enabled, header->text, QPalette::ButtonText);
            p->restore();
        }
        break;
    case CE_ToolButtonLabel:
        if (const QStyleOptionToolButton *tb = qstyleoption_cast<const QStyleOptionToolButton *>(opt)) {
            QStyleOptionToolButton myTb = *tb;
            myTb.state &= ~State_AutoRaise;
            if (QStyleHelper::hasAncestor(opt->styleObject, QAccessible::ToolBar)) {
                QRect cr = tb->rect;
                int shiftX = 0;
                int shiftY = 0;
                bool needText = false;
                int alignment = 0;
                bool down = tb->state & (State_Sunken | State_On);
                if (down) {
                    shiftX = proxy()->pixelMetric(PM_ButtonShiftHorizontal, tb, w);
                    shiftY = proxy()->pixelMetric(PM_ButtonShiftVertical, tb, w);
                }
                // The down state is special for QToolButtons in a toolbar on the Mac
                // The text is a bit bolder and gets a drop shadow and the icons are also darkened.
                // This doesn't really fit into any particular case in QIcon, so we
                // do the majority of the work ourselves.
                if (!(tb->features & QStyleOptionToolButton::Arrow)) {
                    Qt::ToolButtonStyle tbstyle = tb->toolButtonStyle;
                    if (tb->icon.isNull() && !tb->text.isEmpty())
                        tbstyle = Qt::ToolButtonTextOnly;

                    switch (tbstyle) {
                    case Qt::ToolButtonTextOnly: {
                        needText = true;
                        alignment = Qt::AlignCenter;
                        break; }
                    case Qt::ToolButtonIconOnly:
                    case Qt::ToolButtonTextBesideIcon:
                    case Qt::ToolButtonTextUnderIcon: {
                        QRect pr = cr;
                        QIcon::Mode iconMode = (tb->state & State_Enabled) ? QIcon::Normal
                                                                            : QIcon::Disabled;
                        QIcon::State iconState = (tb->state & State_On) ? QIcon::On
                                                                         : QIcon::Off;
                        QPixmap pixmap = tb->icon.pixmap(tb->rect.size().boundedTo(tb->iconSize), iconMode, iconState);

                        // Draw the text if it's needed.
                        if (tb->toolButtonStyle != Qt::ToolButtonIconOnly) {
                            needText = true;
                            if (tb->toolButtonStyle == Qt::ToolButtonTextUnderIcon) {
                                pr.setHeight(pixmap.size().height() / pixmap.devicePixelRatio() + 6);
                                cr.adjust(0, pr.bottom(), 0, -3);
                                alignment |= Qt::AlignCenter;
                            } else {
                                pr.setWidth(pixmap.width() / pixmap.devicePixelRatio() + 8);
                                cr.adjust(pr.right(), 0, 0, 0);
                                alignment |= Qt::AlignLeft | Qt::AlignVCenter;
                            }
                        }
                        if (opt->state & State_Sunken) {
                            pr.translate(shiftX, shiftY);
                            pixmap = darkenPixmap(pixmap);
                        }
                        proxy()->drawItemPixmap(p, pr, Qt::AlignCenter, pixmap);
                        break; }
                    default:
                        Q_ASSERT(false);
                        break;
                    }

                    if (needText) {
                        QPalette pal = tb->palette;
                        QPalette::ColorRole role = QPalette::NoRole;
                        if (!proxy()->styleHint(SH_UnderlineShortcut, tb, w))
                            alignment |= Qt::TextHideMnemonic;
                        if (down)
                            cr.translate(shiftX, shiftY);
                        if (QSysInfo::MacintoshVersion >= QSysInfo::MV_10_5
                            && (tbstyle == Qt::ToolButtonTextOnly
                                || (tbstyle != Qt::ToolButtonTextOnly && !down))) {
                            QPen pen = p->pen();
                            QColor light = down ? Qt::black : Qt::white;
                            light.setAlphaF(0.375f);
                            p->setPen(light);
                            p->drawText(cr.adjusted(0, 1, 0, 1), alignment, tb->text);
                            p->setPen(pen);
                            if (down && tbstyle == Qt::ToolButtonTextOnly) {
                                pal = QApplication::palette("QMenu");
                                pal.setCurrentColorGroup(tb->palette.currentColorGroup());
                                role = QPalette::HighlightedText;
                            }
                        }
                        proxy()->drawItemText(p, cr, alignment, pal,
                                              tb->state & State_Enabled, tb->text, role);
                        if (QSysInfo::MacintoshVersion < QSysInfo::MV_10_5 &&
                            (tb->state & State_Sunken)) {
                            // Draw a "drop shadow" in earlier versions.
                            proxy()->drawItemText(p, cr.adjusted(0, 1, 0, 1), alignment,
                                                  tb->palette, tb->state & State_Enabled, tb->text);
                        }
                    }
                } else {
                    QCommonStyle::drawControl(ce, &myTb, p, w);
                }
            } else {
                QCommonStyle::drawControl(ce, &myTb, p, w);
            }
        }
        break;
    case CE_ToolBoxTabShape:
        QCommonStyle::drawControl(ce, opt, p, w);
        break;
    case CE_PushButtonBevel:
        if (const QStyleOptionButton *btn = qstyleoption_cast<const QStyleOptionButton *>(opt)) {
            if (!(btn->state & (State_Raised | State_Sunken | State_On)))
                break;

            if (btn->features & QStyleOptionButton::CommandLinkButton) {
                QCommonStyle::drawControl(ce, opt, p, w);
                break;
            }

            // No default button pulsating animation on Yosemite,
            // so we have to do few things differently.
            const bool yosemiteOrLater = QSysInfo::QSysInfo::MacintoshVersion > QSysInfo::MV_10_9;

            // a focused auto-default button within an active window
            // takes precedence over a normal default button
            if (btn->features & QStyleOptionButton::AutoDefaultButton
                    && opt->state & State_Active && opt->state & State_HasFocus) {
                if (yosemiteOrLater)
                    d->autoDefaultButton = opt->styleObject;
                else
                    d->setAutoDefaultButton(opt->styleObject);
            } else if (d->autoDefaultButton == opt->styleObject) {
                d->setAutoDefaultButton(0);
            }

            if (!d->autoDefaultButton) {
                if (btn->features & QStyleOptionButton::DefaultButton && opt->state & State_Active) {
                    d->defaultButton = opt->styleObject;
                    if (!yosemiteOrLater && !d->animation(opt->styleObject))
                        d->startAnimation(new QStyleAnimation(opt->styleObject));
                } else if (d->defaultButton == opt->styleObject) {
                    if (QStyleAnimation *animation = d->animation(opt->styleObject)) {
                        animation->updateTarget();
                        d->stopAnimation(opt->styleObject);
                    }
                    d->defaultButton = 0;
                }
            }

            // TODO: find out the pressed button in a qwidget independent way
            extern QWidget *qt_button_down; // qwidgetwindow.cpp
            if (opt->styleObject == qt_button_down)
                d->pressedButton = opt->styleObject;
            else if (d->pressedButton == opt->styleObject)
                d->pressedButton = 0;

            HIThemeButtonDrawInfo bdi;
            d->initHIThemePushButton(btn, w, tds, &bdi);

            if (yosemiteOrLater) {
                // HITheme is not drawing a nice focus frame around buttons.
                // We'll do it ourselves further down.
                bdi.adornment &= ~kThemeAdornmentFocus;

                // We can't rely on an animation existing to test for the default look. That means a bit
                // more logic (notice that the logic is slightly different for the bevel and the label).
                if (tds == kThemeStateActive
                    && (btn->features & QStyleOptionButton::DefaultButton
                        || (btn->features & QStyleOptionButton::AutoDefaultButton
                            && d->autoDefaultButton == btn->styleObject)))
                    bdi.adornment |= kThemeAdornmentDefault;
            } else {
                // the default button animation is paused meanwhile any button
                // is pressed or an auto-default button is animated instead
                if (QStyleAnimation *defaultAnimation = d->animation(d->defaultButton)) {
                    if (d->pressedButton || d->autoDefaultButton) {
                        if (defaultAnimation->state() == QStyleAnimation::Running) {
                            defaultAnimation->pause();
                            defaultAnimation->updateTarget();
                        }
                    } else if (defaultAnimation->state() == QStyleAnimation::Paused) {
                        defaultAnimation->resume();
                    }
                }

                if (!d->pressedButton) {
                    QStyleAnimation* animation = d->animation(opt->styleObject);
                    if (animation && animation->state() == QStyleAnimation::Running) {
                        bdi.adornment |= kThemeAdornmentDefault;
                        bdi.animation.time.start = d->defaultButtonStart;
                        bdi.animation.time.current = CFAbsoluteTimeGetCurrent();
                    }
                }
            }

            // Unlike Carbon, we want the button to always be drawn inside its bounds.
            // Therefore, make the button a bit smaller, so that even if it got focus,
            // the focus 'shadow' will be inside.
            HIRect newRect = qt_hirectForQRect(btn->rect);
            if (bdi.kind == kThemePushButton || bdi.kind == kThemePushButtonSmall) {
                newRect.origin.x += QMacStylePrivate::PushButtonLeftOffset;
                newRect.origin.y += QMacStylePrivate::PushButtonTopOffset;
                newRect.size.width -= QMacStylePrivate::PushButtonRightOffset;
                newRect.size.height -= QMacStylePrivate::PushButtonBottomOffset;
            } else if (bdi.kind == kThemePushButtonMini) {
                newRect.origin.x += QMacStylePrivate::PushButtonLeftOffset - 2;
                newRect.origin.y += QMacStylePrivate::PushButtonTopOffset;
                newRect.size.width -= QMacStylePrivate::PushButtonRightOffset - 4;
            }

            bool hasMenu = btn->features & QStyleOptionButton::HasMenu;
            if (hasMenu && bdi.state == kThemeStatePressed && QSysInfo::macVersion() > QSysInfo::MV_10_6)
                d->drawColorlessButton(newRect, &bdi, p, opt);
            else
                HIThemeDrawButton(&newRect, &bdi, cg, kHIThemeOrientationNormal, 0);

            if (yosemiteOrLater && btn->state & State_HasFocus) {
                CGRect focusRect = newRect;
                if (bdi.kind == kThemePushButton)
                    focusRect.size.height += 1; // Another thing HITheme and Cocoa seem to disagree about.
                else if (bdi.kind == kThemePushButtonMini)
                    focusRect.size.height = 15; // Our QPushButton sizes are really weird

                if (bdi.adornment & kThemeAdornmentDefault || bdi.state == kThemeStatePressed) {
                    if (bdi.kind == kThemePushButtonSmall) {
                        focusRect = CGRectInset(focusRect, -1, 0);
                    } else if (bdi.kind == kThemePushButtonMini) {
                        focusRect = CGRectInset(focusRect, 1, 0);
                    }
                } else {
                    if (bdi.kind == kThemePushButton) {
                        focusRect = CGRectInset(focusRect, 1, 1);
                    } else if (bdi.kind == kThemePushButtonSmall) {
                        focusRect = CGRectInset(focusRect, 0, 2);
                    } else if (bdi.kind == kThemePushButtonMini) {
                        focusRect = CGRectInset(focusRect, 2, 1);
                    }
                }

                NSBezierPath *pushButtonFocusRingPath;
                if (bdi.kind == kThemeBevelButton)
                    pushButtonFocusRingPath = [NSBezierPath bezierPathWithRect:NSRectFromCGRect(focusRect)];
                else
                    pushButtonFocusRingPath = [NSBezierPath bezierPathWithRoundedRect:NSRectFromCGRect(focusRect) xRadius:4 yRadius:4];
                qt_drawFocusRingOnPath(cg, pushButtonFocusRingPath);
            }

            if (hasMenu) {
                int mbi = proxy()->pixelMetric(QStyle::PM_MenuButtonIndicator, btn, w);
                QRect ir = btn->rect;
                int arrowXOffset = 0;
                if (QSysInfo::macVersion() > QSysInfo::MV_10_6)
                    arrowXOffset = bdi.kind == kThemePushButton ? 6 :
                                   bdi.kind == kThemePushButtonSmall ? 7 : 8;
                int arrowYOffset;
                if (QSysInfo::macVersion() > QSysInfo::MV_10_6)
                    arrowYOffset = bdi.kind == kThemePushButton ? 3 :
                                   bdi.kind == kThemePushButtonSmall ? 1 : 2;
                else
                    arrowYOffset = bdi.kind == kThemePushButton ? 4 : 2;
                if (!w) {
                    // adjustment for Qt Quick Controls
                    arrowYOffset -= ir.top();
                    if (bdi.kind == kThemePushButtonSmall)
                        arrowYOffset += 1;
                }
                QRect ar = QRect(ir.right() - mbi - QMacStylePrivate::PushButtonRightOffset,
                                 ir.height() / 2 - arrowYOffset, mbi, ir.height() / 2);
                ar = visualRect(btn->direction, ir, ar);
                HIRect arrowRect = CGRectMake(ar.x() + arrowXOffset, ar.y(), ar.width(), ar.height());

                HIThemePopupArrowDrawInfo pdi;
                pdi.version = qt_mac_hitheme_version;
                pdi.state = tds == kThemeStateInactive ? kThemeStateActive : tds;
                pdi.orientation = kThemeArrowDown;
                if (QSysInfo::macVersion() > QSysInfo::MV_10_6) {
                    if (bdi.kind == kThemePushButtonMini)
                        pdi.size = kThemeArrow5pt;
                    else if (bdi.kind == kThemePushButton || bdi.kind == kThemePushButtonSmall)
                        pdi.size = kThemeArrow7pt;
                } else {
                    if (arrowRect.size.width < 8.)
                        pdi.size = kThemeArrow5pt;
                    else
                        pdi.size = kThemeArrow9pt;
                }
                HIThemeDrawPopupArrow(&arrowRect, &pdi, cg, kHIThemeOrientationNormal);
            }
        }
        break;
    case CE_PushButtonLabel:
        if (const QStyleOptionButton *b = qstyleoption_cast<const QStyleOptionButton *>(opt)) {
            QStyleOptionButton btn(*b);
            // We really don't want the label to be drawn the same as on
            // windows style if it has an icon and text, then it should be more like a
            // tab. So, cheat a little here. However, if it *is* only an icon
            // the windows style works great, so just use that implementation.
            bool hasMenu = btn.features & QStyleOptionButton::HasMenu;
            bool hasIcon = !btn.icon.isNull();
            bool hasText = !btn.text.isEmpty();

            if (QSysInfo::QSysInfo::MacintoshVersion > QSysInfo::MV_10_9) {
                if (tds == kThemeStatePressed
                    || (tds == kThemeStateActive
                        && ((btn.features & QStyleOptionButton::DefaultButton && !d->autoDefaultButton)
                            || d->autoDefaultButton == btn.styleObject)))
                btn.palette.setColor(QPalette::ButtonText, Qt::white);
            }

            if (!hasIcon && !hasMenu) {
                // ### this is really overly difficult, simplify.
                // It basically tries to get the right font for "small" and "mini" icons.
                QFont oldFont = p->font();
                QFont newFont = qt_app_fonts_hash()->value("QPushButton", QFont());
                ThemeFontID themeId = kThemePushButtonFont;
                if (oldFont == newFont) {  // Yes, use HITheme to draw the text for small sizes.
                    switch (d->aquaSizeConstrain(opt, w)) {
                    default:
                        break;
                    case QAquaSizeSmall:
                        themeId = kThemeSmallSystemFont;
                        break;
                    case QAquaSizeMini:
                        themeId = kThemeMiniSystemFont;
                        break;
                    }
                }

                if (themeId == kThemePushButtonFont) {
                    QCommonStyle::drawControl(ce, &btn, p, w);
                } else {
                    p->save();
                    CGContextSetShouldAntialias(cg, true);
                    CGContextSetShouldSmoothFonts(cg, true);
                    HIThemeTextInfo tti;
                    tti.version = qt_mac_hitheme_version;
                    tti.state = tds;
                    QColor textColor;
                    textColor = btn.palette.buttonText().color();
                    CGFloat colorComp[] = { static_cast<CGFloat>(textColor.redF()), static_cast<CGFloat>(textColor.greenF()),
                                          static_cast<CGFloat>(textColor.blueF()), static_cast<CGFloat>(textColor.alphaF()) };
                    CGContextSetFillColorSpace(cg, qt_mac_genericColorSpace());
                    CGContextSetFillColor(cg, colorComp);
                    tti.fontID = themeId;
                    tti.horizontalFlushness = kHIThemeTextHorizontalFlushCenter;
                    tti.verticalFlushness = kHIThemeTextVerticalFlushCenter;
                    tti.options = kHIThemeTextBoxOptionNone;
                    tti.truncationPosition = kHIThemeTextTruncationNone;
                    tti.truncationMaxLines = 1 + btn.text.count(QLatin1Char('\n'));
                    QCFString buttonText = qt_mac_removeMnemonics(btn.text);
                    QRect r = btn.rect;
                    HIRect bounds = qt_hirectForQRect(r);
                    HIThemeDrawTextBox(buttonText, &bounds, &tti,
                                       cg, kHIThemeOrientationNormal);
                    p->restore();
                }
            } else {
                if (hasIcon && !hasText) {
                    QCommonStyle::drawControl(ce, &btn, p, w);
                } else {
                    QRect freeContentRect = btn.rect;
                    QRect textRect = itemTextRect(
                        btn.fontMetrics, freeContentRect, Qt::AlignCenter, btn.state & State_Enabled, btn.text);
                    if (hasMenu) {
                        if (QSysInfo::macVersion() > QSysInfo::MV_10_6)
                            textRect.moveTo(w ? 15 : 11, textRect.top()); // Supports Qt Quick Controls
                        else
                            textRect.adjust(-1, 0, -1, 0);
                    }
                    // Draw the icon:
                    if (hasIcon) {
                        int contentW = textRect.width();
                        if (hasMenu)
                            contentW += proxy()->pixelMetric(PM_MenuButtonIndicator) + 4;
                        QIcon::Mode mode = btn.state & State_Enabled ? QIcon::Normal : QIcon::Disabled;
                        if (mode == QIcon::Normal && btn.state & State_HasFocus)
                            mode = QIcon::Active;
                        // Decide if the icon is should be on or off:
                        QIcon::State state = QIcon::Off;
                        if (btn.state & State_On)
                            state = QIcon::On;
                        QPixmap pixmap = btn.icon.pixmap(btn.iconSize, mode, state);
                        int pixmapWidth = pixmap.width() / pixmap.devicePixelRatio();
                        int pixmapHeight = pixmap.height() / pixmap.devicePixelRatio();
                        contentW += pixmapWidth + QMacStylePrivate::PushButtonContentPadding;
                        int iconLeftOffset = freeContentRect.x() + (freeContentRect.width() - contentW) / 2;
                        int iconTopOffset = freeContentRect.y() + (freeContentRect.height() - pixmapHeight) / 2;
                        QRect iconDestRect(iconLeftOffset, iconTopOffset, pixmapWidth, pixmapHeight);
                        QRect visualIconDestRect = visualRect(btn.direction, freeContentRect, iconDestRect);
                        proxy()->drawItemPixmap(p, visualIconDestRect, Qt::AlignLeft | Qt::AlignVCenter, pixmap);
                        int newOffset = iconDestRect.x() + iconDestRect.width()
                                        + QMacStylePrivate::PushButtonContentPadding - textRect.x();
                        textRect.adjust(newOffset, 0, newOffset, 0);
                    }
                    // Draw the text:
                    if (hasText) {
                        textRect = visualRect(btn.direction, freeContentRect, textRect);
                        proxy()->drawItemText(p, textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic, btn.palette,
                                                   (btn.state & State_Enabled), btn.text, QPalette::ButtonText);
                    }
                }
            }
        }
        break;
    case CE_ComboBoxLabel:
        if (const QStyleOptionComboBox *cb = qstyleoption_cast<const QStyleOptionComboBox *>(opt)) {
            QStyleOptionComboBox comboCopy = *cb;
            comboCopy.direction = Qt::LeftToRight;
            if ((opt->state & QStyle::State_Small) && QSysInfo::macVersion() > QSysInfo::MV_10_6)
                comboCopy.rect.translate(0, w ? (QSysInfo::macVersion() > QSysInfo::MV_10_8 ? 0 : -1) : -2); // Supports Qt Quick Controls
            else if (QSysInfo::macVersion() > QSysInfo::MV_10_8)
                comboCopy.rect.translate(0, 1);
            QCommonStyle::drawControl(CE_ComboBoxLabel, &comboCopy, p, w);
        }
        break;
    case CE_TabBarTabShape:
        if (const QStyleOptionTab *tabOpt = qstyleoption_cast<const QStyleOptionTab *>(opt)) {

            if (const QStyleOptionTabV3 *tabOptV3 = qstyleoption_cast<const QStyleOptionTabV3 *>(opt)) {
                if (tabOptV3->documentMode) {
                    p->save();
                    bool isUnified = false;
                    if (w) {
                        QRect tabRect = tabOptV3->rect;
                        QPoint windowTabStart = w->mapTo(w->window(), tabRect.topLeft());
                        isUnified = isInMacUnifiedToolbarArea(w->window()->windowHandle(), windowTabStart.y());
                    }

                    drawTabShape(p, tabOptV3, isUnified);
                    p->restore();
                    return;
                }
            }

            bool usingYosemiteOrLater = QSysInfo::MacintoshVersion > QSysInfo::MV_10_9;

            HIThemeTabDrawInfo tdi;
            tdi.version = 1;
            tdi.style = kThemeTabNonFront;
            tdi.direction = getTabDirection(tabOpt->shape);
            switch (d->aquaSizeConstrain(opt, w)) {
            default:
            case QAquaSizeUnknown:
            case QAquaSizeLarge:
                tdi.size = kHIThemeTabSizeNormal;
                break;
            case QAquaSizeSmall:
                tdi.size = kHIThemeTabSizeSmall;
                break;
            case QAquaSizeMini:
                tdi.size = kHIThemeTabSizeMini;
                break;
            }
            bool verticalTabs = tdi.direction == kThemeTabWest || tdi.direction == kThemeTabEast;
            QRect tabRect = tabOpt->rect;

            bool selected = tabOpt->state & State_Selected;
            if (selected) {
                if (!(tabOpt->state & State_Active))
                    tdi.style = kThemeTabFrontUnavailable;
                else if (!(tabOpt->state & State_Enabled))
                    tdi.style = kThemeTabFrontInactive;
                else
                    tdi.style = kThemeTabFront;
            } else if (!(tabOpt->state & State_Active)) {
                tdi.style = kThemeTabNonFrontUnavailable;
            } else if (!(tabOpt->state & State_Enabled)) {
                tdi.style = kThemeTabNonFrontInactive;
            } else if (tabOpt->state & State_Sunken) {
                tdi.style = kThemeTabNonFrontPressed;
            }
            if (tabOpt->state & State_HasFocus)
                tdi.adornment = kHIThemeTabAdornmentFocus;
            else
                tdi.adornment = kHIThemeTabAdornmentNone;
            tdi.kind = kHIThemeTabKindNormal;

            if (!usingYosemiteOrLater) {
                if (!verticalTabs)
                    tabRect.setY(tabRect.y() - 1);
                else
                    tabRect.setX(tabRect.x() - 1);
            }
            QStyleOptionTab::TabPosition tp = tabOpt->position;
            QStyleOptionTab::SelectedPosition sp = tabOpt->selectedPosition;
            if (tabOpt->direction == Qt::RightToLeft && !verticalTabs) {
                if (sp == QStyleOptionTab::NextIsSelected)
                    sp = QStyleOptionTab::PreviousIsSelected;
                else if (sp == QStyleOptionTab::PreviousIsSelected)
                    sp = QStyleOptionTab::NextIsSelected;
                switch (tp) {
                case QStyleOptionTab::Beginning:
                    tp = QStyleOptionTab::End;
                    break;
                case QStyleOptionTab::End:
                    tp = QStyleOptionTab::Beginning;
                    break;
                default:
                    break;
                }
            }
            bool stretchTabs = (!verticalTabs && tabRect.height() > 22) || (verticalTabs && tabRect.width() > 22);

            switch (tp) {
            case QStyleOptionTab::Beginning:
                tdi.position = kHIThemeTabPositionFirst;
                if (sp != QStyleOptionTab::NextIsSelected || stretchTabs)
                    tdi.adornment |= kHIThemeTabAdornmentTrailingSeparator;
                break;
            case QStyleOptionTab::Middle:
                tdi.position = kHIThemeTabPositionMiddle;
                if (selected)
                    tdi.adornment |= kHIThemeTabAdornmentLeadingSeparator;
                if (sp != QStyleOptionTab::NextIsSelected || stretchTabs)  // Also when we're selected.
                    tdi.adornment |= kHIThemeTabAdornmentTrailingSeparator;
                break;
            case QStyleOptionTab::End:
                tdi.position = kHIThemeTabPositionLast;
                if (selected)
                    tdi.adornment |= kHIThemeTabAdornmentLeadingSeparator;
                break;
            case QStyleOptionTab::OnlyOneTab:
                tdi.position = kHIThemeTabPositionOnly;
                break;
            }
            // HITheme doesn't stretch its tabs. Therefore we have to cheat and do the job ourselves.
            if (stretchTabs) {
                HIRect hirect = CGRectMake(0, 0, 23, 23);
                QPixmap pm(23, 23);
                pm.fill(Qt::transparent);
                {
                    QMacCGContext pmcg(&pm);
                    HIThemeDrawTab(&hirect, &tdi, pmcg, kHIThemeOrientationNormal, 0);
                }
                QStyleHelper::drawBorderPixmap(pm, p, tabRect, 7, 7, 7, 7);
            } else {
                HIRect hirect = qt_hirectForQRect(tabRect);
                HIThemeDrawTab(&hirect, &tdi, cg, kHIThemeOrientationNormal, 0);
            }
        }
        break;
    case CE_TabBarTabLabel:
        if (const QStyleOptionTab *tab = qstyleoption_cast<const QStyleOptionTab *>(opt)) {
            QStyleOptionTabV3 myTab = *tab;
            ThemeTabDirection ttd = getTabDirection(myTab.shape);
            bool verticalTabs = ttd == kThemeTabWest || ttd == kThemeTabEast;
            bool selected = (myTab.state & QStyle::State_Selected);
            bool usingLionOrLater = QSysInfo::MacintoshVersion > QSysInfo::MV_10_6;
            bool usingYosemiteOrLater = QSysInfo::MacintoshVersion > QSysInfo::MV_10_9;

            if (usingLionOrLater && selected && !myTab.documentMode
                && (!usingYosemiteOrLater || myTab.state & State_Active))
                myTab.palette.setColor(QPalette::WindowText, Qt::white);

            // Check to see if we use have the same as the system font
            // (QComboMenuItem is internal and should never be seen by the
            // outside world, unless they read the source, in which case, it's
            // their own fault).
            bool nonDefaultFont = p->font() != qt_app_fonts_hash()->value("QComboMenuItem");
            bool isSelectedAndNeedsShadow = selected && usingLionOrLater && !usingYosemiteOrLater;
            if (isSelectedAndNeedsShadow || verticalTabs || nonDefaultFont || !tab->icon.isNull()
                || !myTab.leftButtonSize.isEmpty() || !myTab.rightButtonSize.isEmpty()) {
                int heightOffset = 0;
                if (verticalTabs) {
                    heightOffset = -1;
                } else if (nonDefaultFont) {
                    if (p->fontMetrics().height() == myTab.rect.height())
                        heightOffset = 2;
                }
                myTab.rect.setHeight(myTab.rect.height() + heightOffset);

                if (myTab.documentMode || isSelectedAndNeedsShadow) {
                    p->save();
                    rotateTabPainter(p, myTab.shape, myTab.rect);

                    QColor shadowColor = QColor(myTab.documentMode ? Qt::white : Qt::black);
                    shadowColor.setAlpha(75);
                    QPalette np = tab->palette;
                    np.setColor(QPalette::WindowText, shadowColor);

                    QRect nr = subElementRect(SE_TabBarTabText, opt, w);
                    nr.moveTop(-1);
                    int alignment = Qt::AlignCenter | Qt::TextShowMnemonic | Qt::TextHideMnemonic;
                    proxy()->drawItemText(p, nr, alignment, np, tab->state & State_Enabled,
                                               tab->text, QPalette::WindowText);
                    p->restore();
                }

                QCommonStyle::drawControl(ce, &myTab, p, w);
            } else {
                p->save();
                CGContextSetShouldAntialias(cg, true);
                CGContextSetShouldSmoothFonts(cg, true);
                HIThemeTextInfo tti;
                tti.version = qt_mac_hitheme_version;
                tti.state = tds;
                QColor textColor = myTab.palette.windowText().color();
                CGFloat colorComp[] = { static_cast<CGFloat>(textColor.redF()), static_cast<CGFloat>(textColor.greenF()),
                                        static_cast<CGFloat>(textColor.blueF()), static_cast<CGFloat>(textColor.alphaF()) };
                CGContextSetFillColorSpace(cg, qt_mac_genericColorSpace());
                CGContextSetFillColor(cg, colorComp);
                switch (d->aquaSizeConstrain(opt, w)) {
                default:
                case QAquaSizeUnknown:
                case QAquaSizeLarge:
                    tti.fontID = kThemeSystemFont;
                    break;
                case QAquaSizeSmall:
                    tti.fontID = kThemeSmallSystemFont;
                    break;
                case QAquaSizeMini:
                    tti.fontID = kThemeMiniSystemFont;
                    break;
                }
                tti.horizontalFlushness = kHIThemeTextHorizontalFlushCenter;
                tti.verticalFlushness = kHIThemeTextVerticalFlushCenter;
                tti.options = verticalTabs ? kHIThemeTextBoxOptionStronglyVertical : kHIThemeTextBoxOptionNone;
                tti.truncationPosition = kHIThemeTextTruncationNone;
                tti.truncationMaxLines = 1 + myTab.text.count(QLatin1Char('\n'));
                QCFString tabText = qt_mac_removeMnemonics(myTab.text);
                QRect r = myTab.rect.adjusted(0, 0, 0, -1);
                HIRect bounds = qt_hirectForQRect(r);
                HIThemeDrawTextBox(tabText, &bounds, &tti, cg, kHIThemeOrientationNormal);
                p->restore();
            }
        }
        break;
    case CE_DockWidgetTitle:
        if (const QDockWidget *dockWidget = qobject_cast<const QDockWidget *>(w)) {
            bool floating = dockWidget->isFloating();
            if (floating) {
                ThemeDrawState tds = d->getDrawState(opt->state);
                HIThemeWindowDrawInfo wdi;
                wdi.version = qt_mac_hitheme_version;
                wdi.state = tds;
                wdi.windowType = kThemeMovableDialogWindow;
                wdi.titleHeight = opt->rect.height();
                wdi.titleWidth = opt->rect.width();
                wdi.attributes = 0;

                HIRect titleBarRect;
                HIRect tmpRect = qt_hirectForQRect(opt->rect);
                {
                    QCFType<HIShapeRef> titleRegion;
                    QRect newr = opt->rect.adjusted(0, 0, 2, 0);
                    HIThemeGetWindowShape(&tmpRect, &wdi, kWindowTitleBarRgn, &titleRegion);
                    ptrHIShapeGetBounds(titleRegion, &tmpRect);
                    newr.translate(newr.x() - int(tmpRect.origin.x), newr.y() - int(tmpRect.origin.y));
                    titleBarRect = qt_hirectForQRect(newr);
                }
                QMacCGContext cg(p);
                HIThemeDrawWindowFrame(&titleBarRect, &wdi, cg, kHIThemeOrientationNormal, 0);
            } else {
                // fill title bar background
                QLinearGradient linearGrad(0, opt->rect.top(), 0, opt->rect.bottom());
                linearGrad.setColorAt(0, mainWindowGradientBegin);
                linearGrad.setColorAt(1, mainWindowGradientEnd);
                p->fillRect(opt->rect, linearGrad);

                // draw horizontal lines at top and bottom
                p->save();
                p->setPen(mainWindowGradientBegin.lighter(114));
                p->drawLine(opt->rect.topLeft(), opt->rect.topRight());
                p->setPen(mainWindowGradientEnd.darker(114));
                p->drawLine(opt->rect.bottomLeft(), opt->rect.bottomRight());
                p->restore();
            }
        }

        // Draw the text...
        if (const QStyleOptionDockWidget *dwOpt = qstyleoption_cast<const QStyleOptionDockWidget *>(opt)) {
            if (!dwOpt->title.isEmpty()) {
                const QStyleOptionDockWidgetV2 *v2
                    = qstyleoption_cast<const QStyleOptionDockWidgetV2*>(dwOpt);
                bool verticalTitleBar = v2 == 0 ? false : v2->verticalTitleBar;

                QRect titleRect = subElementRect(SE_DockWidgetTitleBarText, opt, w);
                if (verticalTitleBar) {
                    QRect rect = dwOpt->rect;
                    QRect r = rect;
                    QSize s = r.size();
                    s.transpose();
                    r.setSize(s);

                    titleRect = QRect(r.left() + rect.bottom()
                                        - titleRect.bottom(),
                                    r.top() + titleRect.left() - rect.left(),
                                    titleRect.height(), titleRect.width());

                    p->translate(r.left(), r.top() + r.width());
                    p->rotate(-90);
                    p->translate(-r.left(), -r.top());
                }

                QFont oldFont = p->font();
                p->setFont(qt_app_fonts_hash()->value("QToolButton", p->font()));
                QString text = p->fontMetrics().elidedText(dwOpt->title, Qt::ElideRight,
                    titleRect.width());
                drawItemText(p, titleRect,
                              Qt::AlignCenter | Qt::TextShowMnemonic, dwOpt->palette,
                              dwOpt->state & State_Enabled, text,
                              QPalette::WindowText);
                p->setFont(oldFont);
            }
        }
        break;
    case CE_FocusFrame: {
        int xOff = proxy()->pixelMetric(PM_FocusFrameHMargin, opt, w);
        int yOff = proxy()->pixelMetric(PM_FocusFrameVMargin, opt, w);
        NSRect rect = NSMakeRect(xOff+opt->rect.x(), yOff+opt->rect.y(), opt->rect.width() - 2 * xOff,
                                 opt->rect.height() - 2 * yOff);
        NSBezierPath *focusFramePath = [NSBezierPath bezierPathWithRect:rect];
        qt_drawFocusRingOnPath(cg, focusFramePath);
        break; }
    case CE_MenuItem:
    case CE_MenuEmptyArea:
        if (const QStyleOptionMenuItem *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt)) {
            p->fillRect(mi->rect, opt->palette.background());
            QAquaWidgetSize widgetSize = d->aquaSizeConstrain(opt, w);
            int tabwidth = mi->tabWidth;
            int maxpmw = mi->maxIconWidth;
            bool active = mi->state & State_Selected;
            bool enabled = mi->state & State_Enabled;
            HIRect menuRect = qt_hirectForQRect(mi->menuRect);
            HIRect itemRect = qt_hirectForQRect(mi->rect);
            HIThemeMenuItemDrawInfo mdi;
            mdi.version = qt_mac_hitheme_version;
            mdi.itemType = kThemeMenuItemPlain;
            if (!mi->icon.isNull())
                mdi.itemType |= kThemeMenuItemHasIcon;
            if (mi->menuItemType == QStyleOptionMenuItem::SubMenu)
                mdi.itemType |= kThemeMenuItemHierarchical | kThemeMenuItemHierBackground;
            else
                mdi.itemType |= kThemeMenuItemPopUpBackground;
            if (enabled)
                mdi.state = kThemeMenuActive;
            else
                mdi.state = kThemeMenuDisabled;
            if (active)
                mdi.state |= kThemeMenuSelected;
            QRect contentRect;
            if (mi->menuItemType == QStyleOptionMenuItem::Separator) {
                // First arg should be &menurect, but wacky stuff happens then.
                HIThemeDrawMenuSeparator(&itemRect, &itemRect, &mdi,
                                         cg, kHIThemeOrientationNormal);
                break;
            } else {
                HIRect cr;
                bool needAlpha = mi->palette.color(QPalette::Button) == Qt::transparent;
                if (needAlpha) {
                    CGContextSaveGState(cg);
                    CGContextSetAlpha(cg, 0.0);
                }
                HIThemeDrawMenuItem(&menuRect, &itemRect, &mdi,
                                    cg, kHIThemeOrientationNormal, &cr);
                if (needAlpha)
                    CGContextRestoreGState(cg);
                if (ce == CE_MenuEmptyArea)
                    break;
                contentRect = qt_qrectForHIRect(cr);
            }
            int xpos = contentRect.x() + 18;
            int checkcol = maxpmw;
            if (!enabled)
                p->setPen(mi->palette.text().color());
            else if (active)
                p->setPen(mi->palette.highlightedText().color());
            else
                p->setPen(mi->palette.buttonText().color());

            if (mi->checked) {
                // Use the HIThemeTextInfo foo to draw the check mark correctly, if we do it,
                // we somehow need to use a special encoding as it doesn't look right with our
                // drawText().
                p->save();
                CGContextSetShouldAntialias(cg, true);
                CGContextSetShouldSmoothFonts(cg, true);
                QColor textColor = p->pen().color();
                CGFloat colorComp[] = { static_cast<CGFloat>(textColor.redF()), static_cast<CGFloat>(textColor.greenF()),
                                      static_cast<CGFloat>(textColor.blueF()), static_cast<CGFloat>(textColor.alphaF()) };
                CGContextSetFillColorSpace(cg, qt_mac_genericColorSpace());
                CGContextSetFillColor(cg, colorComp);
                HIThemeTextInfo tti;
                tti.version = qt_mac_hitheme_version;
                tti.state = tds;
                if (active && enabled)
                    tti.state = kThemeStatePressed;
                switch (widgetSize) {
                case QAquaSizeUnknown:
                case QAquaSizeLarge:
                    tti.fontID = kThemeMenuItemMarkFont;
                    break;
                case QAquaSizeSmall:
                    tti.fontID = kThemeSmallSystemFont;
                    break;
                case QAquaSizeMini:
                    tti.fontID = kThemeMiniSystemFont;
                    break;
                }
                tti.horizontalFlushness = kHIThemeTextHorizontalFlushLeft;
                tti.verticalFlushness = kHIThemeTextVerticalFlushCenter;
                tti.options = kHIThemeTextBoxOptionNone;
                tti.truncationPosition = kHIThemeTextTruncationNone;
                tti.truncationMaxLines = 1;
                QCFString checkmark;
#if 0
                if (mi->checkType == QStyleOptionMenuItem::Exclusive)
                    checkmark = QString(QChar(kDiamondUnicode));
                else
#endif
                    checkmark = QString(QChar(kCheckUnicode));
                int mw = checkcol + macItemFrame;
                int mh = contentRect.height() - 2 * macItemFrame;
                int xp = contentRect.x();
                xp += macItemFrame;
                CGFloat outWidth, outHeight, outBaseline;
                HIThemeGetTextDimensions(checkmark, 0, &tti, &outWidth, &outHeight,
                                         &outBaseline);
                if (widgetSize == QAquaSizeMini)
                    outBaseline += 1;
                QRect r(xp, contentRect.y(), mw, mh);
                r.translate(0, p->fontMetrics().ascent() - int(outBaseline) + 1);
                HIRect bounds = qt_hirectForQRect(r);
                HIThemeDrawTextBox(checkmark, &bounds, &tti,
                                   cg, kHIThemeOrientationNormal);
                p->restore();
            }
            if (!mi->icon.isNull()) {
                QIcon::Mode mode = (mi->state & State_Enabled) ? QIcon::Normal
                                                               : QIcon::Disabled;
                // Always be normal or disabled to follow the Mac style.
                int smallIconSize = proxy()->pixelMetric(PM_SmallIconSize);
                QSize iconSize(smallIconSize, smallIconSize);
                if (const QComboBox *comboBox = qobject_cast<const QComboBox *>(w)) {
                    iconSize = comboBox->iconSize();
                }
                QPixmap pixmap = mi->icon.pixmap(iconSize, mode);
                int pixw = pixmap.width() / pixmap.devicePixelRatio();
                int pixh = pixmap.height() / pixmap.devicePixelRatio();
                QRect cr(xpos, contentRect.y(), checkcol, contentRect.height());
                QRect pmr(0, 0, pixw, pixh);
                pmr.moveCenter(cr.center());
                p->drawPixmap(pmr.topLeft(), pixmap);
                xpos += pixw + 6;
            }

            QString s = mi->text;
            if (!s.isEmpty()) {
                int t = s.indexOf(QLatin1Char('\t'));
                int text_flags = Qt::AlignRight | Qt::AlignVCenter | Qt::TextHideMnemonic
                                 | Qt::TextSingleLine | Qt::AlignAbsolute;
                int yPos = contentRect.y();
                if (widgetSize == QAquaSizeMini)
                    yPos += 1;
                p->save();
                if (t >= 0) {
                    p->setFont(qt_app_fonts_hash()->value("QMenuItem", p->font()));
                    int xp = contentRect.right() - tabwidth - macRightBorder
                             - macItemHMargin - macItemFrame + 1;
                    p->drawText(xp, yPos, tabwidth, contentRect.height(), text_flags,
                                s.mid(t + 1));
                    s = s.left(t);
                }

                const int xm = macItemFrame + maxpmw + macItemHMargin;
                QFont myFont = mi->font;
                // myFont may not have any "hard" flags set. We override
                // the point size so that when it is resolved against the device, this font will win.
                // This is mainly to handle cases where someone sets the font on the window
                // and then the combo inherits it and passes it onward. At that point the resolve mask
                // is very, very weak. This makes it stonger.
                myFont.setPointSizeF(QFontInfo(mi->font).pointSizeF());
                p->setFont(myFont);
                p->drawText(xpos, yPos, contentRect.width() - xm - tabwidth + 1,
                            contentRect.height(), text_flags ^ Qt::AlignRight, s);
                p->restore();
            }
        }
        break;
    case CE_MenuHMargin:
    case CE_MenuVMargin:
    case CE_MenuTearoff:
    case CE_MenuScroller:
        if (const QStyleOptionMenuItem *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt)) {
            p->fillRect(mi->rect, opt->palette.background());

            HIRect menuRect = qt_hirectForQRect(mi->menuRect);
            HIRect itemRect = qt_hirectForQRect(mi->rect);
            HIThemeMenuItemDrawInfo mdi;
            mdi.version = qt_mac_hitheme_version;
            if (!(opt->state & State_Enabled))
                mdi.state = kThemeMenuDisabled;
            else if (opt->state & State_Selected)
                mdi.state = kThemeMenuSelected;
            else
                mdi.state = kThemeMenuActive;
            if (ce == CE_MenuScroller) {
                if (opt->state & State_DownArrow)
                    mdi.itemType = kThemeMenuItemScrollDownArrow;
                else
                    mdi.itemType = kThemeMenuItemScrollUpArrow;
            } else {
                mdi.itemType = kThemeMenuItemPlain;
            }
            HIThemeDrawMenuItem(&menuRect, &itemRect, &mdi,
                                cg,
                                kHIThemeOrientationNormal, 0);
            if (ce == CE_MenuTearoff) {
                p->setPen(QPen(mi->palette.dark().color(), 1, Qt::DashLine));
                p->drawLine(mi->rect.x() + 2, mi->rect.y() + mi->rect.height() / 2 - 1,
                            mi->rect.x() + mi->rect.width() - 4,
                            mi->rect.y() + mi->rect.height() / 2 - 1);
                p->setPen(QPen(mi->palette.light().color(), 1, Qt::DashLine));
                p->drawLine(mi->rect.x() + 2, mi->rect.y() + mi->rect.height() / 2,
                            mi->rect.x() + mi->rect.width() - 4,
                            mi->rect.y() + mi->rect.height() / 2);
            }
        }
        break;
    case CE_MenuBarItem:
        if (const QStyleOptionMenuItem *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt)) {
            HIRect menuRect = qt_hirectForQRect(mi->menuRect);
            HIRect itemRect = qt_hirectForQRect(mi->rect);

            if ((opt->state & State_Selected) && (opt->state & State_Enabled) && (opt->state & State_Sunken)){
                // Draw a selected menu item background:
                HIThemeMenuItemDrawInfo mdi;
                mdi.version = qt_mac_hitheme_version;
                mdi.state = kThemeMenuSelected;
                mdi.itemType = kThemeMenuItemPlain;
                HIThemeDrawMenuItem(&menuRect, &itemRect, &mdi, cg, kHIThemeOrientationNormal, 0);
            } else {
                // Draw the toolbar background:
                HIThemeMenuBarDrawInfo bdi;
                bdi.version = qt_mac_hitheme_version;
                bdi.state = kThemeMenuBarNormal;
                bdi.attributes = 0;
                HIThemeDrawMenuBarBackground(&menuRect, &bdi, cg, kHIThemeOrientationNormal);
            }

            if (!mi->icon.isNull()) {
                drawItemPixmap(p, mi->rect,
                                  Qt::AlignCenter | Qt::TextHideMnemonic | Qt::TextDontClip
                                  | Qt::TextSingleLine,
                                  mi->icon.pixmap(proxy()->pixelMetric(PM_SmallIconSize),
                          (mi->state & State_Enabled) ? QIcon::Normal : QIcon::Disabled));
            } else {
                drawItemText(p, mi->rect,
                                Qt::AlignCenter | Qt::TextHideMnemonic | Qt::TextDontClip
                                | Qt::TextSingleLine,
                                mi->palette, mi->state & State_Enabled,
                                mi->text, QPalette::ButtonText);
            }
        }
        break;
    case CE_MenuBarEmptyArea:
        if (const QStyleOptionMenuItem *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt)) {
            HIThemeMenuBarDrawInfo bdi;
            bdi.version = qt_mac_hitheme_version;
            bdi.state = kThemeMenuBarNormal;
            bdi.attributes = 0;
            HIRect hirect = qt_hirectForQRect(mi->rect);
            HIThemeDrawMenuBarBackground(&hirect, &bdi, cg,
                                         kHIThemeOrientationNormal);
            break;
        }
    case CE_ProgressBarContents:
        if (const QStyleOptionProgressBar *pb = qstyleoption_cast<const QStyleOptionProgressBar *>(opt)) {
            HIThemeTrackDrawInfo tdi;
            tdi.version = qt_mac_hitheme_version;
            tdi.reserved = 0;
            bool isIndeterminate = (pb->minimum == 0 && pb->maximum == 0);
            bool vertical = false;
            bool inverted = false;
            if (const QStyleOptionProgressBarV2 *pb2 = qstyleoption_cast<const QStyleOptionProgressBarV2 *>(opt)) {
                vertical = (pb2->orientation == Qt::Vertical);
                inverted = pb2->invertedAppearance;
            }
            bool reverse = (!vertical && (pb->direction == Qt::RightToLeft));
            if (inverted)
                reverse = !reverse;
            switch (d->aquaSizeConstrain(opt, w)) {
            case QAquaSizeUnknown:
            case QAquaSizeLarge:
                tdi.kind = !isIndeterminate ? kThemeLargeProgressBar
                                            : kThemeLargeIndeterminateBar;
                break;
            case QAquaSizeMini:
            case QAquaSizeSmall:
                tdi.kind = !isIndeterminate ? kThemeProgressBar : kThemeIndeterminateBar;
                break;
            }
            tdi.bounds = qt_hirectForQRect(pb->rect);
            tdi.max = pb->maximum;
            tdi.min = pb->minimum;
            tdi.value = pb->progress;
            tdi.attributes = vertical ? 0 : kThemeTrackHorizontal;

            if (isIndeterminate || tdi.value < tdi.max) {
                if (QProgressStyleAnimation *animation = qobject_cast<QProgressStyleAnimation*>(d->animation(opt->styleObject)))
                    tdi.trackInfo.progress.phase = animation->animationStep();
                else if (opt->styleObject)
                    d->startAnimation(new QProgressStyleAnimation(d->animateSpeed(QMacStylePrivate::AquaProgressBar), opt->styleObject));
            } else {
                d->stopAnimation(opt->styleObject);
            }
            if (!(pb->state & State_Active))
                tdi.enableState = kThemeTrackInactive;
            else if (!(pb->state & State_Enabled))
                tdi.enableState = kThemeTrackDisabled;
            else
                tdi.enableState = kThemeTrackActive;
            HIThemeOrientation drawOrientation = kHIThemeOrientationNormal;
            if (reverse) {
                if (vertical) {
                    drawOrientation = kHIThemeOrientationInverted;
                } else {
                    CGContextSaveGState(cg);
                    CGContextTranslateCTM(cg, pb->rect.width(), 0);
                    CGContextScaleCTM(cg, -1, 1);
                }
            }
            HIThemeDrawTrack(&tdi, 0, cg, drawOrientation);
            if (reverse && !vertical)
                CGContextRestoreGState(cg);
        }
        break;
    case CE_ProgressBarLabel:
    case CE_ProgressBarGroove:
        break;
    case CE_SizeGrip: {
        // We do not draw size grips on versions > 10.6 unless it's a QMdiSubWindow
        if (QSysInfo::MacintoshVersion > QSysInfo::MV_10_6
#ifndef QT_NO_MDIAREA
            && !(w && w->parentWidget() && qobject_cast<QMdiSubWindow *>(w->parentWidget()))
#endif
            )
            break;

        if (w && w->testAttribute(Qt::WA_MacOpaqueSizeGrip)) {
            HIThemeGrowBoxDrawInfo gdi;
            gdi.version = qt_mac_hitheme_version;
            gdi.state = tds;
            gdi.kind = kHIThemeGrowBoxKindNormal;
            gdi.direction = kThemeGrowRight | kThemeGrowDown;
            gdi.size = kHIThemeGrowBoxSizeNormal;
            HIPoint pt = CGPointMake(opt->rect.x(), opt->rect.y());
            HIThemeDrawGrowBox(&pt, &gdi, cg, kHIThemeOrientationNormal);
        } else {
            // It isn't possible to draw a transparent size grip with the
            // native API, so we do it ourselves here.
            const bool metal = qt_mac_is_metal(w);
            QPen lineColor = metal ? QColor(236, 236, 236) : QColor(82, 82, 82, 192);
            QPen metalHighlight = QColor(5, 5, 5, 192);
            lineColor.setWidth(1);
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(lineColor);
            const Qt::LayoutDirection layoutDirection = w ? w->layoutDirection() : qApp->layoutDirection();
            const int NumLines = metal ? 4 : 3;
            for (int l = 0; l < NumLines; ++l) {
                const int offset = (l * 4 + (metal ? 2 : 3));
                QPoint start, end;
                if (layoutDirection == Qt::LeftToRight) {
                    start = QPoint(opt->rect.width() - offset, opt->rect.height() - 1);
                    end = QPoint(opt->rect.width() - 1, opt->rect.height() - offset);
                } else {
                    start = QPoint(offset, opt->rect.height() - 1);
                    end = QPoint(1, opt->rect.height() - offset);
                }
                p->drawLine(start, end);
                if (metal) {
                    p->setPen(metalHighlight);
                    p->setRenderHint(QPainter::Antialiasing, false);
                    p->drawLine(start + QPoint(0, -1), end + QPoint(0, -1));
                    p->setRenderHint(QPainter::Antialiasing, true);
                    p->setPen(lineColor);
                }
            }
            p->restore();
        }
        break;
        }
    case CE_Splitter:
        if (opt->rect.width() > 1 && opt->rect.height() > 1){
            HIThemeSplitterDrawInfo sdi;
            sdi.version = qt_mac_hitheme_version;
            sdi.state = tds;
            sdi.adornment = qt_mac_is_metal(w) ? kHIThemeSplitterAdornmentMetal
                                               : kHIThemeSplitterAdornmentNone;
            HIRect hirect = qt_hirectForQRect(opt->rect);
            HIThemeDrawPaneSplitter(&hirect, &sdi, cg, kHIThemeOrientationNormal);
        } else {
            QPen oldPen = p->pen();
            p->setPen(opt->palette.dark().color());
            if (opt->state & QStyle::State_Horizontal)
                p->drawLine(opt->rect.topLeft(), opt->rect.bottomLeft());
            else
                p->drawLine(opt->rect.topLeft(), opt->rect.topRight());
            p->setPen(oldPen);
        }
        break;
    case CE_RubberBand:
        if (const QStyleOptionRubberBand *rubber = qstyleoption_cast<const QStyleOptionRubberBand *>(opt)) {
            QColor fillColor(opt->palette.color(QPalette::Disabled, QPalette::Highlight));
            if (!rubber->opaque) {
                QColor strokeColor;
                // I retrieved these colors from the Carbon-Dev mailing list
                strokeColor.setHsvF(0, 0, 0.86, 1.0);
                fillColor.setHsvF(0, 0, 0.53, 0.25);
                if (opt->rect.width() * opt->rect.height() <= 3) {
                    p->fillRect(opt->rect, strokeColor);
                } else {
                    QPen oldPen = p->pen();
                    QBrush oldBrush = p->brush();
                    QPen pen(strokeColor);
                    p->setPen(pen);
                    p->setBrush(fillColor);
                    QRect adjusted = opt->rect.adjusted(1, 1, -1, -1);
                    if (adjusted.isValid())
                        p->drawRect(adjusted);
                    p->setPen(oldPen);
                    p->setBrush(oldBrush);
                }
            } else {
                p->fillRect(opt->rect, fillColor);
            }
        }
        break;
    case CE_ToolBar: {
        const QStyleOptionToolBar *toolBar = qstyleoption_cast<const QStyleOptionToolBar *>(opt);

        // Unified title and toolbar drawing. In this mode the cocoa platform plugin will
        // fill the top toolbar area part with a background gradient that "unifies" with
        // the title bar. The following code fills the toolBar area with transparent pixels
        // to make that gradient visible.
        if (w)  {
            if (QMainWindow * mainWindow = qobject_cast<QMainWindow *>(w->window())) {
                if (toolBar && toolBar->toolBarArea == Qt::TopToolBarArea && mainWindow->unifiedTitleAndToolBarOnMac()) {

                    // fill with transparent pixels.
                    p->save();
                    p->setCompositionMode(QPainter::CompositionMode_Source);
                    p->fillRect(opt->rect, Qt::transparent);
                    p->restore();

                    // Drow a horizontal sepearator line at the toolBar bottom if the "unified" area ends here.
                    // There might be additional toolbars or other widgets such as tab bars in document
                    // mode below. Determine this by making a unified toolbar area test for the row below
                    // this toolbar.
                    QPoint windowToolbarEnd = w->mapTo(w->window(), opt->rect.bottomLeft());
                    bool isEndOfUnifiedArea = !isInMacUnifiedToolbarArea(w->window()->windowHandle(), windowToolbarEnd.y() + 1);
                    if (isEndOfUnifiedArea) {
                        SInt32 margin;
                        GetThemeMetric(kThemeMetricSeparatorSize, &margin);
                        CGRect separatorRect = CGRectMake(opt->rect.left(), opt->rect.bottom(), opt->rect.width(), margin);
                        HIThemeSeparatorDrawInfo separatorDrawInfo;
                        separatorDrawInfo.version = 0;
                        separatorDrawInfo.state = qt_macWindowMainWindow(mainWindow) ? kThemeStateActive : kThemeStateInactive;
                        QMacCGContext cg(p);
                        HIThemeDrawSeparator(&separatorRect, &separatorDrawInfo, cg, kHIThemeOrientationNormal);
                    }
                    break;
                }
            }
        }

        // draw background gradient
        QLinearGradient linearGrad;
        if (opt->state & State_Horizontal)
            linearGrad = QLinearGradient(0, opt->rect.top(), 0, opt->rect.bottom());
        else
            linearGrad = QLinearGradient(opt->rect.left(), 0,  opt->rect.right(), 0);

        linearGrad.setColorAt(0, mainWindowGradientBegin);
        linearGrad.setColorAt(1, mainWindowGradientEnd);
        p->fillRect(opt->rect, linearGrad);

        p->save();
        if (opt->state & State_Horizontal) {
            p->setPen(mainWindowGradientBegin.lighter(114));
            p->drawLine(opt->rect.topLeft(), opt->rect.topRight());
            p->setPen(mainWindowGradientEnd.darker(114));
            p->drawLine(opt->rect.bottomLeft(), opt->rect.bottomRight());

        } else {
            p->setPen(mainWindowGradientBegin.lighter(114));
            p->drawLine(opt->rect.topLeft(), opt->rect.bottomLeft());
            p->setPen(mainWindowGradientEnd.darker(114));
            p->drawLine(opt->rect.topRight(), opt->rect.bottomRight());
        }
        p->restore();


        } break;
    default:
        QCommonStyle::drawControl(ce, opt, p, w);
        break;
    }
}

static void setLayoutItemMargins(int left, int top, int right, int bottom, QRect *rect, Qt::LayoutDirection dir)
{
    if (dir == Qt::RightToLeft) {
        rect->adjust(-right, top, -left, bottom);
    } else {
        rect->adjust(left, top, right, bottom);
    }
}

QRect QMacStyle::subElementRect(SubElement sr, const QStyleOption *opt,
                                const QWidget *widget) const
{
    Q_D(const QMacStyle);
    QRect rect;
    int controlSize = getControlSize(opt, widget);

    switch (sr) {
    case SE_ItemViewItemText:
        if (const QStyleOptionViewItem *vopt = qstyleoption_cast<const QStyleOptionViewItem *>(opt)) {
            int fw = proxy()->pixelMetric(PM_FocusFrameHMargin, opt, widget);
            // We add the focusframeargin between icon and text in commonstyle
            rect = QCommonStyle::subElementRect(sr, opt, widget);
            if (vopt->features & QStyleOptionViewItem::HasDecoration)
                rect.adjust(-fw, 0, 0, 0);
        }
        break;
    case SE_ToolBoxTabContents:
        rect = QCommonStyle::subElementRect(sr, opt, widget);
        break;
    case SE_PushButtonContents:
        if (const QStyleOptionButton *btn = qstyleoption_cast<const QStyleOptionButton *>(opt)) {
            // Unlike Carbon, we want the button to always be drawn inside its bounds.
            // Therefore, the button is a bit smaller, so that even if it got focus,
            // the focus 'shadow' will be inside. Adjust the content rect likewise.
            HIThemeButtonDrawInfo bdi;
            d->initHIThemePushButton(btn, widget, d->getDrawState(opt->state), &bdi);
            HIRect contentRect = d->pushButtonContentBounds(btn, &bdi);
            rect = qt_qrectForHIRect(contentRect);
        }
        break;
    case SE_HeaderLabel: {
        int margin = proxy()->pixelMetric(QStyle::PM_HeaderMargin, opt, widget);
        rect.setRect(opt->rect.x() + margin, opt->rect.y(),
                  opt->rect.width() - margin * 2, opt->rect.height() - 2);
        if (const QStyleOptionHeader *header = qstyleoption_cast<const QStyleOptionHeader *>(opt)) {
            // Subtract width needed for arrow, if there is one
            if (header->sortIndicator != QStyleOptionHeader::None) {
                if (opt->state & State_Horizontal)
                    rect.setWidth(rect.width() - (opt->rect.height() / 2) - (margin * 2));
                else
                    rect.setHeight(rect.height() - (opt->rect.width() / 2) - (margin * 2));
            }
        }
        rect = visualRect(opt->direction, opt->rect, rect);
        break;
    }
    case SE_ProgressBarGroove:
    case SE_ProgressBarLabel:
        break;
    case SE_ProgressBarContents:
        rect = opt->rect;
        break;
    case SE_TreeViewDisclosureItem: {
        HIRect inRect = CGRectMake(opt->rect.x(), opt->rect.y(),
                                   opt->rect.width(), opt->rect.height());
        HIThemeButtonDrawInfo bdi;
        bdi.version = qt_mac_hitheme_version;
        bdi.state = kThemeStateActive;
        bdi.kind = kThemeDisclosureButton;
        bdi.value = kThemeDisclosureRight;
        bdi.adornment = kThemeAdornmentNone;
        HIRect contentRect;
        HIThemeGetButtonContentBounds(&inRect, &bdi, &contentRect);
        QCFType<HIShapeRef> shape;
        HIRect outRect;
        HIThemeGetButtonShape(&inRect, &bdi, &shape);
        ptrHIShapeGetBounds(shape, &outRect);
        rect = QRect(int(outRect.origin.x + DisclosureOffset), int(outRect.origin.y),
                  int(contentRect.origin.x - outRect.origin.x + DisclosureOffset),
                  int(outRect.size.height));
        break;
    }
    case SE_TabWidgetLeftCorner:
        if (const QStyleOptionTabWidgetFrame *twf
                = qstyleoption_cast<const QStyleOptionTabWidgetFrame *>(opt)) {
            switch (twf->shape) {
            case QTabBar::RoundedNorth:
            case QTabBar::TriangularNorth:
                rect = QRect(QPoint(0, 0), twf->leftCornerWidgetSize);
                break;
            case QTabBar::RoundedSouth:
            case QTabBar::TriangularSouth:
                rect = QRect(QPoint(0, twf->rect.height() - twf->leftCornerWidgetSize.height()),
                          twf->leftCornerWidgetSize);
                break;
            default:
                break;
            }
            rect = visualRect(twf->direction, twf->rect, rect);
        }
        break;
    case SE_TabWidgetRightCorner:
        if (const QStyleOptionTabWidgetFrame *twf
                = qstyleoption_cast<const QStyleOptionTabWidgetFrame *>(opt)) {
            switch (twf->shape) {
            case QTabBar::RoundedNorth:
            case QTabBar::TriangularNorth:
                rect = QRect(QPoint(twf->rect.width() - twf->rightCornerWidgetSize.width(), 0),
                          twf->rightCornerWidgetSize);
                break;
            case QTabBar::RoundedSouth:
            case QTabBar::TriangularSouth:
                rect = QRect(QPoint(twf->rect.width() - twf->rightCornerWidgetSize.width(),
                                 twf->rect.height() - twf->rightCornerWidgetSize.height()),
                          twf->rightCornerWidgetSize);
                break;
            default:
                break;
            }
            rect = visualRect(twf->direction, twf->rect, rect);
        }
        break;
    case SE_TabWidgetTabContents:
        rect = QCommonStyle::subElementRect(sr, opt, widget);
        if (const QStyleOptionTabWidgetFrame *twf
                = qstyleoption_cast<const QStyleOptionTabWidgetFrame *>(opt)) {
            if (twf->lineWidth != 0) {
                switch (getTabDirection(twf->shape)) {
                case kThemeTabNorth:
                    rect.adjust(+1, +14, -1, -1);
                    break;
                case kThemeTabSouth:
                    rect.adjust(+1, +1, -1, -14);
                    break;
                case kThemeTabWest:
                    rect.adjust(+14, +1, -1, -1);
                    break;
                case kThemeTabEast:
                    rect.adjust(+1, +1, -14, -1);
                }
            }
        }
        break;
    case SE_LineEditContents:
        rect = QCommonStyle::subElementRect(sr, opt, widget);
        if (widget && qobject_cast<const QComboBox*>(widget->parentWidget()))
            rect.adjust(-1, -2, 0, 0);
        else
            rect.adjust(-1, -1, 0, +1);
        break;
    case SE_CheckBoxLayoutItem:
        rect = opt->rect;
        if (controlSize == QAquaSizeLarge) {
            setLayoutItemMargins(+2, +3, -9, -4, &rect, opt->direction);
        } else if (controlSize == QAquaSizeSmall) {
            setLayoutItemMargins(+1, +5, 0 /* fix */, -6, &rect, opt->direction);
        } else {
            setLayoutItemMargins(0, +7, 0 /* fix */, -6, &rect, opt->direction);
        }
        break;
    case SE_ComboBoxLayoutItem:
        if (widget && qobject_cast<QToolBar *>(widget->parentWidget())) {
            // Do nothing, because QToolbar needs the entire widget rect.
            // Otherwise it will be clipped. Equivalent to
            // widget->setAttribute(Qt::WA_LayoutUsesWidgetRect), but without
            // all the hassle.
        } else {
            rect = opt->rect;
            if (controlSize == QAquaSizeLarge) {
                rect.adjust(+3, +2, -3, -4);
            } else if (controlSize == QAquaSizeSmall) {
                setLayoutItemMargins(+2, +1, -3, -4, &rect, opt->direction);
            } else {
                setLayoutItemMargins(+1, 0, -2, 0, &rect, opt->direction);
            }
        }
        break;
    case SE_LabelLayoutItem:
        rect = opt->rect;
        setLayoutItemMargins(+1, 0 /* SHOULD be -1, done for alignment */, 0, 0 /* SHOULD be -1, done for alignment */, &rect, opt->direction);
        break;
    case SE_ProgressBarLayoutItem: {
        rect = opt->rect;
        int bottom = SIZE(3, 8, 8);
        if (opt->state & State_Horizontal) {
            rect.adjust(0, +1, 0, -bottom);
        } else {
            setLayoutItemMargins(+1, 0, -bottom, 0, &rect, opt->direction);
        }
        break;
    }
    case SE_PushButtonLayoutItem:
        if (const QStyleOptionButton *buttonOpt
                = qstyleoption_cast<const QStyleOptionButton *>(opt)) {
            if ((buttonOpt->features & QStyleOptionButton::Flat))
                break;  // leave rect alone
        }
        rect = opt->rect;
        if (controlSize == QAquaSizeLarge) {
            rect.adjust(+6, +4, -6, -8);
        } else if (controlSize == QAquaSizeSmall) {
            rect.adjust(+5, +4, -5, -6);
        } else {
            rect.adjust(+1, 0, -1, -2);
        }
        break;
    case SE_RadioButtonLayoutItem:
        rect = opt->rect;
        if (controlSize == QAquaSizeLarge) {
            setLayoutItemMargins(+2, +2 /* SHOULD BE +3, done for alignment */,
                                 0, -4 /* SHOULD BE -3, done for alignment */, &rect, opt->direction);
        } else if (controlSize == QAquaSizeSmall) {
            rect.adjust(0, +6, 0 /* fix */, -5);
        } else {
            rect.adjust(0, +6, 0 /* fix */, -7);
        }
        break;
    case SE_SliderLayoutItem:
        if (const QStyleOptionSlider *sliderOpt
                = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
            rect = opt->rect;
            if (sliderOpt->tickPosition == QSlider::NoTicks) {
                int above = SIZE(3, 0, 2);
                int below = SIZE(4, 3, 0);
                if (sliderOpt->orientation == Qt::Horizontal) {
                    rect.adjust(0, +above, 0, -below);
                } else {
                    rect.adjust(+above, 0, -below, 0);  //### Seems that QSlider flip the position of the ticks in reverse mode.
                }
            } else if (sliderOpt->tickPosition == QSlider::TicksAbove) {
                int below = SIZE(3, 2, 0);
                if (sliderOpt->orientation == Qt::Horizontal) {
                    rect.setHeight(rect.height() - below);
                } else {
                    rect.setWidth(rect.width() - below);
                }
            } else if (sliderOpt->tickPosition == QSlider::TicksBelow) {
                int above = SIZE(3, 2, 0);
                if (sliderOpt->orientation == Qt::Horizontal) {
                    rect.setTop(rect.top() + above);
                } else {
                    rect.setLeft(rect.left() + above);
                }
            }
        }
        break;
    case SE_FrameLayoutItem:
        // hack because QStyleOptionFrameV2 doesn't have a frameStyle member
        if (const QFrame *frame = qobject_cast<const QFrame *>(widget)) {
            rect = opt->rect;
            switch (frame->frameStyle() & QFrame::Shape_Mask) {
            case QFrame::HLine:
                rect.adjust(0, +1, 0, -1);
                break;
            case QFrame::VLine:
                rect.adjust(+1, 0, -1, 0);
                break;
            default:
                ;
            }
        }
        break;
    case SE_GroupBoxLayoutItem:
        rect = opt->rect;
        if (const QStyleOptionGroupBox *groupBoxOpt =
                qstyleoption_cast<const QStyleOptionGroupBox *>(opt)) {
            /*
                AHIG is very inconsistent when it comes to group boxes.
                Basically, we make sure that (non-checkable) group boxes
                and tab widgets look good when laid out side by side.
            */
            if (groupBoxOpt->subControls & (QStyle::SC_GroupBoxCheckBox
                                            | QStyle::SC_GroupBoxLabel)) {
                int delta;
                if (groupBoxOpt->subControls & QStyle::SC_GroupBoxCheckBox) {
                    delta = SIZE(8, 4, 4);       // guess
                } else {
                    delta = SIZE(15, 12, 12);    // guess
                }
                rect.setTop(rect.top() + delta);
            }
        }
        rect.setBottom(rect.bottom() - 1);
        break;
    case SE_TabWidgetLayoutItem:
        if (const QStyleOptionTabWidgetFrame *tabWidgetOpt =
                qstyleoption_cast<const QStyleOptionTabWidgetFrame *>(opt)) {
            /*
                AHIG specifies "12 or 14" as the distance from the window
                edge. We choose 14 and since the default top margin is 20,
                the overlap is 6.
            */
            rect = tabWidgetOpt->rect;
            if (tabWidgetOpt->shape == QTabBar::RoundedNorth)
                rect.setTop(rect.top() + SIZE(6 /* AHIG */, 3 /* guess */, 2 /* AHIG */));
        }
        break;
#ifndef QT_NO_DOCKWIDGET
        case SE_DockWidgetCloseButton:
        case SE_DockWidgetFloatButton:
        case SE_DockWidgetTitleBarText:
        case SE_DockWidgetIcon: {
            int iconSize = proxy()->pixelMetric(PM_SmallIconSize, opt, widget);
            int buttonMargin = proxy()->pixelMetric(PM_DockWidgetTitleBarButtonMargin, opt, widget);
            QRect srect = opt->rect;

            const QStyleOptionDockWidget *dwOpt
                = qstyleoption_cast<const QStyleOptionDockWidget*>(opt);
            bool canClose = dwOpt == 0 ? true : dwOpt->closable;
            bool canFloat = dwOpt == 0 ? false : dwOpt->floatable;
            const QStyleOptionDockWidgetV2 *v2
                = qstyleoption_cast<const QStyleOptionDockWidgetV2*>(opt);
            bool verticalTitleBar = v2 == 0 ? false : v2->verticalTitleBar;

            // If this is a vertical titlebar, we transpose and work as if it was
            // horizontal, then transpose again.
            if (verticalTitleBar) {
                QSize size = srect.size();
                size.transpose();
                srect.setSize(size);
            }

            do {
                int right = srect.right();
                int left = srect.left();

                QRect closeRect;
                if (canClose) {
                    QSize sz = standardIcon(QStyle::SP_TitleBarCloseButton,
                                            opt, widget).actualSize(QSize(iconSize, iconSize));
                    sz += QSize(buttonMargin, buttonMargin);
                    if (verticalTitleBar)
                        sz.transpose();
                    closeRect = QRect(left,
                                      srect.center().y() - sz.height()/2,
                                      sz.width(), sz.height());
                    left = closeRect.right() + 1;
                }
                if (sr == SE_DockWidgetCloseButton) {
                    rect = closeRect;
                    break;
                }

                QRect floatRect;
                if (canFloat) {
                    QSize sz = standardIcon(QStyle::SP_TitleBarNormalButton,
                                            opt, widget).actualSize(QSize(iconSize, iconSize));
                    sz += QSize(buttonMargin, buttonMargin);
                    if (verticalTitleBar)
                        sz.transpose();
                    floatRect = QRect(left,
                                      srect.center().y() - sz.height()/2,
                                      sz.width(), sz.height());
                    left = floatRect.right() + 1;
                }
                if (sr == SE_DockWidgetFloatButton) {
                    rect = floatRect;
                    break;
                }

                QRect iconRect;
                if (const QDockWidget *dw = qobject_cast<const QDockWidget*>(widget)) {
                    QIcon icon;
                    if (dw->isFloating())
                        icon = dw->windowIcon();
                    if (!icon.isNull()
                        && icon.cacheKey() != QApplication::windowIcon().cacheKey()) {
                        QSize sz = icon.actualSize(QSize(rect.height(), rect.height()));
                        if (verticalTitleBar)
                            sz.transpose();
                        iconRect = QRect(right - sz.width(), srect.center().y() - sz.height()/2,
                                         sz.width(), sz.height());
                        right = iconRect.left() - 1;
                    }
                }
                if (sr == SE_DockWidgetIcon) {
                    rect = iconRect;
                    break;
                }

                QRect textRect = QRect(left, srect.top(),
                                       right - left, srect.height());
                if (sr == SE_DockWidgetTitleBarText) {
                    rect = textRect;
                    break;
                }
            } while (false);

            if (verticalTitleBar) {
                rect = QRect(srect.left() + rect.top() - srect.top(),
                          srect.top() + srect.right() - rect.right(),
                          rect.height(), rect.width());
            } else {
                rect = visualRect(opt->direction, srect, rect);
            }
            break;
        }
#endif
    default:
        rect = QCommonStyle::subElementRect(sr, opt, widget);
        break;
    }
    return rect;
}

static inline void drawToolbarButtonArrow(const QRect &toolButtonRect, ThemeDrawState tds, CGContextRef cg)
{
    QRect arrowRect = QRect(toolButtonRect.right() - 9, toolButtonRect.bottom() - 9, 7, 5);
    HIThemePopupArrowDrawInfo padi;
    padi.version = qt_mac_hitheme_version;
    padi.state = tds;
    padi.orientation = kThemeArrowDown;
    padi.size = kThemeArrow7pt;
    HIRect hirect = qt_hirectForQRect(arrowRect);
    HIThemeDrawPopupArrow(&hirect, &padi, cg, kHIThemeOrientationNormal);
}

void QMacStyle::drawComplexControl(ComplexControl cc, const QStyleOptionComplex *opt, QPainter *p,
                                   const QWidget *widget) const
{
    Q_D(const QMacStyle);
    ThemeDrawState tds = d->getDrawState(opt->state);
    QMacCGContext cg(p);
    switch (cc) {
    case CC_Slider:
    case CC_ScrollBar:
        if (const QStyleOptionSlider *slider = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
            HIThemeTrackDrawInfo tdi;
            d->getSliderInfo(cc, slider, &tdi, widget);
            if (slider->state & State_Sunken) {
                if (cc == CC_Slider) {
                    if (slider->activeSubControls == SC_SliderHandle)
                        tdi.trackInfo.slider.pressState = kThemeThumbPressed;
                    else if (slider->activeSubControls == SC_SliderGroove)
                        tdi.trackInfo.slider.pressState = kThemeLeftTrackPressed;
                } else {
                    if (slider->activeSubControls == SC_ScrollBarSubLine
                        || slider->activeSubControls == SC_ScrollBarAddLine) {
                        // This test looks complex but it basically boils down
                        // to the following: The "RTL look" on the mac also
                        // changed the directions of the controls, that's not
                        // what people expect (an arrow is an arrow), so we
                        // kind of fake and say the opposite button is hit.
                        // This works great, up until 10.4 which broke the
                        // scroll bars, so I also have actually do something
                        // similar when I have an upside down scroll bar
                        // because on Tiger I only "fake" the reverse stuff.
                        bool reverseHorizontal = (slider->direction == Qt::RightToLeft
                                                  && slider->orientation == Qt::Horizontal);
                        if ((reverseHorizontal
                             && slider->activeSubControls == SC_ScrollBarAddLine)
                            || (!reverseHorizontal
                                && slider->activeSubControls == SC_ScrollBarSubLine)) {
                            tdi.trackInfo.scrollbar.pressState = kThemeRightInsideArrowPressed
                                                                 | kThemeLeftOutsideArrowPressed;
                        } else {
                            tdi.trackInfo.scrollbar.pressState = kThemeLeftInsideArrowPressed
                                                                 | kThemeRightOutsideArrowPressed;
                        }
                    } else if (slider->activeSubControls == SC_ScrollBarAddPage) {
                        tdi.trackInfo.scrollbar.pressState = kThemeRightTrackPressed;
                    } else if (slider->activeSubControls == SC_ScrollBarSubPage) {
                        tdi.trackInfo.scrollbar.pressState = kThemeLeftTrackPressed;
                    } else if (slider->activeSubControls == SC_ScrollBarSlider) {
                        tdi.trackInfo.scrollbar.pressState = kThemeThumbPressed;
                    }
                }
            }
            HIRect macRect;
            bool tracking = slider->sliderPosition == slider->sliderValue;
            if (!tracking) {
                // Small optimization, the same as q->subControlRect
                QCFType<HIShapeRef> shape;
                HIThemeGetTrackThumbShape(&tdi, &shape);
                ptrHIShapeGetBounds(shape, &macRect);
                tdi.value = slider->sliderValue;
            }

            // Remove controls from the scroll bar if it is to short to draw them correctly.
            // This is done in two stages: first the thumb indicator is removed when it is
            // no longer possible to move it, second the up/down buttons are removed when
            // there is not enough space for them.
            if (cc == CC_ScrollBar) {
                if (opt && opt->styleObject && !QMacStylePrivate::scrollBars.contains(opt->styleObject))
                    QMacStylePrivate::scrollBars.insert(QPointer<QObject>(opt->styleObject));
                const int scrollBarLength = (slider->orientation == Qt::Horizontal)
                    ? slider->rect.width() : slider->rect.height();
                const QMacStyle::WidgetSizePolicy sizePolicy = widgetSizePolicy(widget, opt);
                if (scrollBarLength < scrollButtonsCutoffSize(thumbIndicatorCutoff, sizePolicy))
                    tdi.attributes &= ~kThemeTrackShowThumb;
                if (scrollBarLength < scrollButtonsCutoffSize(scrollButtonsCutoff, sizePolicy))
                    tdi.enableState = kThemeTrackNothingToScroll;
            } else {
                if (!(slider->subControls & SC_SliderHandle))
                    tdi.attributes &= ~kThemeTrackShowThumb;
                if (!(slider->subControls & SC_SliderGroove))
                    tdi.attributes |= kThemeTrackHideTrack;
            }

#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_7
            if (cc == CC_ScrollBar && proxy()->styleHint(SH_ScrollBar_Transient, opt, widget)) {
                bool wasActive = false;
                CGFloat opacity = 0.0;
                CGFloat expandScale = 1.0;
                CGFloat expandOffset = -1.0;
                bool shouldExpand = false;
                const CGFloat maxExpandScale = tdi.kind == kThemeSmallScrollBar ? 11.0 / 7.0 : 13.0 / 9.0;

                if (QObject *styleObject = opt->styleObject) {
                    int oldPos = styleObject->property("_q_stylepos").toInt();
                    int oldMin = styleObject->property("_q_stylemin").toInt();
                    int oldMax = styleObject->property("_q_stylemax").toInt();
                    QRect oldRect = styleObject->property("_q_stylerect").toRect();
                    int oldState = styleObject->property("_q_stylestate").toInt();
                    uint oldActiveControls = styleObject->property("_q_stylecontrols").toUInt();

                    // a scrollbar is transient when the scrollbar itself and
                    // its sibling are both inactive (ie. not pressed/hovered/moved)
                    bool transient = !opt->activeSubControls && !(slider->state & State_On);

                    if (!transient ||
                            oldPos != slider->sliderPosition ||
                            oldMin != slider->minimum ||
                            oldMax != slider->maximum ||
                            oldRect != slider->rect ||
                            oldState != slider->state ||
                            oldActiveControls != slider->activeSubControls) {

                        // if the scrollbar is transient or its attributes, geometry or
                        // state has changed, the opacity is reset back to 100% opaque
                        opacity = 1.0;

                        styleObject->setProperty("_q_stylepos", slider->sliderPosition);
                        styleObject->setProperty("_q_stylemin", slider->minimum);
                        styleObject->setProperty("_q_stylemax", slider->maximum);
                        styleObject->setProperty("_q_stylerect", slider->rect);
                        styleObject->setProperty("_q_stylestate", static_cast<int>(slider->state));
                        styleObject->setProperty("_q_stylecontrols", static_cast<uint>(slider->activeSubControls));

                        QScrollbarStyleAnimation *anim  = qobject_cast<QScrollbarStyleAnimation *>(d->animation(styleObject));
                        if (transient) {
                            if (!anim) {
                                anim = new QScrollbarStyleAnimation(QScrollbarStyleAnimation::Deactivating, styleObject);
                                d->startAnimation(anim);
                            } else if (anim->mode() == QScrollbarStyleAnimation::Deactivating) {
                                // the scrollbar was already fading out while the
                                // state changed -> restart the fade out animation
                                anim->setCurrentTime(0);
                            }
                        } else if (anim && anim->mode() == QScrollbarStyleAnimation::Deactivating) {
                            d->stopAnimation(styleObject);
                        }
                    }

                    QScrollbarStyleAnimation *anim = qobject_cast<QScrollbarStyleAnimation *>(d->animation(styleObject));
                    if (anim && anim->mode() == QScrollbarStyleAnimation::Deactivating) {
                        // once a scrollbar was active (hovered/pressed), it retains
                        // the active look even if it's no longer active while fading out
                        if (oldActiveControls)
                            anim->setActive(true);

                        wasActive = anim->wasActive();
                        opacity = anim->currentValue();
                    }

                    shouldExpand = (opt->activeSubControls || wasActive) && QSysInfo::macVersion() >= QSysInfo::MV_10_8;
                    if (shouldExpand) {
                        if (!anim && !oldActiveControls) {
                            // Start expand animation only once and when entering
                            anim = new QScrollbarStyleAnimation(QScrollbarStyleAnimation::Activating, styleObject);
                            d->startAnimation(anim);
                        }
                        if (anim && anim->mode() == QScrollbarStyleAnimation::Activating) {
                            expandScale = 1.0 + (maxExpandScale - 1.0) * anim->currentValue();
                            expandOffset = 5.5 * anim->currentValue() - 1;
                        } else {
                            // Keep expanded state after the animation ends, and when fading out
                            expandScale = maxExpandScale;
                            expandOffset = 4.5;
                        }
                    }
                }

                const bool isHorizontal = slider->orientation == Qt::Horizontal;

                CGContextSaveGState(cg);
                [NSGraphicsContext saveGraphicsState];

                [NSGraphicsContext setCurrentContext:[NSGraphicsContext
                     graphicsContextWithGraphicsPort:(CGContextRef)cg flipped:NO]];
                NSScroller *scroller = reinterpret_cast<NSScroller*>(d->nsscroller);
                [scroller initWithFrame:NSMakeRect(0, 0, slider->rect.width(), slider->rect.height())];
                // mac os behaviour: as soon as one color channel is >= 128,
                // the bg is considered bright, scroller is dark
                const QColor bgColor = QStyleHelper::backgroundColor(opt->palette, widget);
                const bool isDarkBg = bgColor.red() < 128 && bgColor.green() < 128 &&
                                      bgColor.blue() < 128;
                if (isDarkBg)
                    [scroller setKnobStyle:NSScrollerKnobStyleLight];
                else
                    [scroller setKnobStyle:NSScrollerKnobStyleDefault];

                [scroller setControlSize:(tdi.kind == kThemeSmallScrollBar ? NSMiniControlSize
                                                                           : NSRegularControlSize)];
                [scroller setBounds:NSMakeRect(0, 0, slider->rect.width(), slider->rect.height())];
                [scroller setScrollerStyle:NSScrollerStyleOverlay];

                CGContextBeginTransparencyLayer(cg, NULL);
                CGContextSetAlpha(cg, opacity);

                // Draw the track when hovering
                if (opt->activeSubControls || wasActive) {
                    NSRect rect = [scroller bounds];
                    if (shouldExpand) {
                        if (isHorizontal)
                            rect.origin.y += 4.5 - expandOffset;
                        else
                            rect.origin.x += 4.5 - expandOffset;
                    }
                    [scroller drawKnobSlotInRect:rect highlight:YES];
                }

                const qreal length = slider->maximum - slider->minimum + slider->pageStep;
                const qreal proportion = slider->pageStep / length;
                qreal value = (slider->sliderValue - slider->minimum) / length;
                if (isHorizontal && slider->direction == Qt::RightToLeft)
                    value = 1.0 - value - proportion;

                [scroller setKnobProportion:1.0];

                const int minKnobWidth = 26;

                if (isHorizontal) {
                    const qreal plannedWidth = proportion * slider->rect.width();
                    const qreal width = qMax<qreal>(minKnobWidth, plannedWidth);
                    const qreal totalWidth = slider->rect.width() + plannedWidth - width;
                    [scroller setFrame:NSMakeRect(0, 0, width, slider->rect.height())];
                    if (shouldExpand) {
                        CGContextScaleCTM(cg, 1, expandScale);
                        CGContextTranslateCTM(cg, value * totalWidth, -expandOffset);
                    } else {
                        CGContextTranslateCTM(cg, value * totalWidth, 1);
                    }
                } else {
                    const qreal plannedHeight = proportion * slider->rect.height();
                    const qreal height = qMax<qreal>(minKnobWidth, plannedHeight);
                    const qreal totalHeight = slider->rect.height() + plannedHeight - height;
                    [scroller setFrame:NSMakeRect(0, 0, slider->rect.width(), height)];
                    if (shouldExpand) {
                        CGContextScaleCTM(cg, expandScale, 1);
                        CGContextTranslateCTM(cg, -expandOffset, value * totalHeight);
                    } else {
                        CGContextTranslateCTM(cg, 1, value * totalHeight);
                    }
                }
                if (length > 0.0) {
                    [scroller layout];
                    [scroller drawKnob];
                }

                CGContextEndTransparencyLayer(cg);

                [NSGraphicsContext restoreGraphicsState];
                CGContextRestoreGState(cg);
            } else
#endif
            {
                d->stopAnimation(opt->styleObject);

                HIThemeDrawTrack(&tdi, tracking ? 0 : &macRect, cg,
                                 kHIThemeOrientationNormal);
                if (cc == CC_Slider && slider->subControls & SC_SliderTickmarks) {
                    if (qt_mac_is_metal(widget)) {
                        if (tdi.enableState == kThemeTrackInactive)
                            tdi.enableState = kThemeTrackActive;  // Looks more Cocoa-like
                    }
                    int interval = slider->tickInterval;
                    if (interval == 0) {
                        interval = slider->pageStep;
                        if (interval == 0)
                            interval = slider->singleStep;
                        if (interval == 0)
                            interval = 1;
                    }
                    int numMarks = 1 + ((slider->maximum - slider->minimum) / interval);

                    if (tdi.trackInfo.slider.thumbDir == kThemeThumbPlain) {
                        // They asked for both, so we'll give it to them.
                        tdi.trackInfo.slider.thumbDir = kThemeThumbDownward;
                        HIThemeDrawTrackTickMarks(&tdi, numMarks,
                                                  cg,
                                                  kHIThemeOrientationNormal);
                        tdi.trackInfo.slider.thumbDir = kThemeThumbUpward;
                        HIThemeDrawTrackTickMarks(&tdi, numMarks,
                                                  cg,
                                                   kHIThemeOrientationNormal);
                    } else {
                        HIThemeDrawTrackTickMarks(&tdi, numMarks,
                                                  cg,
                                                  kHIThemeOrientationNormal);

                    }
                }
            }
        }
        break;
    case CC_SpinBox:
        if (const QStyleOptionSpinBox *sb = qstyleoption_cast<const QStyleOptionSpinBox *>(opt)) {
            QStyleOptionSpinBox newSB = *sb;
            if (sb->frame && (sb->subControls & SC_SpinBoxFrame)) {
                SInt32 frame_size;
                GetThemeMetric(kThemeMetricEditTextFrameOutset, &frame_size);

                QRect lineeditRect = proxy()->subControlRect(CC_SpinBox, sb, SC_SpinBoxEditField, widget);
                lineeditRect.adjust(-frame_size, -frame_size, +frame_size, +frame_size);

                HIThemeFrameDrawInfo fdi;
                fdi.version = qt_mac_hitheme_version;
                fdi.state = tds == kThemeStateInactive ? kThemeStateActive : tds;
                fdi.kind = kHIThemeFrameTextFieldSquare;
                fdi.isFocused = false;
                HIRect hirect = qt_hirectForQRect(lineeditRect);
                HIThemeDrawFrame(&hirect, &fdi, cg, kHIThemeOrientationNormal);
            }
            if (sb->subControls & (SC_SpinBoxUp | SC_SpinBoxDown)) {
                HIThemeButtonDrawInfo bdi;
                bdi.version = qt_mac_hitheme_version;
                QAquaWidgetSize aquaSize = d->aquaSizeConstrain(opt, widget);
                switch (aquaSize) {
                    case QAquaSizeUnknown:
                    case QAquaSizeLarge:
                        bdi.kind = kThemeIncDecButton;
                        break;
                    case QAquaSizeMini:
                        bdi.kind = kThemeIncDecButtonMini;
                        break;
                    case QAquaSizeSmall:
                        bdi.kind = kThemeIncDecButtonSmall;
                        break;
                }
                if (!(sb->stepEnabled & (QAbstractSpinBox::StepUpEnabled
                                        | QAbstractSpinBox::StepDownEnabled)))
                    tds = kThemeStateUnavailable;
                if (sb->activeSubControls == SC_SpinBoxDown
                    && (sb->state & State_Sunken))
                    tds = kThemeStatePressedDown;
                else if (sb->activeSubControls == SC_SpinBoxUp
                         && (sb->state & State_Sunken))
                    tds = kThemeStatePressedUp;
                if (tds == kThemeStateInactive)
                    bdi.state = kThemeStateActive;
                else
                    bdi.state = tds;
                bdi.value = kThemeButtonOff;
                bdi.adornment = kThemeAdornmentNone;

                QRect updown = proxy()->subControlRect(CC_SpinBox, sb, SC_SpinBoxUp, widget);

                updown |= proxy()->subControlRect(CC_SpinBox, sb, SC_SpinBoxDown, widget);
                HIRect newRect = qt_hirectForQRect(updown);
                QRect off_rct;
                HIRect outRect;
                HIThemeGetButtonBackgroundBounds(&newRect, &bdi, &outRect);
                off_rct.setRect(int(newRect.origin.x - outRect.origin.x),
                                int(newRect.origin.y - outRect.origin.y),
                                int(outRect.size.width - newRect.size.width),
                                int(outRect.size.height - newRect.size.height));

                newRect = qt_hirectForQRect(updown, off_rct);
                if (tds == kThemeStateInactive)
                    d->drawColorlessButton(newRect, &bdi, p, sb);
                else
                    HIThemeDrawButton(&newRect, &bdi, cg, kHIThemeOrientationNormal, 0);
            }
        }
        break;
    case CC_ComboBox:
        if (const QStyleOptionComboBox *combo = qstyleoption_cast<const QStyleOptionComboBox *>(opt)){
            HIThemeButtonDrawInfo bdi;
            d->initComboboxBdi(combo, &bdi, widget, d->getDrawState(opt->state));
            if (tds != kThemeStateInactive)
                QMacStylePrivate::drawCombobox(qt_hirectForQRect(combo->rect), bdi, p);
            else
                d->drawColorlessButton(qt_hirectForQRect(combo->rect), &bdi, p, opt);
        }
        break;
    case CC_TitleBar:
        if (const QStyleOptionTitleBar *titlebar
                = qstyleoption_cast<const QStyleOptionTitleBar *>(opt)) {
            if (titlebar->state & State_Active) {
                if (titlebar->titleBarState & State_Active)
                    tds = kThemeStateActive;
                else
                    tds = kThemeStateInactive;
            } else {
                tds = kThemeStateInactive;
            }

            HIThemeWindowDrawInfo wdi;
            wdi.version = qt_mac_hitheme_version;
            wdi.state = tds;
            wdi.windowType = QtWinType;
            wdi.titleHeight = titlebar->rect.height();
            wdi.titleWidth = titlebar->rect.width();
            wdi.attributes = kThemeWindowHasTitleText;
            // It seems HIThemeDrawTitleBarWidget is not able to draw a dirty
            // close button, so use HIThemeDrawWindowFrame instead.
            if (widget && widget->isWindowModified() && titlebar->subControls & SC_TitleBarCloseButton)
                wdi.attributes |= kThemeWindowHasCloseBox | kThemeWindowHasDirty;

            HIRect titleBarRect;
            HIRect tmpRect = qt_hirectForQRect(titlebar->rect);
            {
                QCFType<HIShapeRef> titleRegion;
                QRect newr = titlebar->rect.adjusted(0, 0, 2, 0);
                HIThemeGetWindowShape(&tmpRect, &wdi, kWindowTitleBarRgn, &titleRegion);
                ptrHIShapeGetBounds(titleRegion, &tmpRect);
                newr.translate(newr.x() - int(tmpRect.origin.x), newr.y() - int(tmpRect.origin.y));
                titleBarRect = qt_hirectForQRect(newr);
            }
            HIThemeDrawWindowFrame(&titleBarRect, &wdi, cg, kHIThemeOrientationNormal, 0);
            if (titlebar->subControls & (SC_TitleBarCloseButton
                                         | SC_TitleBarMaxButton
                                         | SC_TitleBarMinButton
                                         | SC_TitleBarNormalButton)) {
                HIThemeWindowWidgetDrawInfo wwdi;
                wwdi.version = qt_mac_hitheme_version;
                wwdi.widgetState = tds;
                if (titlebar->state & State_MouseOver)
                    wwdi.widgetState = kThemeStateRollover;
                wwdi.windowType = QtWinType;
                wwdi.attributes = wdi.attributes | kThemeWindowHasFullZoom | kThemeWindowHasCloseBox | kThemeWindowHasCollapseBox;
                wwdi.windowState = wdi.state;
                wwdi.titleHeight = wdi.titleHeight;
                wwdi.titleWidth = wdi.titleWidth;
                ThemeDrawState savedControlState = wwdi.widgetState;
                uint sc = SC_TitleBarMinButton;
                ThemeTitleBarWidget tbw = kThemeWidgetCollapseBox;
                bool active = titlebar->state & State_Active;
                if (QSysInfo::macVersion() < QSysInfo::MV_10_6) {
                    int border = 2;
                    titleBarRect.origin.x += border;
                    titleBarRect.origin.y -= border;
                }

                while (sc <= SC_TitleBarCloseButton) {
                    if (sc & titlebar->subControls) {
                        uint tmp = sc;
                        wwdi.widgetState = savedControlState;
                        wwdi.widgetType = tbw;
                        if (sc == SC_TitleBarMinButton)
                            tmp |= SC_TitleBarNormalButton;
                        if (active && (titlebar->activeSubControls & tmp)
                                && (titlebar->state & State_Sunken))
                            wwdi.widgetState = kThemeStatePressed;
                        // Draw all sub controllers except the dirty close button
                        // (it is already handled by HIThemeDrawWindowFrame).
                        if (!(widget && widget->isWindowModified() && tbw == kThemeWidgetCloseBox)) {
                            HIThemeDrawTitleBarWidget(&titleBarRect, &wwdi, cg, kHIThemeOrientationNormal);
                            p->paintEngine()->syncState();
                        }
                    }
                    sc = sc << 1;
                    tbw = tbw >> 1;
                }
            }
            p->paintEngine()->syncState();
            if (titlebar->subControls & SC_TitleBarLabel) {
                int iw = 0;
                if (!titlebar->icon.isNull()) {
                    QCFType<HIShapeRef> titleRegion2;
                    HIThemeGetWindowShape(&titleBarRect, &wdi, kWindowTitleProxyIconRgn,
                                          &titleRegion2);
                    ptrHIShapeGetBounds(titleRegion2, &tmpRect);
                    if (tmpRect.size.width != 1) {
                        int iconExtent = proxy()->pixelMetric(PM_SmallIconSize);
                        iw = titlebar->icon.actualSize(QSize(iconExtent, iconExtent)).width();
                    }
                }
                if (!titlebar->text.isEmpty()) {
                    p->save();
                    QCFType<HIShapeRef> titleRegion3;
                    HIThemeGetWindowShape(&titleBarRect, &wdi, kWindowTitleTextRgn, &titleRegion3);
                    ptrHIShapeGetBounds(titleRegion3, &tmpRect);
                    p->setClipRect(qt_qrectForHIRect(tmpRect));
                    QRect br = p->clipRegion().boundingRect();
                    int x = br.x(),
                    y = br.y() + (titlebar->rect.height() / 2 - p->fontMetrics().height() / 2);
                    if (br.width() <= (p->fontMetrics().width(titlebar->text) + iw * 2))
                        x += iw;
                    else
                        x += br.width() / 2 - p->fontMetrics().width(titlebar->text) / 2;
                    if (iw)
                        p->drawPixmap(x - iw, y,
                                      titlebar->icon.pixmap(proxy()->pixelMetric(PM_SmallIconSize), QIcon::Normal));
                    drawItemText(p, br, Qt::AlignCenter, opt->palette, tds == kThemeStateActive,
                                    titlebar->text, QPalette::Text);
                    p->restore();
                }
            }
        }
        break;
    case CC_GroupBox:
        if (const QStyleOptionGroupBox *gb
                = qstyleoption_cast<const QStyleOptionGroupBox *>(opt)) {

            QStyleOptionGroupBox groupBox(*gb);
            bool flat = (groupBox.features & QStyleOptionFrameV2::Flat);
            if (!flat)
                groupBox.state |= QStyle::State_Mini; // Force mini-sized checkbox to go with small-sized label
            else
                groupBox.subControls = groupBox.subControls & ~SC_GroupBoxFrame; // We don't like frames and ugly lines

            bool didModifySubControls = false;
            if ((!widget || !widget->testAttribute(Qt::WA_SetFont))
                    && QApplication::desktopSettingsAware()) {
                groupBox.subControls = groupBox.subControls & ~SC_GroupBoxLabel;
                didModifySubControls = true;
            }
            QCommonStyle::drawComplexControl(cc, &groupBox, p, widget);
            if (didModifySubControls) {
                p->save();
                CGContextSetShouldAntialias(cg, true);
                CGContextSetShouldSmoothFonts(cg, true);
                HIThemeTextInfo tti;
                tti.version = qt_mac_hitheme_version;
                tti.state = tds;
                QColor textColor = groupBox.palette.windowText().color();
                CGFloat colorComp[] = { static_cast<CGFloat>(textColor.redF()), static_cast<CGFloat>(textColor.greenF()),
                                      static_cast<CGFloat>(textColor.blueF()), static_cast<CGFloat>(textColor.alphaF()) };
                CGContextSetFillColorSpace(cg, qt_mac_genericColorSpace());
                CGContextSetFillColor(cg, colorComp);
                tti.fontID = flat ? kThemeSystemFont : kThemeSmallSystemFont;
                tti.horizontalFlushness = kHIThemeTextHorizontalFlushCenter;
                tti.verticalFlushness = kHIThemeTextVerticalFlushCenter;
                tti.options = kHIThemeTextBoxOptionNone;
                tti.truncationPosition = kHIThemeTextTruncationNone;
                tti.truncationMaxLines = 1 + groupBox.text.count(QLatin1Char('\n'));
                QCFString groupText = qt_mac_removeMnemonics(groupBox.text);
                QRect r = proxy()->subControlRect(CC_GroupBox, &groupBox, SC_GroupBoxLabel, widget);
                HIRect bounds = qt_hirectForQRect(r);
                HIThemeDrawTextBox(groupText, &bounds, &tti, cg, kHIThemeOrientationNormal);
                p->restore();
            }
        }
        break;
    case CC_ToolButton:
        if (const QStyleOptionToolButton *tb
                = qstyleoption_cast<const QStyleOptionToolButton *>(opt)) {

            if (QStyleHelper::hasAncestor(opt->styleObject, QAccessible::ToolBar)) {
                if (tb->subControls & SC_ToolButtonMenu) {
                    QStyleOption arrowOpt(0);
                    arrowOpt.rect = proxy()->subControlRect(cc, tb, SC_ToolButtonMenu, widget);
                    arrowOpt.rect.setY(arrowOpt.rect.y() + arrowOpt.rect.height() / 2);
                    arrowOpt.rect.setHeight(arrowOpt.rect.height() / 2);
                    arrowOpt.state = tb->state;
                    arrowOpt.palette = tb->palette;
                    proxy()->drawPrimitive(PE_IndicatorArrowDown, &arrowOpt, p, widget);
                } else if ((tb->features & QStyleOptionToolButton::HasMenu)
                            && (tb->toolButtonStyle != Qt::ToolButtonTextOnly && !tb->icon.isNull())) {
                    drawToolbarButtonArrow(tb->rect, tds, cg);
                }
                if (tb->state & State_On) {
                    if (QSysInfo::MacintoshVersion >= QSysInfo::MV_10_5) {
                        static QPixmap pm(QLatin1String(":/qt-project.org/mac/style/images/leopard-unified-toolbar-on.png"));
                        p->setRenderHint(QPainter::SmoothPixmapTransform);
                        QStyleHelper::drawBorderPixmap(pm, p, tb->rect, 2, 2, 2, 2);
                    } else {
                        QPen oldPen = p->pen();
                        p->setPen(QColor(0, 0, 0, 0x3a));
                        p->fillRect(tb->rect.adjusted(1, 1, -1, -1), QColor(0, 0, 0, 0x12));
                        p->drawLine(tb->rect.left() + 1, tb->rect.top(),
                                    tb->rect.right() - 1, tb->rect.top());
                        p->drawLine(tb->rect.left() + 1, tb->rect.bottom(),
                                    tb->rect.right() - 1, tb->rect.bottom());
                        p->drawLine(tb->rect.topLeft(), tb->rect.bottomLeft());
                        p->drawLine(tb->rect.topRight(), tb->rect.bottomRight());
                        p->setPen(oldPen);
                    }
                }
                proxy()->drawControl(CE_ToolButtonLabel, opt, p, widget);
            } else {
                ThemeButtonKind bkind = kThemeBevelButton;
                switch (d->aquaSizeConstrain(opt, widget)) {
                case QAquaSizeUnknown:
                case QAquaSizeLarge:
                    bkind = kThemeBevelButton;
                    break;
                case QAquaSizeMini:
                case QAquaSizeSmall:
                    bkind = kThemeSmallBevelButton;
                    break;
                }

                QRect button, menuarea;
                button   = proxy()->subControlRect(cc, tb, SC_ToolButton, widget);
                menuarea = proxy()->subControlRect(cc, tb, SC_ToolButtonMenu, widget);
                State bflags = tb->state,
                mflags = tb->state;
                if (tb->subControls & SC_ToolButton)
                    bflags |= State_Sunken;
                if (tb->subControls & SC_ToolButtonMenu)
                    mflags |= State_Sunken;

                if (tb->subControls & SC_ToolButton) {
                    if (bflags & (State_Sunken | State_On | State_Raised)) {
                        HIThemeButtonDrawInfo bdi;
                        bdi.version = qt_mac_hitheme_version;
                        bdi.state = tds;
                        bdi.adornment = kThemeAdornmentNone;
                        bdi.kind = bkind;
                        bdi.value = kThemeButtonOff;
                        if (tb->state & State_HasFocus)
                            bdi.adornment = kThemeAdornmentFocus;
                        if (tb->state & State_Sunken)
                            bdi.state = kThemeStatePressed;
                        if (tb->state & State_On)
                            bdi.value = kThemeButtonOn;

                        QRect off_rct(0, 0, 0, 0);
                        HIRect myRect, macRect;
                        myRect = CGRectMake(tb->rect.x(), tb->rect.y(),
                                            tb->rect.width(), tb->rect.height());
                        HIThemeGetButtonBackgroundBounds(&myRect, &bdi, &macRect);
                        off_rct.setRect(int(myRect.origin.x - macRect.origin.x),
                                        int(myRect.origin.y - macRect.origin.y),
                                        int(macRect.size.width - myRect.size.width),
                                        int(macRect.size.height - myRect.size.height));

                        myRect = qt_hirectForQRect(button, off_rct);
                        HIThemeDrawButton(&myRect, &bdi, cg, kHIThemeOrientationNormal, 0);
                    }
                }

                if (tb->subControls & SC_ToolButtonMenu) {
                    HIThemeButtonDrawInfo bdi;
                    bdi.version = qt_mac_hitheme_version;
                    bdi.state = tds;
                    bdi.value = kThemeButtonOff;
                    bdi.adornment = kThemeAdornmentNone;
                    bdi.kind = bkind;
                    if (tb->state & State_HasFocus)
                        bdi.adornment = kThemeAdornmentFocus;
                    if (tb->state & (State_On | State_Sunken)
                                     || (tb->activeSubControls & SC_ToolButtonMenu))
                        bdi.state = kThemeStatePressed;
                    HIRect hirect = qt_hirectForQRect(menuarea);
                    HIThemeDrawButton(&hirect, &bdi, cg, kHIThemeOrientationNormal, 0);
                    QRect r(menuarea.x() + ((menuarea.width() / 2) - 3), menuarea.height() - 8, 8, 8);
                    HIThemePopupArrowDrawInfo padi;
                    padi.version = qt_mac_hitheme_version;
                    padi.state = tds;
                    padi.orientation = kThemeArrowDown;
                    padi.size = kThemeArrow7pt;
                    hirect = qt_hirectForQRect(r);
                    HIThemeDrawPopupArrow(&hirect, &padi, cg, kHIThemeOrientationNormal);
                } else if (tb->features & QStyleOptionToolButton::HasMenu) {
                    drawToolbarButtonArrow(tb->rect, tds, cg);
                }
                QRect buttonRect = proxy()->subControlRect(CC_ToolButton, tb, SC_ToolButton, widget);
                int fw = proxy()->pixelMetric(PM_DefaultFrameWidth, opt, widget);
                QStyleOptionToolButton label = *tb;
                label.rect = buttonRect.adjusted(fw, fw, -fw, -fw);
                proxy()->drawControl(CE_ToolButtonLabel, &label, p, widget);
            }
        }
        break;
    case CC_Dial:
        if (const QStyleOptionSlider *dial = qstyleoption_cast<const QStyleOptionSlider *>(opt))
            QStyleHelper::drawDial(dial, p);
        break;
    default:
        QCommonStyle::drawComplexControl(cc, opt, p, widget);
        break;
    }
}

QStyle::SubControl QMacStyle::hitTestComplexControl(ComplexControl cc,
                                                    const QStyleOptionComplex *opt,
                                                    const QPoint &pt, const QWidget *widget) const
{
    Q_D(const QMacStyle);
    SubControl sc = QStyle::SC_None;
    switch (cc) {
    case CC_ComboBox:
        if (const QStyleOptionComboBox *cmb = qstyleoption_cast<const QStyleOptionComboBox *>(opt)) {
            sc = QCommonStyle::hitTestComplexControl(cc, cmb, pt, widget);
            if (!cmb->editable && sc != QStyle::SC_None)
                sc = SC_ComboBoxArrow;  // A bit of a lie, but what we want
        }
        break;
    case CC_Slider:
        if (const QStyleOptionSlider *slider = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
            HIThemeTrackDrawInfo tdi;
            d->getSliderInfo(cc, slider, &tdi, widget);
            ControlPartCode part;
            HIPoint pos = CGPointMake(pt.x(), pt.y());
            if (HIThemeHitTestTrack(&tdi, &pos, &part)) {
                if (part == kControlPageUpPart || part == kControlPageDownPart)
                    sc = SC_SliderGroove;
                else
                    sc = SC_SliderHandle;
            }
        }
        break;
    case CC_ScrollBar:
        if (const QStyleOptionSlider *sb = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
            HIScrollBarTrackInfo sbi;
            sbi.version = qt_mac_hitheme_version;
            if (!(sb->state & State_Active))
                sbi.enableState = kThemeTrackInactive;
            else if (sb->state & State_Enabled)
                sbi.enableState = kThemeTrackActive;
            else
                sbi.enableState = kThemeTrackDisabled;

            // The arrow buttons are not drawn if the scroll bar is to short,
            // exclude them from the hit test.
            const int scrollBarLength = (sb->orientation == Qt::Horizontal)
                ? sb->rect.width() : sb->rect.height();
            if (scrollBarLength < scrollButtonsCutoffSize(scrollButtonsCutoff, widgetSizePolicy(widget, opt)))
                sbi.enableState = kThemeTrackNothingToScroll;

            sbi.viewsize = sb->pageStep;
            HIPoint pos = CGPointMake(pt.x(), pt.y());

            HIRect macSBRect = qt_hirectForQRect(sb->rect);
            ControlPartCode part;
            bool reverseHorizontal = (sb->direction == Qt::RightToLeft
                                      && sb->orientation == Qt::Horizontal
                                      && (!sb->upsideDown ||
                                          (QSysInfo::MacintoshVersion >= QSysInfo::MV_10_4
                                                      && sb->upsideDown)));
            if (HIThemeHitTestScrollBarArrows(&macSBRect, &sbi, sb->orientation == Qt::Horizontal,
                        &pos, 0, &part)) {
                if (part == kControlUpButtonPart)
                    sc = reverseHorizontal ? SC_ScrollBarAddLine : SC_ScrollBarSubLine;
                else if (part == kControlDownButtonPart)
                    sc = reverseHorizontal ? SC_ScrollBarSubLine : SC_ScrollBarAddLine;
            } else {
                HIThemeTrackDrawInfo tdi;
                d->getSliderInfo(cc, sb, &tdi, widget);
                if(tdi.enableState == kThemeTrackInactive)
                    tdi.enableState = kThemeTrackActive;
                if (HIThemeHitTestTrack(&tdi, &pos, &part)) {
                    if (part == kControlPageUpPart)
                        sc = reverseHorizontal ? SC_ScrollBarAddPage
                                               : SC_ScrollBarSubPage;
                    else if (part == kControlPageDownPart)
                        sc = reverseHorizontal ? SC_ScrollBarSubPage
                                               : SC_ScrollBarAddPage;
                    else
                        sc = SC_ScrollBarSlider;
                }
            }
        }
        break;
/*
    I don't know why, but we only get kWindowContentRgn here, which isn't what we want at all.
    It would be very nice if this would work.
    case QStyle::CC_TitleBar:
        if (const QStyleOptionTitleBar *tbar = qstyleoption_cast<const QStyleOptionTitleBar *>(opt)) {
            HIThemeWindowDrawInfo wdi;
            memset(&wdi, 0, sizeof(wdi));
            wdi.version = qt_mac_hitheme_version;
            wdi.state = kThemeStateActive;
            wdi.windowType = QtWinType;
            wdi.titleWidth = tbar->rect.width();
            wdi.titleHeight = tbar->rect.height();
            if (tbar->titleBarState)
                wdi.attributes |= kThemeWindowHasFullZoom | kThemeWindowHasCloseBox
                                  | kThemeWindowHasCollapseBox;
            else if (tbar->titleBarFlags & Qt::WindowSystemMenuHint)
                wdi.attributes |= kThemeWindowHasCloseBox;
            QRect tmpRect = tbar->rect;
            tmpRect.setHeight(tmpRect.height() + 100);
            HIRect hirect = qt_hirectForQRect(tmpRect);
            WindowRegionCode hit;
            HIPoint hipt = CGPointMake(pt.x(), pt.y());
            if (HIThemeGetWindowRegionHit(&hirect, &wdi, &hipt, &hit)) {
                switch (hit) {
                case kWindowCloseBoxRgn:
                    sc = QStyle::SC_TitleBarCloseButton;
                    break;
                case kWindowCollapseBoxRgn:
                    sc = QStyle::SC_TitleBarMinButton;
                    break;
                case kWindowZoomBoxRgn:
                    sc = QStyle::SC_TitleBarMaxButton;
                    break;
                case kWindowTitleTextRgn:
                    sc = QStyle::SC_TitleBarLabel;
                    break;
                default:
                    qDebug("got something else %d", hit);
                    break;
                }
            }
        }
        break;
*/
    default:
        sc = QCommonStyle::hitTestComplexControl(cc, opt, pt, widget);
        break;
    }
    return sc;
}

QRect QMacStyle::subControlRect(ComplexControl cc, const QStyleOptionComplex *opt, SubControl sc,
                                const QWidget *widget) const
{
    Q_D(const QMacStyle);
    QRect ret;
    switch (cc) {
    case CC_Slider:
    case CC_ScrollBar:
        if (const QStyleOptionSlider *slider = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
            HIThemeTrackDrawInfo tdi;
            d->getSliderInfo(cc, slider, &tdi, widget);
            HIRect macRect;
            QCFType<HIShapeRef> shape;
            bool scrollBar = cc == CC_ScrollBar;
            if ((scrollBar && sc == SC_ScrollBarSlider)
                || (!scrollBar && sc == SC_SliderHandle)) {
                HIThemeGetTrackThumbShape(&tdi, &shape);
                ptrHIShapeGetBounds(shape, &macRect);
            } else if (!scrollBar && sc == SC_SliderGroove) {
                HIThemeGetTrackBounds(&tdi, &macRect);
            } else if (sc == SC_ScrollBarGroove) { // Only scroll bar parts available...
                HIThemeGetTrackDragRect(&tdi, &macRect);
            } else {
                ControlPartCode cpc;
                if (sc == SC_ScrollBarSubPage || sc == SC_ScrollBarAddPage) {
                    cpc = sc == SC_ScrollBarSubPage ? kControlPageDownPart
                                                            : kControlPageUpPart;
                } else {
                    cpc = sc == SC_ScrollBarSubLine ? kControlUpButtonPart
                                                            : kControlDownButtonPart;
                    if (slider->direction == Qt::RightToLeft
                        && slider->orientation == Qt::Horizontal) {
                        if (cpc == kControlDownButtonPart)
                            cpc = kControlUpButtonPart;
                        else if (cpc == kControlUpButtonPart)
                            cpc = kControlDownButtonPart;
                    }
                }
                HIThemeGetTrackPartBounds(&tdi, cpc, &macRect);
            }
            ret = qt_qrectForHIRect(macRect);

            // Tweak: the dark line between the sub/add line buttons belong to only one of the buttons
            // when doing hit-testing, but both of them have to repaint it. Extend the rect to cover
            // the line in the cases where HIThemeGetTrackPartBounds returns a rect that doesn't.
            if (slider->orientation == Qt::Horizontal) {
                if (slider->direction == Qt::LeftToRight && sc == SC_ScrollBarSubLine)
                    ret.adjust(0, 0, 1, 0);
                else if (slider->direction == Qt::RightToLeft && sc == SC_ScrollBarAddLine)
                    ret.adjust(-1, 0, 1, 0);
            } else if (sc == SC_ScrollBarAddLine) {
                ret.adjust(0, -1, 0, 1);
            }
        }
        break;
    case CC_TitleBar:
        if (const QStyleOptionTitleBar *titlebar
                = qstyleoption_cast<const QStyleOptionTitleBar *>(opt)) {
            HIThemeWindowDrawInfo wdi;
            memset(&wdi, 0, sizeof(wdi));
            wdi.version = qt_mac_hitheme_version;
            wdi.state = kThemeStateActive;
            wdi.windowType = QtWinType;
            wdi.titleHeight = titlebar->rect.height();
            wdi.titleWidth = titlebar->rect.width();
            wdi.attributes = kThemeWindowHasTitleText;
            if (titlebar->subControls & SC_TitleBarCloseButton)
                wdi.attributes |= kThemeWindowHasCloseBox;
            if (titlebar->subControls & SC_TitleBarMaxButton
                                        | SC_TitleBarNormalButton)
                wdi.attributes |= kThemeWindowHasFullZoom;
            if (titlebar->subControls & SC_TitleBarMinButton)
                wdi.attributes |= kThemeWindowHasCollapseBox;
            WindowRegionCode wrc = kWindowGlobalPortRgn;

            if (sc == SC_TitleBarCloseButton)
                wrc = kWindowCloseBoxRgn;
            else if (sc == SC_TitleBarMinButton)
                wrc = kWindowCollapseBoxRgn;
            else if (sc == SC_TitleBarMaxButton)
                wrc = kWindowZoomBoxRgn;
            else if (sc == SC_TitleBarLabel)
                wrc = kWindowTitleTextRgn;
            else if (sc == SC_TitleBarSysMenu)
                ret.setRect(-1024, -1024, 10, proxy()->pixelMetric(PM_TitleBarHeight,
                                                             titlebar, widget));
            if (wrc != kWindowGlobalPortRgn) {
                QCFType<HIShapeRef> region;
                QRect tmpRect = titlebar->rect;
                HIRect titleRect = qt_hirectForQRect(tmpRect);
                HIThemeGetWindowShape(&titleRect, &wdi, kWindowTitleBarRgn, &region);
                ptrHIShapeGetBounds(region, &titleRect);
                CFRelease(region);
                tmpRect.translate(tmpRect.x() - int(titleRect.origin.x),
                               tmpRect.y() - int(titleRect.origin.y));
                titleRect = qt_hirectForQRect(tmpRect);
                HIThemeGetWindowShape(&titleRect, &wdi, wrc, &region);
                ptrHIShapeGetBounds(region, &titleRect);
                ret = qt_qrectForHIRect(titleRect);
            }
        }
        break;
    case CC_ComboBox:
        if (const QStyleOptionComboBox *combo = qstyleoption_cast<const QStyleOptionComboBox *>(opt)) {
            HIThemeButtonDrawInfo bdi;
            d->initComboboxBdi(combo, &bdi, widget, d->getDrawState(opt->state));

            switch (sc) {
            case SC_ComboBoxEditField:{
                ret = QMacStylePrivate::comboboxEditBounds(combo->rect, bdi);
                break; }
            case SC_ComboBoxArrow:{
                ret = QMacStylePrivate::comboboxEditBounds(combo->rect, bdi);
                ret.setX(ret.x() + ret.width());
                ret.setWidth(combo->rect.right() - ret.right());
                break; }
            case SC_ComboBoxListBoxPopup:{
                if (combo->editable) {
                    HIRect inner = QMacStylePrivate::comboboxInnerBounds(qt_hirectForQRect(combo->rect), bdi.kind);
                    QRect editRect = QMacStylePrivate::comboboxEditBounds(combo->rect, bdi);
                    const int comboTop = combo->rect.top();
                    ret = QRect(qRound(inner.origin.x),
                                comboTop,
                                qRound(inner.origin.x - combo->rect.left() + inner.size.width),
                                editRect.bottom() - comboTop + 2);
                } else {
                    QRect editRect = QMacStylePrivate::comboboxEditBounds(combo->rect, bdi);
                    ret = QRect(combo->rect.x() + 4 - 11,
                                combo->rect.y() + 1,
                                editRect.width() + 10 + 11,
                                1);
                 }
                break; }
            default:
                break;
            }
        }
        break;
    case CC_GroupBox:
        if (const QStyleOptionGroupBox *groupBox = qstyleoption_cast<const QStyleOptionGroupBox *>(opt)) {
            bool checkable = groupBox->subControls & SC_GroupBoxCheckBox;
            bool flat = (groupBox->features & QStyleOptionFrameV2::Flat);
            bool hasNoText = !checkable && groupBox->text.isEmpty();
            switch (sc) {
            case SC_GroupBoxLabel:
            case SC_GroupBoxCheckBox: {
                // Cheat and use the smaller font if we need to
                bool checkable = groupBox->subControls & SC_GroupBoxCheckBox;
                bool fontIsSet = (widget && widget->testAttribute(Qt::WA_SetFont))
                                  || !QApplication::desktopSettingsAware();
                int tw;
                int h;
                int margin =  flat || hasNoText ? 0 : 9;
                ret = groupBox->rect.adjusted(margin, 0, -margin, 0);

                if (!fontIsSet) {
                    HIThemeTextInfo tti;
                    tti.version = qt_mac_hitheme_version;
                    tti.state = kThemeStateActive;
                    tti.fontID = flat ? kThemeSystemFont : kThemeSmallSystemFont;
                    tti.horizontalFlushness = kHIThemeTextHorizontalFlushCenter;
                    tti.verticalFlushness = kHIThemeTextVerticalFlushCenter;
                    tti.options = kHIThemeTextBoxOptionNone;
                    tti.truncationPosition = kHIThemeTextTruncationNone;
                    tti.truncationMaxLines = 1 + groupBox->text.count(QLatin1Char('\n'));
                    CGFloat width;
                    CGFloat height;
                    QCFString groupText = qt_mac_removeMnemonics(groupBox->text);
                    HIThemeGetTextDimensions(groupText, 0, &tti, &width, &height, 0);
                    tw = qRound(width);
                    h = qCeil(height);
                } else {
                    QFontMetricsF fm = QFontMetricsF(groupBox->fontMetrics);
                    h = qCeil(fm.height());
                    tw = qCeil(fm.size(Qt::TextShowMnemonic, groupBox->text).width());
                }
                ret.setHeight(h);

                QRect labelRect = alignedRect(groupBox->direction, groupBox->textAlignment,
                                              QSize(tw, h), ret);
                if (flat && checkable)
                    labelRect.moveLeft(labelRect.left() + 4);
                int indicatorWidth = proxy()->pixelMetric(PM_IndicatorWidth, opt, widget);
                bool rtl = groupBox->direction == Qt::RightToLeft;
                if (sc == SC_GroupBoxLabel) {
                    if (checkable) {
                        int newSum = indicatorWidth + 1;
                        int newLeft = labelRect.left() + (rtl ? -newSum : newSum);
                        labelRect.moveLeft(newLeft);
                        if (flat)
                            labelRect.moveTop(labelRect.top() + 3);
                        else
                            labelRect.moveTop(labelRect.top() + 4);
                    } else if (flat) {
                        int newLeft = labelRect.left() - (rtl ? 3 : -3);
                        labelRect.moveLeft(newLeft);
                        labelRect.moveTop(labelRect.top() + 3);
                    } else {
                        int newLeft = labelRect.left() - (rtl ? 3 : 2);
                        labelRect.moveLeft(newLeft);
                        labelRect.moveTop(labelRect.top() + 4);
                    }
                    ret = labelRect;
                }

                if (sc == SC_GroupBoxCheckBox) {
                    int left = rtl ? labelRect.right() - indicatorWidth : labelRect.left() - 1;
                    int top = flat ? ret.top() + 1 : ret.top() + 5;
                    ret.setRect(left, top,
                                indicatorWidth, proxy()->pixelMetric(PM_IndicatorHeight, opt, widget));
                }
                break;
            }
            case SC_GroupBoxContents:
            case SC_GroupBoxFrame: {
                QFontMetrics fm = groupBox->fontMetrics;
                int yOffset = 3;
                if (!flat) {
                    if (widget && !widget->testAttribute(Qt::WA_SetFont)
                            && QApplication::desktopSettingsAware())
                        fm = QFontMetrics(qt_app_fonts_hash()->value("QSmallFont", QFont()));
                    yOffset = 5;
                }

                if (hasNoText)
                    yOffset = -qCeil(QFontMetricsF(fm).height());
                ret = opt->rect.adjusted(0, qCeil(QFontMetricsF(fm).height()) + yOffset, 0, 0);
                if (sc == SC_GroupBoxContents) {
                    if (flat)
                        ret.adjust(3, -5, -3, -4);   // guess too
                    else
                        ret.adjust(3, 3, -3, -4);    // guess
                }
            }
                break;
            default:
                ret = QCommonStyle::subControlRect(cc, groupBox, sc, widget);
                break;
            }
        }
        break;
    case CC_SpinBox:
        if (const QStyleOptionSpinBox *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(opt)) {
            QAquaWidgetSize aquaSize = d->aquaSizeConstrain(spin, widget);
            int spinner_w;
            int spinBoxSep;
            int fw = proxy()->pixelMetric(PM_SpinBoxFrameWidth, spin, widget);
            switch (aquaSize) {
            default:
            case QAquaSizeUnknown:
            case QAquaSizeLarge:
                spinner_w = 14;
                spinBoxSep = 2;
                break;
            case QAquaSizeSmall:
                spinner_w = 12;
                spinBoxSep = 2;
                break;
            case QAquaSizeMini:
                spinner_w = 10;
                spinBoxSep = 1;
                break;
            }

            switch (sc) {
            case SC_SpinBoxUp:
            case SC_SpinBoxDown: {
                if (spin->buttonSymbols == QAbstractSpinBox::NoButtons)
                    break;

                const int y = fw;
                const int x = spin->rect.width() - spinner_w;
                ret.setRect(x + spin->rect.x(), y + spin->rect.y(), spinner_w, spin->rect.height() - y * 2);
                HIThemeButtonDrawInfo bdi;
                bdi.version = qt_mac_hitheme_version;
                bdi.kind = kThemeIncDecButton;
                int hackTranslateX;
                switch (aquaSize) {
                default:
                case QAquaSizeUnknown:
                case QAquaSizeLarge:
                    bdi.kind = kThemeIncDecButton;
                    hackTranslateX = 0;
                    break;
                case QAquaSizeSmall:
                    bdi.kind = kThemeIncDecButtonSmall;
                    hackTranslateX = -2;
                    break;
                case QAquaSizeMini:
                    bdi.kind = kThemeIncDecButtonMini;
                    hackTranslateX = -1;
                    break;
                }
                bdi.state = kThemeStateActive;
                bdi.value = kThemeButtonOff;
                bdi.adornment = kThemeAdornmentNone;
                HIRect hirect = qt_hirectForQRect(ret);

                HIRect outRect;
                HIThemeGetButtonBackgroundBounds(&hirect, &bdi, &outRect);
                ret = qt_qrectForHIRect(outRect);
                switch (sc) {
                case SC_SpinBoxUp:
                    ret.setHeight(ret.height() / 2);
                    break;
                case SC_SpinBoxDown:
                    ret.setY(ret.y() + ret.height() / 2);
                    break;
                default:
                    Q_ASSERT(0);
                    break;
                }
                ret.translate(hackTranslateX, 0); // hack: position the buttons correctly (weird that we need this)
                ret = visualRect(spin->direction, spin->rect, ret);
                break;
            }
            case SC_SpinBoxEditField:
                if (spin->buttonSymbols == QAbstractSpinBox::NoButtons) {
                    ret.setRect(fw, fw,
                                spin->rect.width() - fw * 2,
                                spin->rect.height() - fw * 2);
                } else {
                    ret.setRect(fw, fw,
                                spin->rect.width() - fw * 2 - spinBoxSep - spinner_w,
                                spin->rect.height() - fw * 2);
                }
                ret = visualRect(spin->direction, spin->rect, ret);
                break;
            default:
                ret = QCommonStyle::subControlRect(cc, spin, sc, widget);
                break;
            }
        }
        break;
    case CC_ToolButton:
        ret = QCommonStyle::subControlRect(cc, opt, sc, widget);
        if (sc == SC_ToolButtonMenu && !QStyleHelper::hasAncestor(opt->styleObject, QAccessible::ToolBar)) {
            ret.adjust(-1, 0, 0, 0);
        }
        break;
    default:
        ret = QCommonStyle::subControlRect(cc, opt, sc, widget);
        break;
    }
    return ret;
}

QSize QMacStyle::sizeFromContents(ContentsType ct, const QStyleOption *opt,
                                  const QSize &csz, const QWidget *widget) const
{
    Q_D(const QMacStyle);
    QSize sz(csz);
    bool useAquaGuideline = true;

    switch (ct) {

    case CT_SpinBox:
        if (const QStyleOptionSpinBox *vopt = qstyleoption_cast<const QStyleOptionSpinBox *>(opt)) {
            // Add button + frame widths
            int buttonWidth = 20;
            int fw = proxy()->pixelMetric(PM_SpinBoxFrameWidth, vopt, widget);
            sz += QSize(buttonWidth + 2*fw, 2*fw - 3);
        }
        break;
    case QStyle::CT_TabWidget:
        // the size between the pane and the "contentsRect" (+4,+4)
        // (the "contentsRect" is on the inside of the pane)
        sz = QCommonStyle::sizeFromContents(ct, opt, csz, widget);
        /**
            This is supposed to show the relationship between the tabBar and
            the stack widget of a QTabWidget.
            Unfortunately ascii is not a good way of representing graphics.....
            PS: The '=' line is the painted frame.

               top    ---+
                         |
                         |
                         |
                         |                vvv just outside the painted frame is the "pane"
                      - -|- - - - - - - - - - <-+
            TAB BAR      +=====^============    | +2 pixels
                    - - -|- - -|- - - - - - - <-+
                         |     |      ^   ^^^ just inside the painted frame is the "contentsRect"
                         |     |      |
                         |   overlap  |
                         |     |      |
            bottom ------+   <-+     +14 pixels
                                      |
                                      v
                ------------------------------  <- top of stack widget


        To summarize:
             * 2 is the distance between the pane and the contentsRect
             * The 14 and the 1's are the distance from the contentsRect to the stack widget.
               (same value as used in SE_TabWidgetTabContents)
             * overlap is how much the pane should overlap the tab bar
        */
        // then add the size between the stackwidget and the "contentsRect"

        if (const QStyleOptionTabWidgetFrame *twf
                = qstyleoption_cast<const QStyleOptionTabWidgetFrame *>(opt)) {
            QSize extra(0,0);
            const int overlap = pixelMetric(PM_TabBarBaseOverlap, opt, widget);
            const int gapBetweenTabbarAndStackWidget = 2 + 14 - overlap;

            if (getTabDirection(twf->shape) == kThemeTabNorth || getTabDirection(twf->shape) == kThemeTabSouth) {
                extra = QSize(2, gapBetweenTabbarAndStackWidget + 1);
            } else {
                extra = QSize(gapBetweenTabbarAndStackWidget + 1, 2);
            }
            sz+= extra;
        }

        break;
    case QStyle::CT_TabBarTab:
        if (const QStyleOptionTabV3 *tab = qstyleoption_cast<const QStyleOptionTabV3 *>(opt)) {
            const QAquaWidgetSize AquaSize = d->aquaSizeConstrain(opt, widget);
            const bool differentFont = (widget && widget->testAttribute(Qt::WA_SetFont))
                                       || !QApplication::desktopSettingsAware();
            ThemeTabDirection ttd = getTabDirection(tab->shape);
            bool vertTabs = ttd == kThemeTabWest || ttd == kThemeTabEast;
            if (vertTabs)
                sz.transpose();
            int defaultTabHeight;
            int defaultExtraSpace = proxy()->pixelMetric(PM_TabBarTabHSpace, tab, widget); // Remove spurious gcc warning (AFAIK)
            QFontMetrics fm = opt->fontMetrics;
            switch (AquaSize) {
            case QAquaSizeUnknown:
            case QAquaSizeLarge:
                if (tab->documentMode)
                    defaultTabHeight = 23;
                else
                    defaultTabHeight = 21;
                break;
            case QAquaSizeSmall:
                defaultTabHeight = 18;
                break;
            case QAquaSizeMini:
                defaultTabHeight = 16;
                break;
            }
            bool setWidth = false;
            if (differentFont || !tab->icon.isNull()) {
                sz.rheight() = qMax(defaultTabHeight, sz.height());
            } else {
                QSize textSize = fm.size(Qt::TextShowMnemonic, tab->text);
                sz.rheight() = qMax(defaultTabHeight, textSize.height());
                sz.rwidth() = textSize.width() + defaultExtraSpace;
                setWidth = true;
            }

            if (vertTabs)
                sz.transpose();

            int maxWidgetHeight = qMax(tab->leftButtonSize.height(), tab->rightButtonSize.height());
            int maxWidgetWidth = qMax(tab->leftButtonSize.width(), tab->rightButtonSize.width());

            int widgetWidth = 0;
            int widgetHeight = 0;
            int padding = 0;
            if (tab->leftButtonSize.isValid()) {
                padding += 8;
                widgetWidth += tab->leftButtonSize.width();
                widgetHeight += tab->leftButtonSize.height();
            }
            if (tab->rightButtonSize.isValid()) {
                padding += 8;
                widgetWidth += tab->rightButtonSize.width();
                widgetHeight += tab->rightButtonSize.height();
            }

            if (vertTabs) {
                sz.setHeight(sz.height() + widgetHeight + padding);
                sz.setWidth(qMax(sz.width(), maxWidgetWidth));
            } else {
                if (setWidth)
                    sz.setWidth(sz.width() + widgetWidth + padding);
                sz.setHeight(qMax(sz.height(), maxWidgetHeight));
            }
        }
        break;
    case QStyle::CT_PushButton:
        // By default, we fit the contents inside a normal rounded push button.
        // Do this by add enough space around the contents so that rounded
        // borders (including highlighting when active) will show.
        sz.rwidth() += QMacStylePrivate::PushButtonLeftOffset + QMacStylePrivate::PushButtonRightOffset + 12;
        if (opt->state & QStyle::State_Small)
            sz.rheight() += 14;
        else
            sz.rheight() += 4;
        break;
    case QStyle::CT_MenuItem:
        if (const QStyleOptionMenuItem *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(opt)) {
            int maxpmw = mi->maxIconWidth;
            const QComboBox *comboBox = qobject_cast<const QComboBox *>(widget);
            int w = sz.width(),
                h = sz.height();
            if (mi->menuItemType == QStyleOptionMenuItem::Separator) {
                w = 10;
                SInt16 ash;
                GetThemeMenuSeparatorHeight(&ash);
                h = ash;
            } else {
                h = mi->fontMetrics.height() + 2;
                if (!mi->icon.isNull()) {
                    if (comboBox) {
                        const QSize &iconSize = comboBox->iconSize();
                        h = qMax(h, iconSize.height() + 4);
                        maxpmw = qMax(maxpmw, iconSize.width());
                    } else {
                        int iconExtent = proxy()->pixelMetric(PM_SmallIconSize);
                        h = qMax(h, mi->icon.actualSize(QSize(iconExtent, iconExtent)).height() + 4);
                    }
                }
            }
            if (mi->text.contains(QLatin1Char('\t')))
                w += 12;
            if (mi->menuItemType == QStyleOptionMenuItem::SubMenu)
                w += 20;
            if (maxpmw)
                w += maxpmw + 6;
            // add space for a check. All items have place for a check too.
            w += 20;
            if (comboBox && comboBox->isVisible()) {
                QStyleOptionComboBox cmb;
                cmb.initFrom(comboBox);
                cmb.editable = false;
                cmb.subControls = QStyle::SC_ComboBoxEditField;
                cmb.activeSubControls = QStyle::SC_None;
                w = qMax(w, subControlRect(QStyle::CC_ComboBox, &cmb,
                                                   QStyle::SC_ComboBoxEditField,
                                                   comboBox).width());
            } else {
                w += 12;
            }
            sz = QSize(w, h);
        }
        break;
    case CT_MenuBarItem:
        if (!sz.isEmpty())
            sz += QSize(12, 4); // Constants from QWindowsStyle
        break;
    case CT_ToolButton:
        sz.rwidth() += 10;
        sz.rheight() += 10;
        return sz;
    case CT_ComboBox:
        sz.rwidth() += 50;
        sz.rheight() += 2;
        break;
    case CT_Menu: {
        QStyleHintReturnMask menuMask;
        QStyleOption myOption = *opt;
        myOption.rect.setSize(sz);
        if (proxy()->styleHint(SH_Menu_Mask, &myOption, widget, &menuMask)) {
            sz = menuMask.region.boundingRect().size();
        }
        break; }
    case CT_HeaderSection:{
        const QStyleOptionHeader *header = qstyleoption_cast<const QStyleOptionHeader *>(opt);
        sz = QCommonStyle::sizeFromContents(ct, opt, csz, widget);
        if (header->text.contains(QLatin1Char('\n')))
            useAquaGuideline = false;
        break; }
    case CT_ScrollBar :
        // Make sure that the scroll bar is large enough to display the thumb indicator.
        if (const QStyleOptionSlider *slider = qstyleoption_cast<const QStyleOptionSlider *>(opt)) {
            const int minimumSize = scrollButtonsCutoffSize(thumbIndicatorCutoff, widgetSizePolicy(widget, opt));
            if (slider->orientation == Qt::Horizontal)
                sz = sz.expandedTo(QSize(minimumSize, sz.height()));
            else
                sz = sz.expandedTo(QSize(sz.width(), minimumSize));
        }
        break;
    case CT_ItemViewItem:
        if (const QStyleOptionViewItem *vopt = qstyleoption_cast<const QStyleOptionViewItem *>(opt)) {
            sz = QCommonStyle::sizeFromContents(ct, vopt, csz, widget);
            sz.setHeight(sz.height() + 2);
        }
        break;

    default:
        sz = QCommonStyle::sizeFromContents(ct, opt, csz, widget);
    }

    if (useAquaGuideline){
        QSize macsz;
        if (d->aquaSizeConstrain(opt, widget, ct, sz, &macsz) != QAquaSizeUnknown) {
            if (macsz.width() != -1)
                sz.setWidth(macsz.width());
            if (macsz.height() != -1)
                sz.setHeight(macsz.height());
        }
    }

    // The sizes that Carbon and the guidelines gives us excludes the focus frame.
    // We compensate for this by adding some extra space here to make room for the frame when drawing:
    if (const QStyleOptionComboBox *combo = qstyleoption_cast<const QStyleOptionComboBox *>(opt)){
        QAquaWidgetSize widgetSize = d->aquaSizeConstrain(opt, widget);
        int bkind = 0;
        switch (widgetSize) {
        default:
        case QAquaSizeLarge:
            bkind = combo->editable ? kThemeComboBox : kThemePopupButton;
            break;
        case QAquaSizeSmall:
            bkind = combo->editable ? int(kThemeComboBoxSmall) : int(kThemePopupButtonSmall);
            break;
        case QAquaSizeMini:
            bkind = combo->editable ? kThemeComboBoxMini : kThemePopupButtonMini;
            break;
        }
        HIRect tmpRect = {{0, 0}, {0, 0}};
        HIRect diffRect = QMacStylePrivate::comboboxInnerBounds(tmpRect, bkind);
        sz.rwidth() -= qRound(diffRect.size.width);
        sz.rheight() -= qRound(diffRect.size.height);
    } else if (ct == CT_PushButton || ct == CT_ToolButton){
        ThemeButtonKind bkind;
        QAquaWidgetSize widgetSize = d->aquaSizeConstrain(opt, widget);
        switch (ct) {
        default:
        case CT_PushButton:
            if (const QStyleOptionButton *btn = qstyleoption_cast<const QStyleOptionButton *>(opt)) {
                if (btn->features & QStyleOptionButton::CommandLinkButton) {
                    return QCommonStyle::sizeFromContents(ct, opt, sz, widget);
                }
            }

            switch (widgetSize) {
            default:
            case QAquaSizeLarge:
                bkind = kThemePushButton;
                break;
            case QAquaSizeSmall:
                bkind = kThemePushButtonSmall;
                break;
            case QAquaSizeMini:
                bkind = kThemePushButtonMini;
                break;
            }
            break;
        case CT_ToolButton:
            switch (widgetSize) {
            default:
            case QAquaSizeLarge:
                bkind = kThemeLargeBevelButton;
                break;
            case QAquaSizeMini:
            case QAquaSizeSmall:
                bkind = kThemeSmallBevelButton;
            }
            break;
        }

        HIThemeButtonDrawInfo bdi;
        bdi.version = qt_mac_hitheme_version;
        bdi.state = kThemeStateActive;
        bdi.kind = bkind;
        bdi.value = kThemeButtonOff;
        bdi.adornment = kThemeAdornmentNone;
        HIRect macRect, myRect;
        myRect = CGRectMake(0, 0, sz.width(), sz.height());
        HIThemeGetButtonBackgroundBounds(&myRect, &bdi, &macRect);
        // Mini buttons only return their actual size in HIThemeGetButtonBackgroundBounds, so help them out a bit (guess),
        if (bkind == kThemePushButtonMini)
            macRect.size.height += 8.;
        else if (bkind == kThemePushButtonSmall)
            macRect.size.height -= 10;
        sz.setWidth(sz.width() + int(macRect.size.width - myRect.size.width));
        sz.setHeight(sz.height() + int(macRect.size.height - myRect.size.height));
    }
    return sz;
}

void QMacStyle::drawItemText(QPainter *p, const QRect &r, int flags, const QPalette &pal,
                             bool enabled, const QString &text, QPalette::ColorRole textRole) const
{
    if(flags & Qt::TextShowMnemonic)
        flags |= Qt::TextHideMnemonic;
    QCommonStyle::drawItemText(p, r, flags, pal, enabled, text, textRole);
}

bool QMacStyle::event(QEvent *e)
{
    Q_D(QMacStyle);
    if(e->type() == QEvent::FocusIn) {
        QWidget *f = 0;
        QWidget *focusWidget = QApplication::focusWidget();
#ifndef QT_NO_GRAPHICSVIEW
        if (QGraphicsView *graphicsView = qobject_cast<QGraphicsView *>(focusWidget)) {
            QGraphicsItem *focusItem = graphicsView->scene() ? graphicsView->scene()->focusItem() : 0;
            if (focusItem && focusItem->type() == QGraphicsProxyWidget::Type) {
                QGraphicsProxyWidget *proxy = static_cast<QGraphicsProxyWidget *>(focusItem);
                if (proxy->widget())
                    focusWidget = proxy->widget()->focusWidget();
            }
        }
#endif
        if (focusWidget && focusWidget->testAttribute(Qt::WA_MacShowFocusRect)) {
            f = focusWidget;
            QWidget *top = f->parentWidget();
            while (top && !top->isWindow() && !(top->windowType() == Qt::SubWindow))
                top = top->parentWidget();
#ifndef QT_NO_MAINWINDOW
            if (qobject_cast<QMainWindow *>(top)) {
                QWidget *central = static_cast<QMainWindow *>(top)->centralWidget();
                for (const QWidget *par = f; par; par = par->parentWidget()) {
                    if (par == central) {
                        top = central;
                        break;
                    }
                    if (par->isWindow())
                        break;
                }
            }
#endif
        }
        if (f) {
            if(!d->focusWidget)
                d->focusWidget = new QFocusFrame(f);
            d->focusWidget->setWidget(f);
        } else if(d->focusWidget) {
            d->focusWidget->setWidget(0);
        }
    } else if(e->type() == QEvent::FocusOut) {
        if(d->focusWidget)
            d->focusWidget->setWidget(0);
    }
    return false;
}

QIcon QMacStyle::standardIcon(StandardPixmap standardIcon, const QStyleOption *opt,
                              const QWidget *widget) const
{
    switch (standardIcon) {
    default:
        return QCommonStyle::standardIcon(standardIcon, opt, widget);
    case SP_ToolBarHorizontalExtensionButton:
    case SP_ToolBarVerticalExtensionButton: {
        QPixmap pixmap(QLatin1String(":/qt-project.org/styles/macstyle/images/toolbar-ext.png"));
        if (standardIcon == SP_ToolBarVerticalExtensionButton) {
            QPixmap pix2(pixmap.height(), pixmap.width());
            pix2.setDevicePixelRatio(pixmap.devicePixelRatio());
            pix2.fill(Qt::transparent);
            QPainter p(&pix2);
            p.translate(pix2.width(), 0);
            p.rotate(90);
            p.drawPixmap(0, 0, pixmap);
            return pix2;
        }
        return pixmap;
    }
    }
}

int QMacStyle::layoutSpacing(QSizePolicy::ControlType control1,
                             QSizePolicy::ControlType control2,
                             Qt::Orientation orientation,
                             const QStyleOption *option,
                             const QWidget *widget) const
{
    const int ButtonMask = QSizePolicy::ButtonBox | QSizePolicy::PushButton;
    bool isMetal = (widget && widget->testAttribute(Qt::WA_MacBrushedMetal));
    int controlSize = getControlSize(option, widget);

    if (control2 == QSizePolicy::ButtonBox) {
        /*
            AHIG seems to prefer a 12-pixel margin between group
            boxes and the row of buttons. The 20 pixel comes from
            Builder.
        */
        if (isMetal                                         // (AHIG, guess, guess)
                || (control1 & (QSizePolicy::Frame          // guess
                                | QSizePolicy::GroupBox     // (AHIG, guess, guess)
                                | QSizePolicy::TabWidget    // guess
                                | ButtonMask)))    {        // AHIG
            return_SIZE(14, 8, 8);
        } else if (control1 == QSizePolicy::LineEdit) {
            return_SIZE(8, 8, 8); // Interface Builder
        } else {
            return_SIZE(20, 7, 7); // Interface Builder
        }
    }

    if ((control1 | control2) & ButtonMask) {
        if (control1 == QSizePolicy::LineEdit)
            return_SIZE(8, 8, 8); // Interface Builder
        else if (control2 == QSizePolicy::LineEdit) {
            if (orientation == Qt::Vertical)
                return_SIZE(20, 7, 7); // Interface Builder
            else
                return_SIZE(20, 8, 8);
        }
        return_SIZE(14, 8, 8);     // Interface Builder
    }

    switch (CT2(control1, control2)) {
    case CT1(QSizePolicy::Label):                             // guess
    case CT2(QSizePolicy::Label, QSizePolicy::DefaultType):   // guess
    case CT2(QSizePolicy::Label, QSizePolicy::CheckBox):      // AHIG
    case CT2(QSizePolicy::Label, QSizePolicy::ComboBox):      // AHIG
    case CT2(QSizePolicy::Label, QSizePolicy::LineEdit):      // guess
    case CT2(QSizePolicy::Label, QSizePolicy::RadioButton):   // AHIG
    case CT2(QSizePolicy::Label, QSizePolicy::Slider):        // guess
    case CT2(QSizePolicy::Label, QSizePolicy::SpinBox):       // guess
    case CT2(QSizePolicy::Label, QSizePolicy::ToolButton):    // guess
        return_SIZE(8, 6, 5);
    case CT1(QSizePolicy::ToolButton):
        return 8;   // AHIG
    case CT1(QSizePolicy::CheckBox):
    case CT2(QSizePolicy::CheckBox, QSizePolicy::RadioButton):
    case CT2(QSizePolicy::RadioButton, QSizePolicy::CheckBox):
        if (orientation == Qt::Vertical)
            return_SIZE(8, 8, 7);        // AHIG and Builder
        break;
    case CT1(QSizePolicy::RadioButton):
        if (orientation == Qt::Vertical)
            return 5;                   // (Builder, guess, AHIG)
    }

    if (orientation == Qt::Horizontal
            && (control2 & (QSizePolicy::CheckBox | QSizePolicy::RadioButton)))
        return_SIZE(12, 10, 8);        // guess

    if ((control1 | control2) & (QSizePolicy::Frame
                                 | QSizePolicy::GroupBox
                                 | QSizePolicy::TabWidget)) {
        /*
            These values were chosen so that nested container widgets
            look good side by side. Builder uses 8, which looks way
            too small, and AHIG doesn't say anything.
        */
        return_SIZE(16, 10, 10);    // guess
    }

    if ((control1 | control2) & (QSizePolicy::Line | QSizePolicy::Slider))
        return_SIZE(12, 10, 8);     // AHIG

    if ((control1 | control2) & QSizePolicy::LineEdit)
        return_SIZE(10, 8, 8);      // AHIG

    /*
        AHIG and Builder differ by up to 4 pixels for stacked editable
        comboboxes. We use some values that work fairly well in all
        cases.
    */
    if ((control1 | control2) & QSizePolicy::ComboBox)
        return_SIZE(10, 8, 7);      // guess

    /*
        Builder defaults to 8, 6, 5 in lots of cases, but most of the time the
        result looks too cramped.
    */
    return_SIZE(10, 8, 6);  // guess
}

static void qt_mac_clip_cg(CGContextRef hd, const QRegion &rgn, CGAffineTransform *orig_xform)
{
    CGAffineTransform old_xform = CGAffineTransformIdentity;
    if (orig_xform) { //setup xforms
        old_xform = CGContextGetCTM(hd);
        CGContextConcatCTM(hd, CGAffineTransformInvert(old_xform));
        CGContextConcatCTM(hd, *orig_xform);
    }

    //do the clipping
    CGContextBeginPath(hd);
    if (rgn.isEmpty()) {
        CGContextAddRect(hd, CGRectMake(0, 0, 0, 0));
    } else {
        QCFType<HIMutableShapeRef> shape = qt_mac_toHIMutableShape(rgn);
        Q_ASSERT(!HIShapeIsEmpty(shape));
        HIShapeReplacePathInCGContext(shape, hd);
    }
    CGContextClip(hd);

    if (orig_xform) {//reset xforms
        CGContextConcatCTM(hd, CGAffineTransformInvert(CGContextGetCTM(hd)));
        CGContextConcatCTM(hd, old_xform);
    }
}

// move to QRegion?
void qt_mac_scale_region(QRegion *region, qreal scaleFactor)
{
    if (!region || !region->rectCount())
        return;

    QVector<QRect> scaledRects;
    scaledRects.reserve(region->rects().count());

    foreach (const QRect &rect, region->rects()) {
        scaledRects.append(QRect(rect.topLeft() * scaleFactor, rect.size() * scaleFactor));
    }
    region->setRects(&scaledRects[0], scaledRects.count());
}

static CGColorSpaceRef qt_mac_colorSpaceForDeviceType(const QPaintDevice *paintDevice);

namespace {
QMacCGContext::QMacCGContext(QPainter *p)
{
    QPaintEngine *pe = p->paintEngine();
    pe->syncState();
    context = 0;

    int devType = p->device()->devType();
    if (pe->type() == QPaintEngine::Raster
            && (devType == QInternal::Widget ||
                devType == QInternal::Pixmap ||
                devType == QInternal::Image)) {

        CGColorSpaceRef colorspace = qt_mac_colorSpaceForDeviceType(pe->paintDevice());
        uint flags = kCGImageAlphaPremultipliedFirst;
        flags |= kCGBitmapByteOrder32Host;

        const QImage *image = (const QImage *) pe->paintDevice();

        context = CGBitmapContextCreate((void *) image->bits(), image->width(), image->height(),
                                        8, image->bytesPerLine(), colorspace, flags);

        // Invert y axis.
        CGContextTranslateCTM(context, 0, image->height());
        CGContextScaleCTM(context, 1, -1);

        const qreal devicePixelRatio = image->devicePixelRatio();

        if (devType == QInternal::Widget) {
            // Set the clip rect which is an intersection of the system clip
            // and the painter clip. To make matters more interesting these
            // are in device pixels and device-independent pixels, respectively.
            QRegion clip = p->paintEngine()->systemClip(); // get system clip in device pixels
            QTransform native = p->deviceTransform();      // get device transform. dx/dy is in device pixels

            if (p->hasClipping()) {
                QRegion r = p->clipRegion();               // get painter clip, which is in device-independent pixels
                qt_mac_scale_region(&r, devicePixelRatio); // scale painter clip to device pixels
                r.translate(native.dx(), native.dy());
                if (clip.isEmpty())
                    clip = r;
                else
                    clip &= r;
            }
            qt_mac_clip_cg(context, clip, 0); // clip in device pixels

            // Scale the context so that painting happens in device-independet pixels.
            CGContextScaleCTM(context, devicePixelRatio, devicePixelRatio);
            CGContextTranslateCTM(context, native.dx() / devicePixelRatio, native.dy() / devicePixelRatio);
        } else {
            // Scale to paint in device-independent pixels.
            CGContextScaleCTM(context, devicePixelRatio, devicePixelRatio);
        }
    } else {
        qDebug() << "QMacCGContext:: Unsupported painter devtype type" << devType;
    }
}

} // anonymous namespace

static CGColorSpaceRef qt_mac_colorSpaceForDeviceType(const QPaintDevice *paintDevice)
{
    bool isWidget = (paintDevice->devType() == QInternal::Widget);
    return qt_mac_displayColorSpace(isWidget ? static_cast<const QWidget *>(paintDevice) : 0);
}

/*! \internal

    Returns the CoreGraphics CGContextRef of the paint device. 0 is
    returned if it can't be obtained. It is the caller's responsibility to
    CGContextRelease the context when finished using it.

    \warning This function is only available on Mac OS X.
    \warning This function is duplicated in the Cocoa platform plugin.
*/

CGContextRef qt_mac_cg_context(const QPaintDevice *pdev)
{
    if (pdev->devType() == QInternal::Pixmap) {
        const QPixmap *pm = static_cast<const QPixmap*>(pdev);
        CGColorSpaceRef colorspace = qt_mac_colorSpaceForDeviceType(pdev);
        uint flags = kCGImageAlphaPremultipliedFirst;
        flags |= kCGBitmapByteOrder32Host;
        CGContextRef ret = 0;

        QPlatformPixmap *data = const_cast<QPixmap *>(pm)->data_ptr().data();
        if (data->classId() == QPlatformPixmap::RasterClass) {
            QImage *image = data->buffer();
            ret = CGBitmapContextCreate(image->bits(), image->width(), image->height(),
                                        8, image->bytesPerLine(), colorspace, flags);
        } else {
            qDebug() << "qt_mac_cg_context: Unsupported pixmap class";
        }

        CGContextTranslateCTM(ret, 0, pm->height());
        int devicePixelRatio = pdev->devicePixelRatio();
        CGContextScaleCTM(ret, devicePixelRatio, devicePixelRatio);
        CGContextScaleCTM(ret, 1, -1);
        return ret;
    } else if (pdev->devType() == QInternal::Widget) {
        //CGContextRef ret = static_cast<CGContextRef>(static_cast<const QWidget *>(pdev)->macCGHandle());
        ///CGContextRetain(ret);
        //return ret;
        qDebug() << "qt_mac_cg_context: not implemented: Widget class";
        return 0;
    }
    return 0;
}

/*
FontHash::FontHash()
{
    QHash<QByteArray, QFont>::operator=(QGuiApplicationPrivate::platformIntegration()->fontDatabase()->defaultFonts());
}

Q_GLOBAL_STATIC(FontHash, app_fonts)
FontHash *qt_app_fonts_hash()
{
    return app_fonts();
}
*/
QT_END_NAMESPACE
