/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

#include <windows.h>
#include <string>
#include <DbgHelp.h>
#include <Shellapi.h>
#include <Shlwapi.h>

#include <deque>
#include <string>

using std::deque;
using std::wstring;

#define _NEED_WIN_GENERATE_DUMP

#ifdef _NEED_WIN_GENERATE_DUMP
extern LPTOP_LEVEL_EXCEPTION_FILTER _oldWndExceptionFilter;
LONG CALLBACK _exceptionFilter(EXCEPTION_POINTERS* pExceptionPointers);
#endif _NEED_WIN_GENERATE_DUMP

static int updaterVersion = 1000;
static const WCHAR *updaterVersionStr = L"0.1.0";
