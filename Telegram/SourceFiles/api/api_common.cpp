/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_common.h"

#include "base/qt/qt_key_modifiers.h"
#include "data/data_thread.h"

namespace Api {

SendAction::SendAction(
	not_null<Data::Thread*> thread,
	SendOptions options)
: history(thread->owningHistory())
, options(options)
, replyTo(thread->topicRootId())
, topicRootId(replyTo) {
}

SendOptions DefaultSendWhenOnlineOptions() {
	return {
		.scheduled = kScheduledUntilOnlineTimestamp,
		.silent = base::IsCtrlPressed(),
	};
}

} // namespace Api
