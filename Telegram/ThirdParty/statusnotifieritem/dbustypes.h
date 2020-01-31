/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * LXQt - a lightweight, Qt based, desktop toolset
 * https://lxqt.org
 *
 * Copyright: 2015 LXQt team
 * Authors:
 *  Balázs Béla <balazsbela[at]gmail.com>
 *  Paulo Lieuthier <paulolieuthier@gmail.com>
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

#include <QDBusArgument>

#ifndef DBUSTYPES_H
#define DBUSTYPES_H

struct IconPixmap {
    int width;
    int height;
    QByteArray bytes;
};

typedef QList<IconPixmap> IconPixmapList;

Q_DECLARE_METATYPE(IconPixmap)
Q_DECLARE_METATYPE(IconPixmapList)

struct ToolTip {
    QString iconName;
    QList<IconPixmap> iconPixmap;
    QString title;
    QString description;
};

Q_DECLARE_METATYPE(ToolTip)

QDBusArgument &operator<<(QDBusArgument &argument, const IconPixmap &icon);
const QDBusArgument &operator>>(const QDBusArgument &argument, IconPixmap &icon);

QDBusArgument &operator<<(QDBusArgument &argument, const ToolTip &toolTip);
const QDBusArgument &operator>>(const QDBusArgument &argument, ToolTip &toolTip);

#endif // DBUSTYPES_H
