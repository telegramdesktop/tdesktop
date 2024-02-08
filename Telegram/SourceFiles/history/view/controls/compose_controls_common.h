/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Api {
enum class SendProgressType;
struct SendOptions;
struct SendAction;
} // namespace Api

class History;

namespace HistoryView::Controls {

struct MessageToEdit {
	FullMsgId fullId;
	Api::SendOptions options;
	TextWithTags textWithTags;
};
struct VoiceToSend {
	QByteArray bytes;
	VoiceWaveform waveform;
	crl::time duration = 0;
	Api::SendOptions options;
};
struct SendActionUpdate {
	Api::SendProgressType type = Api::SendProgressType();
	int progress = 0;
	bool cancel = false;
};

enum class WriteRestrictionType {
	None,
	Rights,
	PremiumRequired,
};

struct WriteRestriction {
	using Type = WriteRestrictionType;

	QString text;
	QString button;
	Type type = Type::None;

	[[nodiscard]] bool empty() const {
		return (type == Type::None);
	}
	explicit operator bool() const {
		return !empty();
	}

	friend inline bool operator==(
		const WriteRestriction &a,
		const WriteRestriction &b) = default;
};

struct SetHistoryArgs {
	required<History*> history;
	MsgId topicRootId = 0;
	Fn<bool()> showSlowmodeError;
	Fn<Api::SendAction()> sendActionFactory;
	rpl::producer<int> slowmodeSecondsLeft;
	rpl::producer<bool> sendDisabledBySlowmode;
	rpl::producer<bool> liked;
	rpl::producer<WriteRestriction> writeRestriction;
};

struct ReplyNextRequest {
	enum class Direction {
		Next,
		Previous,
	};
	const FullMsgId replyId;
	const Direction direction;
};

} // namespace HistoryView::Controls
