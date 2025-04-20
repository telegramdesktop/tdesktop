/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "mtproto/sender.h"

class ApiWrap;

namespace Main {
class Session;
} // namespace Main

namespace Api {

enum class UnarchiveOnNewMessage {
	None,
	NotInFoldersUnmuted,
	AnyUnmuted,
};

enum class DisallowedGiftType : uchar {
	Limited   = 0x01,
	Unlimited = 0x02,
	Unique    = 0x04,
	Premium   = 0x08,
	SendHide  = 0x10,
};
inline constexpr bool is_flag_type(DisallowedGiftType) { return true; }

using DisallowedGiftTypes = base::flags<DisallowedGiftType>;

[[nodiscard]] PeerId ParsePaidReactionShownPeer(
	not_null<Main::Session*> session,
	const MTPPaidReactionPrivacy &value);

class GlobalPrivacy final {
public:
	explicit GlobalPrivacy(not_null<ApiWrap*> api);

	void reload(Fn<void()> callback = nullptr);
	void updateArchiveAndMute(bool value);
	void updateUnarchiveOnNewMessage(UnarchiveOnNewMessage value);

	[[nodiscard]] bool archiveAndMuteCurrent() const;
	[[nodiscard]] rpl::producer<bool> archiveAndMute() const;
	[[nodiscard]] auto unarchiveOnNewMessageCurrent() const
		-> UnarchiveOnNewMessage;
	[[nodiscard]] auto unarchiveOnNewMessage() const
		-> rpl::producer<UnarchiveOnNewMessage>;
	[[nodiscard]] rpl::producer<bool> showArchiveAndMute() const;
	[[nodiscard]] rpl::producer<> suggestArchiveAndMute() const;
	void dismissArchiveAndMuteSuggestion();

	void updateHideReadTime(bool hide);
	[[nodiscard]] bool hideReadTimeCurrent() const;
	[[nodiscard]] rpl::producer<bool> hideReadTime() const;

	[[nodiscard]] bool newRequirePremiumCurrent() const;
	[[nodiscard]] rpl::producer<bool> newRequirePremium() const;

	[[nodiscard]] int newChargeStarsCurrent() const;
	[[nodiscard]] rpl::producer<int> newChargeStars() const;

	void updateMessagesPrivacy(bool requirePremium, int chargeStars);

	[[nodiscard]] DisallowedGiftTypes disallowedGiftTypesCurrent() const;
	[[nodiscard]] auto disallowedGiftTypes() const
		-> rpl::producer<DisallowedGiftTypes>;
	void updateDisallowedGiftTypes(DisallowedGiftTypes types);

	void loadPaidReactionShownPeer();
	void updatePaidReactionShownPeer(PeerId shownPeer);
	[[nodiscard]] PeerId paidReactionShownPeerCurrent() const;
	[[nodiscard]] rpl::producer<PeerId> paidReactionShownPeer() const;

private:
	void apply(const MTPGlobalPrivacySettings &settings);

	void update(
		bool archiveAndMute,
		UnarchiveOnNewMessage unarchiveOnNewMessage,
		bool hideReadTime,
		bool newRequirePremium,
		int newChargeStars,
		DisallowedGiftTypes disallowedGiftTypes);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	mtpRequestId _requestId = 0;
	rpl::variable<bool> _archiveAndMute = false;
	rpl::variable<UnarchiveOnNewMessage> _unarchiveOnNewMessage
		= UnarchiveOnNewMessage::None;
	rpl::variable<bool> _showArchiveAndMute = false;
	rpl::variable<bool> _hideReadTime = false;
	rpl::variable<bool> _newRequirePremium = false;
	rpl::variable<int> _newChargeStars = 0;
	rpl::variable<DisallowedGiftTypes> _disallowedGiftTypes;
	rpl::variable<PeerId> _paidReactionShownPeer = false;
	std::vector<Fn<void()>> _callbacks;
	bool _paidReactionShownPeerLoaded = false;

};

} // namespace Api
