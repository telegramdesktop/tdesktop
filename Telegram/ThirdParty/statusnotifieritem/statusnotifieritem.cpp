/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * LXQt - a lightweight, Qt based, desktop toolset
 * https://lxqt.org/
 *
 * Copyright: 2015 LXQt team
 * Authors:
 *   Paulo Lieuthier <paulolieuthier@gmail.com>
 *
 * This program or library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * END_COMMON_COPYRIGHT_HEADER */

#include "statusnotifieritem.h"
#include "statusnotifieritemadaptor.h"
#include <QDBusServiceWatcher>
#include <QDBusMessage>
#include <dbusmenuexporter.h>

int StatusNotifierItem::mServiceCounter = 0;

StatusNotifierItem::StatusNotifierItem(QString id, QObject *parent)
    : QObject(parent),
    mAdaptor(new StatusNotifierItemAdaptor(this)),
    mService(QString::fromLatin1("org.freedesktop.StatusNotifierItem-%1-%2")
             .arg(QCoreApplication::applicationPid())
             .arg(++mServiceCounter)),
    mId(id),
    mTitle(QLatin1String("Test")),
    mStatus(QLatin1String("Active")),
    mCategory(QLatin1String("ApplicationStatus")),
    mMenu(nullptr),
    mMenuPath(QLatin1String("/NO_DBUSMENU")),
    mMenuExporter(nullptr),
    mSessionBus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, mService))
{
    // Separate DBus connection to the session bus is created, because QDbus does not provide
    // a way to register different objects for different services with the same paths.
    // For status notifiers we need different /StatusNotifierItem for each service.

    // register service

    mSessionBus.registerObject(QLatin1String("/StatusNotifierItem"), this);

    registerToHost();

    // monitor the watcher service in case the host restarts
    QDBusServiceWatcher *watcher = new QDBusServiceWatcher(QLatin1String("org.kde.StatusNotifierWatcher"),
                                                           mSessionBus,
                                                           QDBusServiceWatcher::WatchForOwnerChange,
                                                           this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, &StatusNotifierItem::onServiceOwnerChanged);
}

StatusNotifierItem::~StatusNotifierItem()
{
    mSessionBus.unregisterObject(QLatin1String("/StatusNotifierItem"));
    QDBusConnection::disconnectFromBus(mService);
}

void StatusNotifierItem::registerToHost()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String("org.kde.StatusNotifierWatcher"),
                                                          QLatin1String("/StatusNotifierWatcher"),
                                                          QLatin1String("org.kde.StatusNotifierWatcher"),
                                                          QLatin1String("RegisterStatusNotifierItem"));

    message.setArguments({
        mSessionBus.baseService()
    });

    mSessionBus.send(message);
}

void StatusNotifierItem::onServiceOwnerChanged(const QString& service, const QString& oldOwner,
                                               const QString& newOwner)
{
	Q_UNUSED(service);
	Q_UNUSED(oldOwner);

    if (!newOwner.isEmpty())
        registerToHost();
}

void StatusNotifierItem::onMenuDestroyed()
{
    mMenu = nullptr;
    setMenuPath(QLatin1String("/NO_DBUSMENU"));
    mMenuExporter = nullptr; //mMenu is a QObject parent of the mMenuExporter
}

void StatusNotifierItem::setTitle(const QString &title)
{
    if (mTitle == title)
        return;

    mTitle = title;
    Q_EMIT mAdaptor->NewTitle();
}

void StatusNotifierItem::setStatus(const QString &status)
{
    if (mStatus == status)
        return;

    mStatus = status;
    Q_EMIT mAdaptor->NewStatus(mStatus);
}

void StatusNotifierItem::setCategory(const QString &category)
{
    if (mCategory == category)
        return;

    mCategory = category;
}

void StatusNotifierItem::setMenuPath(const QString& path)
{
    mMenuPath.setPath(path);
}

void StatusNotifierItem::setIconByName(const QString &name)
{
    if (mIconName == name)
        return;

    mIconName = name;
    Q_EMIT mAdaptor->NewIcon();
}

void StatusNotifierItem::setIconByPixmap(const QIcon &icon)
{
    if (mIconCacheKey == icon.cacheKey())
        return;

    mIconCacheKey = icon.cacheKey();
    mIcon = iconToPixmapList(icon);
    mIconName.clear();
    Q_EMIT mAdaptor->NewIcon();
}

void StatusNotifierItem::setOverlayIconByName(const QString &name)
{
    if (mOverlayIconName == name)
        return;

    mOverlayIconName = name;
    Q_EMIT mAdaptor->NewOverlayIcon();
}

void StatusNotifierItem::setOverlayIconByPixmap(const QIcon &icon)
{
    if (mOverlayIconCacheKey == icon.cacheKey())
        return;

    mOverlayIconCacheKey = icon.cacheKey();
    mOverlayIcon = iconToPixmapList(icon);
    mOverlayIconName.clear();
    Q_EMIT mAdaptor->NewOverlayIcon();
}

void StatusNotifierItem::setAttentionIconByName(const QString &name)
{
    if (mAttentionIconName == name)
        return;

    mAttentionIconName = name;
    Q_EMIT mAdaptor->NewAttentionIcon();
}

void StatusNotifierItem::setAttentionIconByPixmap(const QIcon &icon)
{
    if (mAttentionIconCacheKey == icon.cacheKey())
        return;

    mAttentionIconCacheKey = icon.cacheKey();
    mAttentionIcon = iconToPixmapList(icon);
    mAttentionIconName.clear();
    Q_EMIT mAdaptor->NewAttentionIcon();
}

void StatusNotifierItem::setToolTipTitle(const QString &title)
{
    if (mTooltipTitle == title)
        return;

    mTooltipTitle = title;
    Q_EMIT mAdaptor->NewToolTip();
}

void StatusNotifierItem::setToolTipSubTitle(const QString &subTitle)
{
    if (mTooltipSubtitle == subTitle)
        return;

    mTooltipSubtitle = subTitle;
    Q_EMIT mAdaptor->NewToolTip();
}

void StatusNotifierItem::setToolTipIconByName(const QString &name)
{
    if (mTooltipIconName == name)
        return;

    mTooltipIconName = name;
    Q_EMIT mAdaptor->NewToolTip();
}

void StatusNotifierItem::setToolTipIconByPixmap(const QIcon &icon)
{
    if (mTooltipIconCacheKey == icon.cacheKey())
        return;

    mTooltipIconCacheKey = icon.cacheKey();
    mTooltipIcon = iconToPixmapList(icon);
    mTooltipIconName.clear();
    Q_EMIT mAdaptor->NewToolTip();
}

void StatusNotifierItem::setContextMenu(QMenu* menu)
{
    if (mMenu == menu)
        return;

    if (nullptr != mMenu)
    {
        disconnect(mMenu, &QObject::destroyed, this, &StatusNotifierItem::onMenuDestroyed);
    }
    mMenu = menu;

    if (nullptr != mMenu)
        setMenuPath(QLatin1String("/MenuBar"));
    else
        setMenuPath(QLatin1String("/NO_DBUSMENU"));

    //Note: we need to destroy menu exporter before creating new one -> to free the DBus object path for new menu
    delete mMenuExporter;
    if (nullptr != mMenu)
    {
        connect(mMenu, &QObject::destroyed, this, &StatusNotifierItem::onMenuDestroyed);
        mMenuExporter = new DBusMenuExporter{this->menu().path(), mMenu, mSessionBus};
    }
}

void StatusNotifierItem::Activate(int x, int y)
{
    if (mStatus == QLatin1String("NeedsAttention"))
        mStatus = QLatin1String("Active");

    Q_EMIT activateRequested(QPoint(x, y));
}

void StatusNotifierItem::SecondaryActivate(int x, int y)
{
    if (mStatus == QLatin1String("NeedsAttention"))
        mStatus = QLatin1String("Active");

    Q_EMIT secondaryActivateRequested(QPoint(x, y));
}

void StatusNotifierItem::ContextMenu(int x, int y)
{
    if (mMenu)
    {
        if (mMenu->isVisible())
            mMenu->popup(QPoint(x, y));
        else
            mMenu->hide();
    }
}

void StatusNotifierItem::Scroll(int delta, const QString &orientation)
{
    Qt::Orientation orient = Qt::Vertical;
    if (orientation.toLower() == QLatin1String("horizontal"))
        orient = Qt::Horizontal;

    Q_EMIT scrollRequested(delta, orient);
}

void StatusNotifierItem::showMessage(const QString& title, const QString& msg,
                                     const QString& iconName, int secs)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String("org.freedesktop.Notifications"),
                                                          QLatin1String("/org/freedesktop/Notifications"),
                                                          QLatin1String("org.freedesktop.Notifications"),
                                                          QLatin1String("Notify"));

    message.setArguments({
        mTitle,
        (uint) 0,
        iconName,
        title,
        msg,
        QStringList(),
        QVariantMap(),
        secs
    });

    mSessionBus.send(message);
}

IconPixmapList StatusNotifierItem::iconToPixmapList(const QIcon& icon)
{
    IconPixmapList pixmapList;

    // long live KDE!
    const QList<QSize> sizes = icon.availableSizes();
    for (const QSize &size : sizes)
    {
        QImage image = icon.pixmap(size).toImage();

        IconPixmap pix;
        pix.height = image.height();
        pix.width = image.width();

        if (image.format() != QImage::Format_ARGB32)
            image = image.convertToFormat(QImage::Format_ARGB32);

        pix.bytes = QByteArray((char *) image.bits(), image.sizeInBytes());

        // swap to network byte order if we are little endian
        if (QSysInfo::ByteOrder == QSysInfo::LittleEndian)
        {
            quint32 *uintBuf = (quint32 *) pix.bytes.data();
            for (uint i = 0; i < pix.bytes.size() / sizeof(quint32); ++i)
            {
                *uintBuf = qToBigEndian(*uintBuf);
                ++uintBuf;
            }
        }

        pixmapList.append(pix);
    }

    return pixmapList;
}
