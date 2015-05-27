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

#ifndef FCITX_QT_CONFIG_UI_FACTORY_H
#define FCITX_QT_CONFIG_UI_FACTORY_H

#include <QtCore/QObject>
#include <QtCore/QMap>
#include <QtCore/QStringList>

#include "fcitxqtwidgetsaddons_export.h"
#include "fcitxqtconfiguiwidget.h"
#include "fcitxqtconfiguiplugin.h"

class FcitxQtConfigUIFactoryPrivate;
/**
 * ui plugin factory.
 **/
class FCITXQTWIDGETSADDONS_EXPORT FcitxQtConfigUIFactory : public QObject
{
    Q_OBJECT
public:
    /**
     * create a plugin factory
     *
     * @param parent object parent
     **/
    explicit FcitxQtConfigUIFactory(QObject* parent = 0);
    virtual ~FcitxQtConfigUIFactory();
    /**
     * create widget based on file name, it might return 0 if there is no match
     *
     * @param file file name need to be configured
     * @return FcitxQtConfigUIWidget*
     **/
    FcitxQtConfigUIWidget* create(const QString& file);
    /**
     * a simplified version of create, but it just test if there is a valid entry or not
     *
     * @param file file name
     * @return bool
     **/
    bool test(const QString& file);
private:
    FcitxQtConfigUIFactoryPrivate* d_ptr;
    Q_DECLARE_PRIVATE(FcitxQtConfigUIFactory);
};

#endif // FCITX_CONFIG_UI_FACTORY_H
