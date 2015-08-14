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

#ifndef FCITXQTCONNECTION_H
#define FCITXQTCONNECTION_H

#include "fcitxqtdbusaddons_export.h"

#include <QtCore/QObject>

class QDBusConnection;

class FcitxQtConnectionPrivate;


/**
 * dbus connection to fcitx
 **/
class FCITXQTDBUSADDONS_EXPORT FcitxQtConnection : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect)
    Q_PROPERTY(bool connected READ isConnected)
    Q_PROPERTY(QDBusConnection* connection READ connection)
    Q_PROPERTY(QString serviceName READ serviceName)
public:
    /**
     * create a new connection
     *
     * @param parent
     **/
    explicit FcitxQtConnection(QObject* parent = 0);

    /**
     * destroy the connection
     **/
    virtual ~FcitxQtConnection();

    /**
     * the connection will not start to work until you call this function
     * you may want to connect to the signal before you call this function
     **/
    void startConnection();
    void endConnection();
    /**
     * automatically reconnect if fcitx disappeared
     *
     * @param a ...
     * @return void
     **/
    void setAutoReconnect(bool a);

    /**
     * check this connection is doing automatical reconnect or not
     *
     * default value is true
     **/
    bool autoReconnect();

    /**
     * return the current dbus connection to fcitx, notice, the object return
     * by this function might be deteled if fcitx disappear, or might return 0
     * if fcitx is not running
     *
     * @return QDBusConnection*
     **/
    QDBusConnection* connection();
    /**
     * current fcitx dbus service name, can be used for create DBus proxy
     *
     * @return service name
     **/
    const QString& serviceName();
    /**
     * check its connected or not
     **/
    bool isConnected();

Q_SIGNALS:
    /**
     * this signal will be emitted upon fcitx appears
     **/
    void connected();
    /**
     * this signal will be emitted upon fcitx disappears
     *
     * it will come with connected in pair
     **/
    void disconnected();

private:
    FcitxQtConnectionPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(FcitxQtConnection);
};

#endif // FCITXCONNECTION_H
