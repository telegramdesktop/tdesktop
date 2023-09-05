/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#include "mtproto/facade.h"

#include "storage/localstorage.h"
#include "core/application.h"
#include "main/main_account.h"

namespace MTP {
namespace details {
namespace {

int PauseLevel = 0;
rpl::event_stream<> Unpaused;

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
		Unpaused.fire({});
	}
}

rpl::producer<> unpaused() {
	return Unpaused.events();
}

} // namespace details
} // namespace MTP
