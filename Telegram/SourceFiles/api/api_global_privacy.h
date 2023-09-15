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

private:
	void apply(const MTPGlobalPrivacySettings &data);

	void update(
		bool archiveAndMute,
		UnarchiveOnNewMessage unarchiveOnNewMessage);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	mtpRequestId _requestId = 0;
	rpl::variable<bool> _archiveAndMute = false;
	rpl::variable<UnarchiveOnNewMessage> _unarchiveOnNewMessage
		= UnarchiveOnNewMessage::None;
	rpl::variable<bool> _showArchiveAndMute = false;
	std::vector<Fn<void()>> _callbacks;

};

} // namespace Api
