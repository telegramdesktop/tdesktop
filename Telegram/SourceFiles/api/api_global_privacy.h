/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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

	void updateNewRequirePremium(bool value);
	[[nodiscard]] bool newRequirePremiumCurrent() const;
	[[nodiscard]] rpl::producer<bool> newRequirePremium() const;

private:
	void apply(const MTPGlobalPrivacySettings &data);

	void update(
		bool archiveAndMute,
		UnarchiveOnNewMessage unarchiveOnNewMessage,
		bool hideReadTime,
		bool newRequirePremium);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	mtpRequestId _requestId = 0;
	rpl::variable<bool> _archiveAndMute = false;
	rpl::variable<UnarchiveOnNewMessage> _unarchiveOnNewMessage
		= UnarchiveOnNewMessage::None;
	rpl::variable<bool> _showArchiveAndMute = false;
	rpl::variable<bool> _hideReadTime = false;
	rpl::variable<bool> _newRequirePremium = false;
	std::vector<Fn<void()>> _callbacks;

};

} // namespace Api
