/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

class History;

namespace style {
struct FlatLabel;
struct Checkbox;
} // namespace style

namespace Api {
struct SendOptions;
struct SendAction;
} // namespace Api

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
class Story;
class Thread;
struct SendError;
struct SendErrorWithThread;
} // namespace Data

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

struct PreparedServiceText {
	TextWithEntities text;
	std::vector<ClickHandlerPtr> links;
};

[[nodiscard]] MessageFlags FlagsFromMTP(
	MsgId id,
	MTPDmessage::Flags flags,
	MessageFlags localFlags);
[[nodiscard]] MessageFlags FlagsFromMTP(
	MsgId id,
	MTPDmessageService::Flags flags,
	MessageFlags localFlags);
[[nodiscard]] MTPMessageReplyHeader NewMessageReplyHeader(
	const Api::SendAction &action);

enum class MediaCheckResult {
	Good,
	Unsupported,
	Empty,
	HasExpiredMediaTimeToLive,
	HasUnsupportedTimeToLive,
	HasStoryMention,
};
[[nodiscard]] MediaCheckResult CheckMessageMedia(
	const MTPMessageMedia &media);
[[nodiscard]] CallId CallIdFromInput(const MTPInputGroupCall &data);

[[nodiscard]] std::vector<not_null<UserData*>> ParseInvitedToCallUsers(
	not_null<HistoryItem*> item,
	const QVector<MTPlong> &users);

inline constexpr auto kMaxUnreadReactions = 5; // Now 3, but just in case.
using OnStackUsers = std::array<UserData*, kMaxUnreadReactions>;
[[nodiscard]] OnStackUsers LookupRecentUnreadReactedUsers(
	not_null<HistoryItem*> item);
void CheckReactionNotificationSchedule(
	not_null<HistoryItem*> item,
	const OnStackUsers &wasUsers);
[[nodiscard]] MessageFlags NewForwardedFlags(
	not_null<PeerData*> peer,
	PeerId from,
	not_null<HistoryItem*> fwd);
[[nodiscard]] MessageFlags FinalizeMessageFlags(
	not_null<History*> history,
	MessageFlags flags);
[[nodiscard]] bool CopyMarkupToForward(not_null<const HistoryItem*> item);
[[nodiscard]] TextWithEntities EnsureNonEmpty(
	const TextWithEntities &text = TextWithEntities());
[[nodiscard]] TextWithEntities UnsupportedMessageText();

void RequestDependentMessageItem(
	not_null<HistoryItem*> item,
	PeerId peerId,
	MsgId msgId);
void RequestDependentMessageStory(
	not_null<HistoryItem*> item,
	PeerId peerId,
	StoryId storyId);
[[nodiscard]] MessageFlags NewMessageFlags(not_null<PeerData*> peer);
[[nodiscard]] TimeId NewMessageDate(TimeId scheduled);
[[nodiscard]] TimeId NewMessageDate(const Api::SendOptions &options);
[[nodiscard]] PeerId NewMessageFromId(const Api::SendAction &action);
[[nodiscard]] QString NewMessagePostAuthor(const Api::SendAction &action);
[[nodiscard]] bool ShouldSendSilent(
	not_null<PeerData*> peer,
	const Api::SendOptions &options);
[[nodiscard]] HistoryItem *LookupReplyTo(
	not_null<History*> history,
	FullMsgId replyToId);
[[nodiscard]] MsgId LookupReplyToTop(
	not_null<History*> history,
	HistoryItem *replyTo);
[[nodiscard]] MsgId LookupReplyToTop(
	not_null<History*> history,
	FullReplyTo replyTo);
[[nodiscard]] bool LookupReplyIsTopicPost(HistoryItem *replyTo);

struct SendingErrorRequest {
	MsgId topicRootId = 0;
	const HistoryItemsList *forward = nullptr;
	const Data::Story *story = nullptr;
	const TextWithTags *text = nullptr;
	int messagesCount = 0;
	bool ignoreSlowmodeCountdown = false;
};
[[nodiscard]] int ComputeSendingMessagesCount(
	not_null<History*> history,
	const SendingErrorRequest &request);
[[nodiscard]] Data::SendError GetErrorForSending(
	not_null<PeerData*> peer,
	SendingErrorRequest request);
[[nodiscard]] Data::SendError GetErrorForSending(
	not_null<Data::Thread*> thread,
	SendingErrorRequest request);

struct SendPaymentDetails {
	int messages = 0;
	int stars = 0;
};
[[nodiscard]] std::optional<SendPaymentDetails> ComputePaymentDetails(
	not_null<PeerData*> peer,
	int messagesCount);

[[nodiscard]] bool SuggestPaymentDataReady(
	not_null<PeerData*> peer,
	SuggestPostOptions suggest);

struct PaidConfirmStyles {
	const style::FlatLabel *label = nullptr;
	const style::Checkbox *checkbox = nullptr;
};
void ShowSendPaidConfirm(
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	SendPaymentDetails details,
	Fn<void()> confirmed,
	PaidConfirmStyles styles = {},
	int suggestStarsPrice = 0);
void ShowSendPaidConfirm(
	std::shared_ptr<Main::SessionShow> show,
	not_null<PeerData*> peer,
	SendPaymentDetails details,
	Fn<void()> confirmed,
	PaidConfirmStyles styles = {},
	int suggestStarsPrice = 0);
void ShowSendPaidConfirm(
	std::shared_ptr<Main::SessionShow> show,
	const std::vector<not_null<PeerData*>> &peers,
	SendPaymentDetails details,
	Fn<void()> confirmed,
	PaidConfirmStyles styles = {},
	int suggestStarsPrice = 0);

class SendPaymentHelper final {
public:
	[[nodiscard]] bool check(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Api::SendOptions options,
		int messagesCount,
		Fn<void(int)> resend,
		PaidConfirmStyles styles = {});
	[[nodiscard]] bool check(
		std::shared_ptr<Main::SessionShow> show,
		not_null<PeerData*> peer,
		Api::SendOptions options,
		int messagesCount,
		Fn<void(int)> resend,
		PaidConfirmStyles styles = {});

	void clear();

private:
	Fn<void()> _resend;
	rpl::lifetime _lifetime;

};

[[nodiscard]] Data::SendErrorWithThread GetErrorForSending(
	const std::vector<not_null<Data::Thread*>> &threads,
	SendingErrorRequest request);
[[nodiscard]] object_ptr<Ui::BoxContent> MakeSendErrorBox(
	const Data::SendErrorWithThread &error,
	bool withTitle);

[[nodiscard]] TextWithEntities DropDisallowedCustomEmoji(
	not_null<PeerData*> to,
	TextWithEntities text);

[[nodiscard]] Main::Session *SessionByUniqueId(uint64 sessionUniqueId);
[[nodiscard]] HistoryItem *MessageByGlobalId(GlobalMsgId globalId);

[[nodiscard]] QDateTime ItemDateTime(not_null<const HistoryItem*> item);
[[nodiscard]] QString ItemDateText(
	not_null<const HistoryItem*> item,
	bool isUntilOnline);
[[nodiscard]] bool IsItemScheduledUntilOnline(
	not_null<const HistoryItem*> item);

[[nodiscard]] ClickHandlerPtr JumpToMessageClickHandler(
	not_null<PeerData*> peer,
	MsgId msgId,
	FullMsgId returnToId = FullMsgId(),
	MessageHighlightId highlight = {});
[[nodiscard]] ClickHandlerPtr JumpToMessageClickHandler(
	not_null<HistoryItem*> item,
	FullMsgId returnToId = FullMsgId(),
	MessageHighlightId highlight = {});
[[nodiscard]] ClickHandlerPtr JumpToStoryClickHandler(
	not_null<Data::Story*> story);
ClickHandlerPtr JumpToStoryClickHandler(
	not_null<PeerData*> peer,
	StoryId storyId);
[[nodiscard]] ClickHandlerPtr HideSponsoredClickHandler();
[[nodiscard]] ClickHandlerPtr ReportSponsoredClickHandler(
	not_null<HistoryItem*> item);
[[nodiscard]] ClickHandlerPtr AboutSponsoredClickHandler();

[[nodiscard]] not_null<HistoryItem*> GenerateJoinedMessage(
	not_null<History*> history,
	TimeId inviteDate,
	not_null<UserData*> inviter,
	bool viaRequest);

[[nodiscard]] std::optional<bool> PeerHasThisCall(
	not_null<PeerData*> peer,
	CallId id);
[[nodiscard]] rpl::producer<bool> PeerHasThisCallValue(
	not_null<PeerData*> peer,
	CallId id);
[[nodiscard]] ClickHandlerPtr GroupCallClickHandler(
	not_null<PeerData*> peer,
	CallId callId);

void ShowTrialTranscribesToast(int left, TimeId until);

[[nodiscard]] int ItemsForwardSendersCount(const HistoryItemsList &list);
[[nodiscard]] int ItemsForwardCaptionsCount(const HistoryItemsList &list);
