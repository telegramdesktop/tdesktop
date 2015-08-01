/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtWidgets module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qsystemtrayicon_p.h"

#include <QtGui/qpa/qplatformsystemtrayicon.h>
#include <qpa/qplatformtheme.h>
#include <private/qguiapplication_p.h>

#include <QApplication>
#include <QStyle>

#ifndef QT_NO_SYSTEMTRAYICON

QT_BEGIN_NAMESPACE

QSystemTrayIconPrivate::QSystemTrayIconPrivate()
    : qpa_sys(QGuiApplicationPrivate::platformTheme()->createPlatformSystemTrayIcon())
    , visible(false)
{
}

QSystemTrayIconPrivate::~QSystemTrayIconPrivate()
{
    delete qpa_sys;
}

void QSystemTrayIconPrivate::install_sys()
{
    if (qpa_sys) {
        qpa_sys->init();
        QObject::connect(qpa_sys, SIGNAL(activated(QPlatformSystemTrayIcon::ActivationReason)),
                         q_func(), SLOT(_q_emitActivated(QPlatformSystemTrayIcon::ActivationReason)));
        QObject::connect(qpa_sys, SIGNAL(messageClicked()),
                         q_func(), SIGNAL(messageClicked()));
        updateMenu_sys();
        updateIcon_sys();
        updateToolTip_sys();
    }
}

void QSystemTrayIconPrivate::remove_sys()
{
    if (qpa_sys)
        qpa_sys->cleanup();
}

QRect QSystemTrayIconPrivate::geometry_sys() const
{
    if (qpa_sys)
        return qpa_sys->geometry();
    else
        return QRect();
}

void QSystemTrayIconPrivate::updateIcon_sys()
{
    if (qpa_sys)
        qpa_sys->updateIcon(icon);
}

void QSystemTrayIconPrivate::updateMenu_sys()
{
	if (qpa_sys) {
		if (menu) {
			if (!menu->platformMenu()) {
				QPlatformMenu *platformMenu = qpa_sys->createMenu();
				if (platformMenu)
					menu->setPlatformMenu(platformMenu);
			}
			qpa_sys->updateMenu(menu->platformMenu());
		} else {
			qpa_sys->updateMenu(0);
		}
	}
}

void QSystemTrayIconPrivate::updateToolTip_sys()
{
    if (qpa_sys)
        qpa_sys->updateToolTip(toolTip);
}

bool QSystemTrayIconPrivate::isSystemTrayAvailable_sys()
{
    QScopedPointer<QPlatformSystemTrayIcon> sys(QGuiApplicationPrivate::platformTheme()->createPlatformSystemTrayIcon());
    if (sys)
        return sys->isSystemTrayAvailable();
    else
        return false;
}

bool QSystemTrayIconPrivate::supportsMessages_sys()
{
    QScopedPointer<QPlatformSystemTrayIcon> sys(QGuiApplicationPrivate::platformTheme()->createPlatformSystemTrayIcon());
    if (sys)
        return sys->supportsMessages();
    else
        return false;
}

void QSystemTrayIconPrivate::showMessage_sys(const QString &message,
                                             const QString &title,
                                             QSystemTrayIcon::MessageIcon icon,
                                             int msecs)
{
    if (!qpa_sys)
        return;

    QIcon notificationIcon;
    switch (icon) {
    case QSystemTrayIcon::Information:
        notificationIcon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation);
        break;
    case QSystemTrayIcon::Warning:
        notificationIcon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
        break;
    case QSystemTrayIcon::Critical:
        notificationIcon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical);
        break;
    default:
        break;
    }
    qpa_sys->showMessage(message, title, notificationIcon,
                     static_cast<QPlatformSystemTrayIcon::MessageIcon>(icon), msecs);
}

QT_END_NAMESPACE

#endif // QT_NO_SYSTEMTRAYICON
