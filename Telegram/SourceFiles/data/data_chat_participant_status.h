/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
struct PreparedList;
struct PreparedFile;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

enum class ChatAdminRight {
	ChangeInfo = (1 << 0),
	PostMessages = (1 << 1),
	EditMessages = (1 << 2),
	DeleteMessages = (1 << 3),
	BanUsers = (1 << 4),
	InviteByLinkOrAdd = (1 << 5),
	PinMessages = (1 << 7),
	AddAdmins = (1 << 9),
	Anonymous = (1 << 10),
	ManageCall = (1 << 11),
	Other = (1 << 12),
	ManageTopics = (1 << 13),
	PostStories = (1 << 14),
	EditStories = (1 << 15),
	DeleteStories = (1 << 16),
	ManageDirect = (1 << 17),
};
inline constexpr bool is_flag_type(ChatAdminRight) { return true; }
using ChatAdminRights = base::flags<ChatAdminRight>;

enum class ChatRestriction {
	ViewMessages = (1 << 0),

	SendStickers = (1 << 3),
	SendGifs = (1 << 4),
	SendGames = (1 << 5),
	SendInline = (1 << 6),
	SendPolls = (1 << 8),
	SendPhotos = (1 << 19),
	SendVideos = (1 << 20),
	SendVideoMessages = (1 << 21),
	SendMusic = (1 << 22),
	SendVoiceMessages = (1 << 23),
	SendFiles = (1 << 24),
	SendOther = (1 << 25),

	EmbedLinks = (1 << 7),

	ChangeInfo = (1 << 10),
	AddParticipants = (1 << 15),
	PinMessages = (1 << 17),
	CreateTopics = (1 << 18),
};
inline constexpr bool is_flag_type(ChatRestriction) { return true; }
using ChatRestrictions = base::flags<ChatRestriction>;

struct ChatAdminRightsInfo {
	ChatAdminRightsInfo() = default;
	explicit ChatAdminRightsInfo(ChatAdminRights flags) : flags(flags) {
	}
	explicit ChatAdminRightsInfo(const MTPChatAdminRights &rights);

	ChatAdminRights flags;
};

[[nodiscard]] MTPChatAdminRights AdminRightsToMTP(ChatAdminRightsInfo info);

struct ChatRestrictionsInfo {
	ChatRestrictionsInfo() = default;
	ChatRestrictionsInfo(ChatRestrictions flags, TimeId until)
	: flags(flags)
	, until(until) {
	}
	explicit ChatRestrictionsInfo(const MTPChatBannedRights &rights);

	ChatRestrictions flags;
	TimeId until = 0;
};

[[nodiscard]] MTPChatBannedRights RestrictionsToMTP(
	ChatRestrictionsInfo info);

namespace Data {

class Thread;

struct AdminRightsSetOptions {
	bool isGroup : 1 = false;
	bool isForum : 1 = false;
	bool anyoneCanAddMembers : 1 = false;
};

struct RestrictionsSetOptions {
	bool isForum = false;
};

[[nodiscard]] std::vector<ChatRestrictions> ListOfRestrictions(
	RestrictionsSetOptions options);

[[nodiscard]] inline constexpr auto AllSendRestrictionsList() {
	return std::array{
		ChatRestriction::SendOther,
		ChatRestriction::SendStickers,
		ChatRestriction::SendGifs,
		ChatRestriction::SendGames,
		ChatRestriction::SendInline,
		ChatRestriction::SendPolls,
		ChatRestriction::SendPhotos,
		ChatRestriction::SendVideos,
		ChatRestriction::SendVideoMessages,
		ChatRestriction::SendMusic,
		ChatRestriction::SendVoiceMessages,
		ChatRestriction::SendFiles,
	};
}
[[nodiscard]] inline constexpr auto FilesSendRestrictionsList() {
	return std::array{
		ChatRestriction::SendStickers,
		ChatRestriction::SendGifs,
		ChatRestriction::SendPhotos,
		ChatRestriction::SendVideos,
		ChatRestriction::SendMusic,
		ChatRestriction::SendFiles,
	};
}
[[nodiscard]] inline constexpr auto TabbedPanelSendRestrictionsList() {
	return std::array{
		ChatRestriction::SendStickers,
		ChatRestriction::SendGifs,
		ChatRestriction::SendOther,
	};
}
[[nodiscard]] ChatRestrictions AllSendRestrictions();
[[nodiscard]] ChatRestrictions FilesSendRestrictions();
[[nodiscard]] ChatRestrictions TabbedPanelSendRestrictions();

[[nodiscard]] bool CanSendAnyOf(
	not_null<const Thread*> thread,
	ChatRestrictions rights,
	bool forbidInForums = true);
[[nodiscard]] bool CanSendAnyOf(
	not_null<const PeerData*> peer,
	ChatRestrictions rights,
	bool forbidInForums = true);

[[nodiscard]] inline bool CanSend(
		not_null<const Thread*> thread,
		ChatRestriction right,
		bool forbidInForums = true) {
	return CanSendAnyOf(thread, right, forbidInForums);
}
[[nodiscard]] inline bool CanSend(
		not_null<const PeerData*> peer,
		ChatRestriction right,
		bool forbidInForums = true) {
	return CanSendAnyOf(peer, right, forbidInForums);
}
[[nodiscard]] inline bool CanSendTexts(
		not_null<const Thread*> thread,
		bool forbidInForums = true) {
	return CanSend(thread, ChatRestriction::SendOther, forbidInForums);
}
[[nodiscard]] inline bool CanSendTexts(
		not_null<const PeerData*> peer,
		bool forbidInForums = true) {
	return CanSend(peer, ChatRestriction::SendOther, forbidInForums);
}
[[nodiscard]] inline bool CanSendAnything(
		not_null<const Thread*> thread,
		bool forbidInForums = true) {
	return CanSendAnyOf(thread, AllSendRestrictions(), forbidInForums);
}
[[nodiscard]] inline bool CanSendAnything(
		not_null<const PeerData*> peer,
		bool forbidInForums = true) {
	return CanSendAnyOf(peer, AllSendRestrictions(), forbidInForums);
}

struct SendError {
	SendError(QString text = QString()) : text(std::move(text)) {
	}

	struct Args {
		QString text;
		int boostsToLift = 0;
		bool monoforumAdmin = false;
		bool premiumToLift = false;
		bool frozen = false;
	};
	SendError(Args &&args)
	: text(std::move(args.text))
	, boostsToLift(args.boostsToLift)
	, monoforumAdmin(args.monoforumAdmin)
	, premiumToLift(args.premiumToLift)
	, frozen(args.frozen) {
	}

	QString text;
	int boostsToLift = 0;
	bool monoforumAdmin = false;
	bool premiumToLift = false;
	bool frozen = false;

	[[nodiscard]] SendError value_or(SendError other) const {
		return *this ? *this : other;
	}

	explicit operator bool() const {
		return monoforumAdmin || !text.isEmpty();
	}
	[[nodiscard]] bool has_value() const {
		return !text.isEmpty();
	}
	[[nodiscard]] const QString &operator*() const {
		return text;
	}
};

struct SendErrorWithThread {
	SendError error;
	Thread *thread = nullptr;
};

[[nodiscard]] SendError RestrictionError(
	not_null<PeerData*> peer,
	ChatRestriction restriction);
[[nodiscard]] SendError AnyFileRestrictionError(not_null<PeerData*> peer);
[[nodiscard]] SendError FileRestrictionError(
	not_null<PeerData*> peer,
	const Ui::PreparedList &list,
	std::optional<bool> compress);
[[nodiscard]] SendError FileRestrictionError(
	not_null<PeerData*> peer,
	const Ui::PreparedFile &file,
	std::optional<bool> compress);

void ShowSendErrorToast(
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	SendError error);
void ShowSendErrorToast(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<PeerData*> peer,
	SendError error);

} // namespace Data
