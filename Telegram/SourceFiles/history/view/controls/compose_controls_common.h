/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"

namespace Api {
enum class SendProgressType;
struct SendAction;
} // namespace Api

namespace Data {
class GroupCall;
} // namespace Data

class History;

namespace HistoryView::Controls {

struct MessageToEdit {
	FullMsgId fullId;
	Api::SendOptions options;
	TextWithTags textWithTags;
	bool spoilered = false;
};
struct VoiceToSend {
	QByteArray bytes;
	VoiceWaveform waveform;
	crl::time duration = 0;
	Api::SendOptions options;
	bool video = false;
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
	Frozen,
	Hidden,
};

struct WriteRestriction {
	using Type = WriteRestrictionType;

	QString text;
	QString button;
	Type type = Type::None;
	int boostsToLift = false;

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
	std::shared_ptr<Data::GroupCall> videoStream;
	MsgId topicRootId = 0;
	PeerId monoforumPeerId = 0;
	Fn<bool()> showSlowmodeError;
	Fn<Api::SendAction()> sendActionFactory;
	rpl::producer<int> slowmodeSecondsLeft;
	rpl::producer<bool> sendDisabledBySlowmode;
	rpl::producer<bool> liked;
	rpl::producer<int> minStarsCount;
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

enum class ToggleCommentsState {
	Empty,
	Shown,
	Hidden,
	WithNew,
};

struct SendStarButtonEffect {
	not_null<PeerData*> from;
	int stars = 0;
};

} // namespace HistoryView::Controls
