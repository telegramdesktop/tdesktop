/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#define QT_NO_URL_CAST_FROM_STRING
#include "qwindowsservices.h"
#include "qtwindows_additional.h"

#include <QtCore/QUrl>
#include <QtCore/QDebug>
#include <QtCore/QDir>

#include <shlobj.h>
#ifndef Q_OS_WINCE
#  include <intshcut.h>
#endif

QT_BEGIN_NAMESPACE

enum { debug = 0 };

static inline bool shellExecute(const QUrl &url)
{
#ifndef Q_OS_WINCE
    const QString nativeFilePath =
            url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : url.toString(QUrl::FullyEncoded);
    const quintptr result = (quintptr)ShellExecute(0, 0, (wchar_t*)nativeFilePath.utf16(), 0, 0, SW_SHOWNORMAL);
    // ShellExecute returns a value greater than 32 if successful
    if (result <= 32) {
        qWarning("ShellExecute '%s' failed (error %s).", qPrintable(url.toString()), qPrintable(QString::number(result)));
        return false;
    }
    return true;
#else
    Q_UNUSED(url);
    return false;
#endif
}

// Retrieve the commandline for the default mail client. It contains a
// placeholder %1 for the URL. The default key used below is the
// command line for the mailto: shell command.
static inline QString mailCommand()
{
    enum { BufferSize = sizeof(wchar_t) * MAX_PATH };

    const wchar_t mailUserKey[] = L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\mailto\\UserChoice";

    wchar_t command[MAX_PATH] = {0};
    // Check if user has set preference, otherwise use default.
    HKEY handle;
    QString keyName;
    if (!RegOpenKeyEx(HKEY_CURRENT_USER, mailUserKey, 0, KEY_READ, &handle)) {
        DWORD bufferSize = BufferSize;
        if (!RegQueryValueEx(handle, L"Progid", 0, 0, reinterpret_cast<unsigned char*>(command), &bufferSize))
            keyName = QString::fromWCharArray(command);
        RegCloseKey(handle);
    }
    if (keyName.isEmpty())
        keyName = QStringLiteral("mailto");
    keyName += QStringLiteral("\\Shell\\Open\\Command");
    if (debug)
        qDebug() << __FUNCTION__ << "keyName=" << keyName;
    command[0] = 0;
    if (!RegOpenKeyExW(HKEY_CLASSES_ROOT, (const wchar_t*)keyName.utf16(), 0, KEY_READ, &handle)) {
        DWORD bufferSize = BufferSize;
        RegQueryValueEx(handle, L"", 0, 0, reinterpret_cast<unsigned char*>(command), &bufferSize);
        RegCloseKey(handle);
    }
    if (!command[0])
        return QString();
#ifndef Q_OS_WINCE
    wchar_t expandedCommand[MAX_PATH] = {0};
    return ExpandEnvironmentStrings(command, expandedCommand, MAX_PATH) ?
           QString::fromWCharArray(expandedCommand) : QString::fromWCharArray(command);
#else
    return QString();
#endif
}

static inline bool launchMail(const QUrl &url)
{
    QString command = mailCommand();
    if (command.isEmpty()) {
        qWarning("Cannot launch '%s': There is no mail program installed.", qPrintable(url.toString()));
        return false;
    }
    //Make sure the path for the process is in quotes
    const QChar doubleQuote = QLatin1Char('"');
    if (!command.startsWith(doubleQuote)) {
        const int exeIndex = command.indexOf(QStringLiteral(".exe "), 0, Qt::CaseInsensitive);
        if (exeIndex != -1) {
            command.insert(exeIndex + 4, doubleQuote);
            command.prepend(doubleQuote);
        }
    }
    // Pass the url as the parameter. Should use QProcess::startDetached(),
    // but that cannot handle a Windows command line [yet].
	if (command.indexOf(QStringLiteral("%1")) < 0) return false;
	command.replace(QStringLiteral("%1"), url.toString(QUrl::FullyEncoded));
	if (debug)
        qDebug() << __FUNCTION__ << "Launching" << command;
    //start the process
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcess(NULL, (wchar_t*)command.utf16(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        qErrnoWarning("Unable to launch '%s'", qPrintable(command));
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

bool QWindowsServices::openUrl(const QUrl &url)
{
    const QString scheme = url.scheme();
    if (scheme == QLatin1String("mailto") && launchMail(url))
        return true;
    return shellExecute(url);
}

bool QWindowsServices::openDocument(const QUrl &url)
{
    return shellExecute(url);
}

QT_END_NAMESPACE
