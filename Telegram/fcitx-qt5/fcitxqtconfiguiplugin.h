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

#ifndef FCITXQTCONFIGPLUGIN_H
#define FCITXQTCONFIGPLUGIN_H

#include "fcitxqtwidgetsaddons_export.h"
#include <QtCore/QString>
#include <QtCore/QObject>
#include <QtCore/QStringList>

class FcitxQtConfigUIWidget;

/**
 * interface for qt config ui
 */
struct FCITXQTWIDGETSADDONS_EXPORT FcitxQtConfigUIFactoryInterface
{
    /**
     *  return the name for plugin
     */
    virtual QString name() = 0;

    /**
     * create new widget based on key
     *
     * @see FcitxQtConfigUIPlugin::files
     *
     * @return plugin name
     */
    virtual FcitxQtConfigUIWidget *create( const QString &key ) = 0;

    /**
     * return a list that this plugin will handle, need to be consist with
     * the file path in config file
     *
     * @return support file list
     */
    virtual QStringList files() = 0;

    /**
     * return gettext domain, due to some reason, fcitx doesn't use qt's i18n feature
     * but gettext
     *
     * @return domain of gettext
     */
    virtual QString domain() = 0;

};

#define FcitxQtConfigUIFactoryInterface_iid "org.fcitx.Fcitx.FcitxQtConfigUIFactoryInterface"
Q_DECLARE_INTERFACE(FcitxQtConfigUIFactoryInterface, FcitxQtConfigUIFactoryInterface_iid)

/**
 * base class for qt config ui
 */
class FCITXQTWIDGETSADDONS_EXPORT FcitxQtConfigUIPlugin : public QObject, public FcitxQtConfigUIFactoryInterface {
    Q_OBJECT
    Q_INTERFACES(FcitxQtConfigUIFactoryInterface)
public:
    explicit FcitxQtConfigUIPlugin(QObject* parent = 0);
    virtual ~FcitxQtConfigUIPlugin();
};


#endif // FCITXCONFIGPLUGIN_H
