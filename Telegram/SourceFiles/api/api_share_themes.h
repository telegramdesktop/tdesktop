/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_share_theme.h"
#include "mtproto/sender.h"

class ApiWrap;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Api {

class ShareThemes final {
public:
	explicit ShareThemes(not_null<ApiWrap*> api);
	~ShareThemes();

	void apply(not_null<PeerData*> peer, const MTPShareTheme *theme);
	[[nodiscard]] std::optional<Data::ShareTheme> kept(
		not_null<PeerData*> peer) const;
	[[nodiscard]] rpl::producer<not_null<PeerData*>> updates() const;

	[[nodiscard]] static bool CanSave(not_null<PeerData*> peer);
	[[nodiscard]] static rpl::producer<bool> CanSaveValue(
		not_null<PeerData*> peer);
	void save(
		not_null<PeerData*> peer,
		Data::ShareTheme theme,
		Fn<void()> done,
		Fn<void(const MTP::Error &)> fail);

	[[nodiscard]] auto backdropsValue()
		-> rpl::producer<std::vector<Data::UniqueGiftBackdrop>>;

private:
	void requestBackdrops();
	void applyBackdrops(const MTPDshareThemeBackdrops &data);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	base::flat_map<not_null<PeerData*>, Data::ShareTheme> _kept;
	rpl::event_stream<not_null<PeerData*>> _updates;
	base::flat_map<not_null<PeerData*>, mtpRequestId> _saveRequests;
	rpl::variable<std::vector<Data::UniqueGiftBackdrop>> _backdrops;
	crl::time _backdropsRequestedAt = 0;
	mtpRequestId _backdropsRequestId = 0;
	int32 _backdropsHash = 0;

};

} // namespace Api
