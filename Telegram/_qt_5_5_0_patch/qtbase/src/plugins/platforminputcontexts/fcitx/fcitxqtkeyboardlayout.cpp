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

// Qt
#include <QDBusArgument>
#include <QDBusMetaType>

// self
#include "fcitxqtkeyboardlayout.h"

const QString& FcitxQtKeyboardLayout::layout() const
{
    return m_layout;
}
const QString& FcitxQtKeyboardLayout::langCode() const
{
    return m_langCode;
}
const QString& FcitxQtKeyboardLayout::name() const
{
    return m_name;
}

const QString& FcitxQtKeyboardLayout::variant() const
{
    return m_variant;
}

void FcitxQtKeyboardLayout::setLayout(const QString& layout)
{
    m_layout = layout;
}

void FcitxQtKeyboardLayout::setLangCode(const QString& lang)
{
    m_langCode = lang;
}

void FcitxQtKeyboardLayout::setName(const QString& name)
{
    m_name = name;
}

void FcitxQtKeyboardLayout::setVariant(const QString& variant)
{
    m_variant = variant;
}

void FcitxQtKeyboardLayout::registerMetaType()
{
    qRegisterMetaType<FcitxQtKeyboardLayout>("FcitxQtKeyboardLayout");
    qDBusRegisterMetaType<FcitxQtKeyboardLayout>();
    qRegisterMetaType<FcitxQtKeyboardLayoutList>("FcitxQtKeyboardLayoutList");
    qDBusRegisterMetaType<FcitxQtKeyboardLayoutList>();
}

FCITXQTDBUSADDONS_EXPORT
QDBusArgument& operator<<(QDBusArgument& argument, const FcitxQtKeyboardLayout& layout)
{
    argument.beginStructure();
    argument << layout.layout();
    argument << layout.variant();
    argument << layout.name();
    argument << layout.langCode();
    argument.endStructure();
    return argument;
}

FCITXQTDBUSADDONS_EXPORT
const QDBusArgument& operator>>(const QDBusArgument& argument, FcitxQtKeyboardLayout& layout)
{
    QString l;
    QString variant;
    QString name;
    QString langCode;
    argument.beginStructure();
    argument >> l >> variant >> name >> langCode;
    argument.endStructure();
    layout.setLayout(l);
    layout.setVariant(variant);
    layout.setName(name);
    layout.setLangCode(langCode);
    return argument;
}
