/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;

namespace Api {

struct SendOptions {
	TimeId scheduled = 0;
	bool silent = false;
	bool handleSupportSwitch = false;
	bool removeWebPageId = false;
};

enum class SendType {
	Normal,
	Scheduled,
	ScheduledToUser, // For "Send when online".
};

struct SendAction {
	explicit SendAction(not_null<History*> history) : history(history) {
	}

	not_null<History*> history;
	SendOptions options;
	MsgId replyTo = 0;
	bool clearDraft = true;
	bool generateLocal = true;
	MsgId replaceMediaOf = 0;
};

struct MessageToSend {
	explicit MessageToSend(not_null<History*> history) : action(history) {
	}

	SendAction action;
	TextWithTags textWithTags;
	WebPageId webPageId = 0;
};

} // namespace Api
