/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;
class ApiWrap;
class PeerData;
class ChannelData;

namespace Api {

class UnreadThings final {
public:
	explicit UnreadThings(not_null<ApiWrap*> api);

	[[nodiscard]] bool trackMentions(PeerData *peer) const;
	[[nodiscard]] bool trackReactions(PeerData *peer) const;

	void preloadEnough(History *history);

	void mediaAndMentionsRead(
		const base::flat_set<MsgId> &readIds,
		ChannelData *channel = nullptr);

private:
	void preloadEnoughMentions(not_null<History*> history);
	void preloadEnoughReactions(not_null<History*> history);

	void requestMentions(not_null<History*> history, int loaded);
	void requestReactions(not_null<History*> history, int loaded);

	const not_null<ApiWrap*> _api;

	base::flat_map<not_null<History*>, mtpRequestId> _mentionsRequests;
	base::flat_map<not_null<History*>, mtpRequestId> _reactionsRequests;

};

} // namespace Api
