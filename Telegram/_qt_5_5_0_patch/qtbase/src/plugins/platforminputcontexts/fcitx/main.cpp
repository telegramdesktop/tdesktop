/***************************************************************************
 *   Copyright (C) 2012~2013 by CSSlayer                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#include "main.h"


QStringList QFcitxPlatformInputContextPlugin::keys() const
{
    return QStringList(QStringLiteral("fcitx"));

}

QFcitxPlatformInputContext *QFcitxPlatformInputContextPlugin::create(const QString& system, const QStringList& paramList)
{
    Q_UNUSED(paramList);
    if (system.compare(system, QStringLiteral("fcitx"), Qt::CaseInsensitive) == 0)
        return new QFcitxPlatformInputContext;
    return 0;
}
