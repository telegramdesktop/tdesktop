/***************************************************************************
 *   Copyright (C) 2012~2012 by CSSlayer                                   *
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

#ifndef FCITX_QT_FORMATTED_PREEDIT_H
#define FCITX_QT_FORMATTED_PREEDIT_H

#include "fcitxqtdbusaddons_export.h"

#include <QtCore/QMetaType>
#include <QtDBus/QDBusArgument>

class FCITXQTDBUSADDONS_EXPORT FcitxQtFormattedPreedit {
public:
    const QString& string() const;
    qint32 format() const;
    void setString(const QString& str);
    void setFormat(qint32 format);

    static void registerMetaType();

    bool operator ==(const FcitxQtFormattedPreedit& preedit) const;
private:
    QString m_string;
    qint32 m_format;
};

typedef QList<FcitxQtFormattedPreedit> FcitxQtFormattedPreeditList;

QDBusArgument& operator<<(QDBusArgument& argument, const FcitxQtFormattedPreedit& im);
const QDBusArgument& operator>>(const QDBusArgument& argument, FcitxQtFormattedPreedit& im);

Q_DECLARE_METATYPE(FcitxQtFormattedPreedit)
Q_DECLARE_METATYPE(FcitxQtFormattedPreeditList)

#endif
