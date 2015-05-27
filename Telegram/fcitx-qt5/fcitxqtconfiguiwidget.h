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

#ifndef FCITXQTCONFIGUIWIDGET_H
#define FCITXQTCONFIGUIWIDGET_H
#include "fcitxqtwidgetsaddons_export.h"
#include <QtWidgets/QWidget>

/**
 * embedded gui for custom configuration
 **/
class FCITXQTWIDGETSADDONS_EXPORT FcitxQtConfigUIWidget : public QWidget {
    Q_OBJECT
public:
    explicit FcitxQtConfigUIWidget(QWidget* parent = 0);

    /**
     * load the configuration, usually, this is being called upon a "reset" button clicked
     * the outer gui will not call it for you for the first time, your initialization might
     * want to call it by yourself.
     *
     * @return void
     **/
    virtual void load() = 0;

    /**
     * save the configuration
     *
     * @see asyncSave saveFinished
     **/
    virtual void save() = 0;

    /**
     * window title
     *
     * @return window title
     **/
    virtual QString title() = 0;

    /**
     * return the addon name it belongs to
     *
     * @return addon name
     **/
    virtual QString addon() = 0;

    /**
     * return the icon name of the window, see QIcon::fromTheme
     *
     * @return icon name
     **/
    virtual QString icon();

    /**
     * return the save function is async or not, default implementation is false
     *
     * @return bool
     **/
    virtual bool asyncSave();

Q_SIGNALS:
    /**
     * the configuration is changed or not, used to indicate parent gui
     *
     * @param changed is config changed
     **/
    void changed(bool changed);

    /**
     * if asyncSave return true, be sure to emit this signal on all case
     *
     * @see asyncSave
     **/
    void saveFinished();
};

#endif // FCITXCONFIGUIWIDGET_H
