/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {

void start();
void finish();

void SetWatchingMediaKeys(bool watching);
bool TranslucentWindowsSupported(QPoint globalPosition);
void StartTranslucentPaint(QPainter &p, QPaintEvent *e);
void InitOnTopPanel(QWidget *panel);
void DeInitOnTopPanel(QWidget *panel);
void ReInitOnTopPanel(QWidget *panel);
void RegisterCustomScheme();

QString SystemLanguage();
QString SystemCountry();

namespace ThirdParty {

void start();
void finish();

} // namespace ThirdParty
} // namespace Platform

#ifdef Q_OS_MAC
#include "platform/mac/specific_mac.h"
#elif defined Q_OS_LINUX // Q_OS_MAC
#include "platform/linux/specific_linux.h"
#elif defined Q_OS_WIN // Q_OS_MAC || Q_OS_LINUX
#include "platform/win/specific_win.h"
#endif // Q_OS_MAC || Q_OS_LINUX || Q_OS_WIN
