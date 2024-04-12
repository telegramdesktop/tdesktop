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

namespace Data {

class TopPeers final {
public:
	explicit TopPeers(not_null<Main::Session*> session);
	~TopPeers();

	[[nodiscard]] std::vector<not_null<PeerData*>> list() const;
	[[nodiscard]] bool disabled() const;
	[[nodiscard]] rpl::producer<> updates() const;

	void remove(not_null<PeerData*> peer);
	void increment(not_null<PeerData*> peer, TimeId date);
	void reload();
	void toggleDisabled(bool disabled);

	[[nodiscard]] QByteArray serialize() const;
	void applyLocal(QByteArray serialized);

private:
	struct TopPeer {
		not_null<PeerData*> peer;
		float64 rating = 0.;
	};

	void request();
	[[nodiscard]] uint64 countHash() const;
	void updated();

	const not_null<Main::Session*> _session;

	std::vector<TopPeer> _list;
	rpl::event_stream<> _updates;
	crl::time _lastReceived = 0;
	TimeId _lastReceivedDate = 0;

	mtpRequestId _requestId = 0;

	bool _disabled = false;
	bool _received = false;

};

} // namespace Data
