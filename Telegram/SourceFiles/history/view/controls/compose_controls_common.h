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
	int duration = 0;
	Api::SendOptions options;
};
struct SendActionUpdate {
	Api::SendProgressType type = Api::SendProgressType();
	int progress = 0;
};

struct SetHistoryArgs {
	required<History*> history;
	Fn<bool()> showSlowmodeError;
	rpl::producer<int> slowmodeSecondsLeft;
	rpl::producer<bool> sendDisabledBySlowmode;
	rpl::producer<std::optional<QString>> writeRestriction;
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
