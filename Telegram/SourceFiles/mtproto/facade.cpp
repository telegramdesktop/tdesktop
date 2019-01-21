/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/facade.h"

#include "storage/localstorage.h"
#include "core/application.h"
#include "core/sandbox.h"

namespace MTP {
namespace internal {
namespace {

int PauseLevel = 0;

} // namespace

bool paused() {
	return PauseLevel > 0;
}

void pause() {
	++PauseLevel;
}

void unpause() {
	--PauseLevel;
	if (!PauseLevel) {
		if (auto instance = MainInstance()) {
			instance->unpaused();
		}
	}
}

} // namespace internal

Instance *MainInstance() {
	return Core::Sandbox::Instance().applicationLaunched()
		? Core::App().mtp()
		: nullptr;
}

} // namespace MTP
