/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "data/data_chat_participant_status.h"

namespace Data {
class Session;
} // namespace Data

namespace InlineBots {
enum class PeerType : uint8;
using PeerTypes = base::flags<PeerType>;
} // namespace InlineBots

enum class ReplyMarkupFlag : uint32 {
	None                  = (1U << 0),
	ForceReply            = (1U << 1),
	HasSwitchInlineButton = (1U << 2),
	Inline                = (1U << 3),
	Resize                = (1U << 4),
	SingleUse             = (1U << 5),
	Selective             = (1U << 6),
	IsNull                = (1U << 7),
	OnlyBuyButton         = (1U << 8),
	Persistent            = (1U << 9),
};
inline constexpr bool is_flag_type(ReplyMarkupFlag) { return true; }
using ReplyMarkupFlags = base::flags<ReplyMarkupFlag>;

struct RequestPeerQuery {
	enum class Type : uchar {
		User,
		Group,
		Broadcast,
	};
	enum class Restriction : uchar {
		Any,
		Yes,
		No,
	};

	int maxQuantity = 0;
	Type type = Type::User;
	Restriction userIsBot = Restriction::Any;
	Restriction userIsPremium = Restriction::Any;
	Restriction groupIsForum = Restriction::Any;
	Restriction hasUsername = Restriction::Any;
	bool amCreator = false;
	bool isBotParticipant = false;
	ChatAdminRights myRights = {};
	ChatAdminRights botRights = {};
};
static_assert(std::is_trivially_copy_assignable_v<RequestPeerQuery>);

struct HistoryMessageMarkupButton {
	enum class Type {
		Default,
		Url,
		Callback,
		CallbackWithPassword,
		RequestPhone,
		RequestLocation,
		RequestPoll,
		RequestPeer,
		SwitchInline,
		SwitchInlineSame,
		Game,
		Buy,
		Auth,
		UserProfile,
		WebView,
		SimpleWebView,
		CopyText,
	};

	HistoryMessageMarkupButton(
		Type type,
		const QString &text,
		const QByteArray &data = QByteArray(),
		const QString &forwardText = QString(),
		int64 buttonId = 0);

	static HistoryMessageMarkupButton *Get(
		not_null<Data::Session*> owner,
		FullMsgId itemId,
		int row,
		int column);

	Type type;
	QString text, forwardText;
	QByteArray data;
	int64 buttonId = 0;
	InlineBots::PeerTypes peerTypes = 0;
	mutable mtpRequestId requestId = 0;

};

struct HistoryMessageMarkupData {
	HistoryMessageMarkupData() = default;
	explicit HistoryMessageMarkupData(const MTPReplyMarkup *data);

	void fillForwardedData(const HistoryMessageMarkupData &original);

	[[nodiscard]] bool isNull() const;
	[[nodiscard]] bool isTrivial() const;

	using Button = HistoryMessageMarkupButton;
	std::vector<std::vector<Button>> rows;
	ReplyMarkupFlags flags = ReplyMarkupFlag::IsNull;
	QString placeholder;

private:
	void fillRows(const QVector<MTPKeyboardButtonRow> &v);

};

struct HistoryMessageRepliesData {
	HistoryMessageRepliesData() = default;
	explicit HistoryMessageRepliesData(const MTPMessageReplies *data);

	std::vector<PeerId> recentRepliers;
	ChannelId channelId = 0;
	MsgId readMaxId = 0;
	MsgId maxId = 0;
	int repliesCount = 0;
	bool isNull = true;
	int pts = 0;
};
