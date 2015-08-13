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

#ifndef FCITXQTCONNECTION_P_H
#define FCITXQTCONNECTION_P_H

#include "fcitxqtconnection.h"
#include <QtCore/QWeakPointer>
#include <QtCore/QFileSystemWatcher>

class QDBusConnection;
class QDBusServiceWatcher;

class FcitxQtConnectionPrivate : public QObject {
    Q_OBJECT
public:
    FcitxQtConnectionPrivate(FcitxQtConnection* conn);
    virtual ~FcitxQtConnectionPrivate();
    FcitxQtConnection * const q_ptr;
    Q_DECLARE_PUBLIC(FcitxQtConnection);

private Q_SLOTS:
    void imChanged(const QString& service, const QString& oldowner, const QString& newowner);
    void dbusDisconnected();
    void cleanUp();
    void newServiceAppear();
    void socketFileChanged();

private:
    bool isConnected();

    static QByteArray localMachineId();
    const QString& socketFile();
    void createConnection();
    QString address();
    int displayNumber();
    void initialize();
    void finalize();

    int m_displayNumber;
    QString m_serviceName;
    QDBusConnection* m_connection;
    QDBusServiceWatcher* m_serviceWatcher;
    QFileSystemWatcher* m_watcher;
    QString m_socketFile;
    bool m_autoReconnect;
    bool m_connectedOnce;
    bool m_initialized;
};


#endif // FCITXCONNECTION_P_H
