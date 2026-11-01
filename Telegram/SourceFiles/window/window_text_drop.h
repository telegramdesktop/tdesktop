/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

class QString;

namespace Data {
class Thread;
} // namespace Data

namespace Window {

class SessionController;

void DropTextToThread(
	not_null<SessionController*> controller,
	not_null<Data::Thread*> thread,
	const QString &text,
	Fn<void(const QString &text)> insertIntoActive);

} // namespace Window
