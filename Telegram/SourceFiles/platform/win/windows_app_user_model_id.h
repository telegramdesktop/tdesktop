/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <windows.h>

namespace Platform {
namespace AppUserModelId {

void cleanupShortcut();
void checkPinned();

const WCHAR *getId();
bool validateShortcut();

const PROPERTYKEY &getKey();

} // namespace AppUserModelId
} // namespace Platform
