/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Api {
class CreditsStatus;
} // namespace Api

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Credits final {
public:
	explicit Credits(not_null<Main::Session*> session);
	~Credits();

	void load(bool force = false);
	void apply(uint64 balance);
	void apply(PeerId peerId, uint64 balance);

	[[nodiscard]] bool loaded() const;
	[[nodiscard]] rpl::producer<bool> loadedValue() const;

	[[nodiscard]] uint64 balance() const;
	[[nodiscard]] uint64 balance(PeerId peerId) const;
	[[nodiscard]] rpl::producer<uint64> balanceValue() const;
	[[nodiscard]] rpl::producer<float64> rateValue(
		not_null<PeerData*> ownedBotOrChannel);

	void lock(int count);
	void unlock(int count);
	void withdrawLocked(int count);
	void invalidate();

	void apply(const MTPDupdateStarsBalance &data);

private:
	void updateNonLockedValue();

	const not_null<Main::Session*> _session;

	std::unique_ptr<Api::CreditsStatus> _loader;

	base::flat_map<PeerId, uint64> _cachedPeerBalances;

	uint64 _balance = 0;
	uint64 _locked = 0;
	rpl::variable<uint64> _nonLockedBalance;
	rpl::event_stream<> _loadedChanges;
	crl::time _lastLoaded = 0;
	float64 _rate = 0.;

	SingleQueuedInvokation _reload;

};

} // namespace Data
