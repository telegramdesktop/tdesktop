/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Api {
namespace details {

struct SingleMessageSearchKey {
	QString domainOrId;
	MsgId postId = 0;

	[[nodiscard]] bool empty() const {
		return domainOrId.isEmpty() || !postId;
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}
	[[nodiscard]] bool operator<(const SingleMessageSearchKey &other) const {
		return std::tie(domainOrId, postId)
			< std::tie(other.domainOrId, other.postId);
	}
	[[nodiscard]] bool operator==(
			const SingleMessageSearchKey &other) const {
		return std::tie(domainOrId, postId)
			== std::tie(other.domainOrId, other.postId);
	}
};

} // namespace details

class SingleMessageSearch {
public:
	explicit SingleMessageSearch(not_null<Main::Session*> session);
	~SingleMessageSearch();

	void clear();

	// If 'ready' callback is empty, the result must not be 'nullopt'.
	[[nodiscard]] std::optional<HistoryItem*> lookup(
		const QString &query,
		Fn<void()> ready = nullptr);

private:
	using Key = details::SingleMessageSearchKey;

	[[nodiscard]] std::optional<HistoryItem*> performLookup(
		Fn<void()> ready);
	[[nodiscard]] std::optional<HistoryItem*> performLookupById(
		ChannelId channelId,
		Fn<void()> ready);
	[[nodiscard]] std::optional<HistoryItem*> performLookupByUsername(
		const QString &username,
		Fn<void()> ready);
	[[nodiscard]] std::optional<HistoryItem*> performLookupByChannel(
		not_null<ChannelData*> channel,
		Fn<void()> ready);

	const not_null<Main::Session*> _session;
	std::map<Key, FullMsgId> _cache;
	mtpRequestId _requestId = 0;
	Key _requestKey;

};

[[nodiscard]] QString ConvertPeerSearchQuery(const QString &query);

} // namespace Api
