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
#include "fcitxqtinputmethoditem.h"

bool FcitxQtInputMethodItem::enabled() const
{
    return m_enabled;
}
const QString& FcitxQtInputMethodItem::langCode() const
{
    return m_langCode;
}
const QString& FcitxQtInputMethodItem::name() const
{
    return m_name;
}
const QString& FcitxQtInputMethodItem::uniqueName() const
{
    return m_uniqueName;
}
void FcitxQtInputMethodItem::setEnabled(bool enable)
{
    m_enabled = enable;
}
void FcitxQtInputMethodItem::setLangCode(const QString& lang)
{
    m_langCode = lang;
}
void FcitxQtInputMethodItem::setName(const QString& name)
{
    m_name = name;
}
void FcitxQtInputMethodItem::setUniqueName(const QString& name)
{
    m_uniqueName = name;
}

void FcitxQtInputMethodItem::registerMetaType()
{
    qRegisterMetaType<FcitxQtInputMethodItem>("FcitxQtInputMethodItem");
    qDBusRegisterMetaType<FcitxQtInputMethodItem>();
    qRegisterMetaType<FcitxQtInputMethodItemList>("FcitxQtInputMethodItemList");
    qDBusRegisterMetaType<FcitxQtInputMethodItemList>();
}

FCITXQTDBUSADDONS_EXPORT
QDBusArgument& operator<<(QDBusArgument& argument, const FcitxQtInputMethodItem& im)
{
    argument.beginStructure();
    argument << im.name();
    argument << im.uniqueName();
    argument << im.langCode();
    argument << im.enabled();
    argument.endStructure();
    return argument;
}

FCITXQTDBUSADDONS_EXPORT
const QDBusArgument& operator>>(const QDBusArgument& argument, FcitxQtInputMethodItem& im)
{
    QString name;
    QString uniqueName;
    QString langCode;
    bool enabled;
    argument.beginStructure();
    argument >> name >> uniqueName >> langCode >> enabled;
    argument.endStructure();
    im.setName(name);
    im.setUniqueName(uniqueName);
    im.setLangCode(langCode);
    im.setEnabled(enabled);
    return argument;
}
