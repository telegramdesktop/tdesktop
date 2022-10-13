/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ApiWrap;
class PeerData;
class ChannelData;

namespace Data {
class Thread;
} // namespace Data

namespace Api {

class UnreadThings final {
public:
	explicit UnreadThings(not_null<ApiWrap*> api);

	[[nodiscard]] bool trackMentions(Data::Thread *thread) const;
	[[nodiscard]] bool trackReactions(Data::Thread *thread) const;

	void preloadEnough(Data::Thread *thread);

	void mediaAndMentionsRead(
		const base::flat_set<MsgId> &readIds,
		ChannelData *channel = nullptr);

	void cancelRequests(not_null<Data::Thread*> thread);

private:
	void preloadEnoughMentions(not_null<Data::Thread*> thread);
	void preloadEnoughReactions(not_null<Data::Thread*> thread);

	void requestMentions(not_null<Data::Thread*> thread, int loaded);
	void requestReactions(not_null<Data::Thread*> thread, int loaded);

	const not_null<ApiWrap*> _api;

	base::flat_map<not_null<Data::Thread*>, mtpRequestId> _mentionsRequests;
	base::flat_map<not_null<Data::Thread*>, mtpRequestId> _reactionsRequests;

};

} // namespace Api
