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


#ifndef STATUS_NOTIFIER_ITEM_H
#define STATUS_NOTIFIER_ITEM_H

#include <QObject>
#include <QIcon>
#include <QMenu>
#include <QDBusConnection>

#include "dbustypes.h"

class StatusNotifierItemAdaptor;
class DBusMenuExporter;

class StatusNotifierItem : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString Category READ category)
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QString Id READ id)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)

    Q_PROPERTY(QString IconName READ iconName)
    Q_PROPERTY(IconPixmapList IconPixmap READ iconPixmap)

    Q_PROPERTY(QString OverlayIconName READ overlayIconName)
    Q_PROPERTY(IconPixmapList OverlayIconPixmap READ overlayIconPixmap)

    Q_PROPERTY(QString AttentionIconName READ attentionIconName)
    Q_PROPERTY(IconPixmapList AttentionIconPixmap READ attentionIconPixmap)

    Q_PROPERTY(ToolTip ToolTip READ toolTip)

public:
    StatusNotifierItem(QString id, QObject *parent = nullptr);
    ~StatusNotifierItem() override;

    QString id() const
    { return mId; }

    QString title() const
    { return mTitle; }
    void setTitle(const QString &title);

    QString status() const
    { return mStatus; }
    void setStatus(const QString &status);

    QString category() const
    { return mCategory; }
    void setCategory(const QString &category);

    QDBusObjectPath menu() const
    { return mMenuPath; }
    void setMenuPath(const QString &path);

    QString iconName() const
    { return mIconName; }
    void setIconByName(const QString &name);

    IconPixmapList iconPixmap() const
    { return mIcon; }
    void setIconByPixmap(const QIcon &icon);

    QString overlayIconName() const
    { return mOverlayIconName; }
    void setOverlayIconByName(const QString &name);

    IconPixmapList overlayIconPixmap() const
    { return mOverlayIcon; }
    void setOverlayIconByPixmap(const QIcon &icon);

    QString attentionIconName() const
    { return mAttentionIconName; }
    void setAttentionIconByName(const QString &name);

    IconPixmapList attentionIconPixmap() const
    { return mAttentionIcon; }
    void setAttentionIconByPixmap(const QIcon &icon);

    QString toolTipTitle() const
    { return mTooltipTitle; }
    void setToolTipTitle(const QString &title);

    QString toolTipSubTitle() const
    { return mTooltipSubtitle; }
    void setToolTipSubTitle(const QString &subTitle);

    QString toolTipIconName() const
    { return mTooltipIconName; }
    void setToolTipIconByName(const QString &name);

    IconPixmapList toolTipIconPixmap() const
    { return mTooltipIcon; }
    void setToolTipIconByPixmap(const QIcon &icon);

    ToolTip toolTip() const
    {
        ToolTip tt;
        tt.title = mTooltipTitle;
        tt.description = mTooltipSubtitle;
        tt.iconName = mTooltipIconName;
        tt.iconPixmap = mTooltipIcon;
        return tt;
    }

    /*!
     * \Note: we don't take ownership for the \param menu
     */
    void setContextMenu(QMenu *menu);

public Q_SLOTS:
    void Activate(int x, int y);
    void SecondaryActivate(int x, int y);
    void ContextMenu(int x, int y);
    void Scroll(int delta, const QString &orientation);

    void showMessage(const QString &title, const QString &msg, const QString &iconName, int secs);

private:
    void registerToHost();
    IconPixmapList iconToPixmapList(const QIcon &icon);

private Q_SLOTS:
    void onServiceOwnerChanged(const QString &service, const QString &oldOwner,
                               const QString &newOwner);
    void onMenuDestroyed();

Q_SIGNALS:
    void activateRequested(const QPoint &pos);
    void secondaryActivateRequested(const QPoint &pos);
    void scrollRequested(int delta, Qt::Orientation orientation);

private:
    StatusNotifierItemAdaptor *mAdaptor;

    QString mService;
    QString mId;
    QString mTitle;
    QString mStatus;
    QString mCategory;

    // icons
    QString mIconName, mOverlayIconName, mAttentionIconName;
    IconPixmapList mIcon, mOverlayIcon, mAttentionIcon;
    qint64 mIconCacheKey, mOverlayIconCacheKey, mAttentionIconCacheKey;

    // tooltip
    QString mTooltipTitle, mTooltipSubtitle, mTooltipIconName;
    IconPixmapList mTooltipIcon;
    qint64 mTooltipIconCacheKey;

    // menu
    QMenu *mMenu;
    QDBusObjectPath mMenuPath;
    DBusMenuExporter *mMenuExporter;
    QDBusConnection mSessionBus;

    static int mServiceCounter;
};

#endif
