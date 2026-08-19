/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Main {

class Session;

struct SendAsPeer {
	not_null<PeerData*> peer;
	bool premiumRequired = false;

	friend inline auto operator<=>(SendAsPeer, SendAsPeer) = default;
};

enum class SendAsType : uchar {
	Message,
	PaidReaction,
	VideoStream,
};

struct SendAsKey {
	SendAsKey(
		not_null<PeerData*> peer,
		SendAsType type = SendAsType::Message)
	: peer(peer)
	, type(type) {
	}

	friend inline auto operator<=>(SendAsKey, SendAsKey) = default;
	friend inline bool operator==(SendAsKey, SendAsKey) = default;

	not_null<PeerData*> peer;
	SendAsType type = SendAsType::Message;
};

class SendAsPeers final {
public:
	explicit SendAsPeers(not_null<Session*> session);

	bool shouldChoose(SendAsKey key);
	void refresh(SendAsKey key, bool force = false);
	[[nodiscard]] const std::vector<SendAsPeer> &list(SendAsKey key) const;
	[[nodiscard]] rpl::producer<SendAsKey> updated() const;

	void saveChosen(not_null<PeerData*> peer, not_null<PeerData*> chosen);
	void setChosen(not_null<PeerData*> peer, PeerId chosenId);
	[[nodiscard]] PeerId chosen(not_null<PeerData*> peer) const;

	[[nodiscard]] const std::vector<not_null<PeerData*>> &paidReactionList(
		not_null<PeerData*> peer) const;

	// If !list(peer).empty() then the result will be from that list.
	[[nodiscard]] not_null<PeerData*> resolveChosen(
		not_null<PeerData*> peer) const;

	[[nodiscard]] static not_null<PeerData*> ResolveChosen(
		not_null<PeerData*> peer,
		const std::vector<SendAsPeer> &list,
		PeerId chosen);

private:
	void request(SendAsKey key);

	const not_null<Session*> _session;
	const std::vector<SendAsPeer> _onlyMe;
	const std::vector<not_null<PeerData*>> _onlyMePaid;

	base::flat_map<SendAsKey, std::vector<SendAsPeer>> _lists;
	base::flat_map<SendAsKey, crl::time> _lastRequestTime;
	base::flat_map<not_null<PeerData*>, PeerId> _chosen;

	rpl::event_stream<SendAsKey> _updates;

	rpl::lifetime _lifetime;

};

} // namespace Main
