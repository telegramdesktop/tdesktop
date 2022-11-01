/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;

namespace Data {
class Thread;
} // namespace Data

namespace Api {

struct SendOptions {
	PeerData *sendAs = nullptr;
	TimeId scheduled = 0;
	bool silent = false;
	bool handleSupportSwitch = false;
	bool removeWebPageId = false;
	bool hideViaBot = false;
};

enum class SendType {
	Normal,
	Scheduled,
	ScheduledToUser, // For "Send when online".
};

struct SendAction {
	explicit SendAction(
		not_null<Data::Thread*> thread,
		SendOptions options = SendOptions());

	not_null<History*> history;
	SendOptions options;
	MsgId replyTo = 0;
	MsgId topicRootId = 0;
	bool clearDraft = true;
	bool generateLocal = true;
	MsgId replaceMediaOf = 0;
};

struct MessageToSend {
	explicit MessageToSend(SendAction action) : action(action) {
	}

	SendAction action;
	TextWithTags textWithTags;
	WebPageId webPageId = 0;
};

struct RemoteFileInfo {
	MTPInputFile file;
	std::optional<MTPInputFile> thumb;
	std::vector<MTPInputDocument> attachedStickers;
};

} // namespace Api
