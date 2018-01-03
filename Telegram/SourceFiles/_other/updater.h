/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <windows.h>
#include <string>

#pragma warning(push)
#pragma warning(disable:4091)
#include <DbgHelp.h>
#include <ShlObj.h>
#pragma warning(pop)

#include <Shellapi.h>
#include <Shlwapi.h>

#include <deque>
#include <string>

using std::deque;
using std::wstring;

extern LPTOP_LEVEL_EXCEPTION_FILTER _oldWndExceptionFilter;
LONG CALLBACK _exceptionFilter(EXCEPTION_POINTERS* pExceptionPointers);
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI RedirectedSetUnhandledExceptionFilter(_In_opt_ LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);

static int updaterVersion = 1000;
static const WCHAR *updaterVersionStr = L"0.1.0";
