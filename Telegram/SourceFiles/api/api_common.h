/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_drafts.h"

class History;

namespace Data {
class Thread;
} // namespace Data

namespace Api {

inline constexpr auto kScheduledUntilOnlineTimestamp = TimeId(0x7FFFFFFE);

struct SendOptions {
	uint64 price = 0;
	PeerData *sendAs = nullptr;
	TimeId scheduled = 0;
	BusinessShortcutId shortcutId = 0;
	EffectId effectId = 0;
	bool silent = false;
	bool handleSupportSwitch = false;
	bool invertCaption = false;
	bool hideViaBot = false;
	crl::time ttlSeconds = 0;

	friend inline bool operator==(
		const SendOptions &,
		const SendOptions &) = default;
};
[[nodiscard]] SendOptions DefaultSendWhenOnlineOptions();

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
	FullReplyTo replyTo;
	bool clearDraft = true;
	bool generateLocal = true;
	MsgId replaceMediaOf = 0;

	[[nodiscard]] MTPInputReplyTo mtpReplyTo() const;

	friend inline bool operator==(
		const SendAction &,
		const SendAction &) = default;
};

struct MessageToSend {
	explicit MessageToSend(SendAction action) : action(action) {
	}

	SendAction action;
	TextWithTags textWithTags;
	Data::WebPageDraft webPage;
};

struct RemoteFileInfo {
	MTPInputFile file;
	std::optional<MTPInputFile> thumb;
	std::optional<MTPInputPhoto> videoCover;
	std::vector<MTPInputDocument> attachedStickers;
};

} // namespace Api
