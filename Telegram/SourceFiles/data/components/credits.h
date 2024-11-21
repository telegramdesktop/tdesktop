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

[[nodiscard]] StarsAmount FromTL(const MTPStarsAmount &value);

class Credits final {
public:
	explicit Credits(not_null<Main::Session*> session);
	~Credits();

	void load(bool force = false);
	void apply(StarsAmount balance);
	void apply(PeerId peerId, StarsAmount balance);

	[[nodiscard]] bool loaded() const;
	[[nodiscard]] rpl::producer<bool> loadedValue() const;

	[[nodiscard]] StarsAmount balance() const;
	[[nodiscard]] StarsAmount balance(PeerId peerId) const;
	[[nodiscard]] rpl::producer<StarsAmount> balanceValue() const;
	[[nodiscard]] rpl::producer<float64> rateValue(
		not_null<PeerData*> ownedBotOrChannel);

	void applyCurrency(PeerId peerId, uint64 balance);
	[[nodiscard]] uint64 balanceCurrency(PeerId peerId) const;

	void lock(StarsAmount count);
	void unlock(StarsAmount count);
	void withdrawLocked(StarsAmount count);
	void invalidate();

	void apply(const MTPDupdateStarsBalance &data);

private:
	void updateNonLockedValue();

	const not_null<Main::Session*> _session;

	std::unique_ptr<Api::CreditsStatus> _loader;

	base::flat_map<PeerId, StarsAmount> _cachedPeerBalances;
	base::flat_map<PeerId, uint64> _cachedPeerCurrencyBalances;

	StarsAmount _balance;
	StarsAmount _locked;
	rpl::variable<StarsAmount> _nonLockedBalance;
	rpl::event_stream<> _loadedChanges;
	crl::time _lastLoaded = 0;
	float64 _rate = 0.;

	SingleQueuedInvokation _reload;

};

} // namespace Data
