/***************************************************************************
 *   Copyright (C) 2011~2012 by CSSlayer                                   *
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

#ifndef FCITX_QT_KEYBOARD_LAYOUT_H
#define FCITX_QT_KEYBOARD_LAYOUT_H

#include "fcitxqtdbusaddons_export.h"

// Qt
#include <QtCore/QString>
#include <QtCore/QMetaType>
#include <QtDBus/QDBusArgument>

class FCITXQTDBUSADDONS_EXPORT FcitxQtKeyboardLayout
{
public:
    const QString& layout() const;
    const QString& variant() const;
    const QString& name() const;
    const QString& langCode() const;
    void setLayout(const QString& layout);
    void setLangCode(const QString& lang);
    void setName(const QString& name);
    void setVariant(const QString& variant);

    static void registerMetaType();
private:
    QString m_layout;
    QString m_variant;
    QString m_name;
    QString m_langCode;
};

typedef QList<FcitxQtKeyboardLayout> FcitxQtKeyboardLayoutList;

QDBusArgument& operator<<(QDBusArgument& argument, const FcitxQtKeyboardLayout& l);
const QDBusArgument& operator>>(const QDBusArgument& argument, FcitxQtKeyboardLayout& l);

Q_DECLARE_METATYPE(FcitxQtKeyboardLayout)
Q_DECLARE_METATYPE(FcitxQtKeyboardLayoutList)

#endif
