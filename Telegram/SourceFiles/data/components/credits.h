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

class Credits final {
public:
	explicit Credits(not_null<Main::Session*> session);
	~Credits();

	void load(bool force = false);
	void apply(CreditsAmount balance);
	void apply(PeerId peerId, CreditsAmount balance);

	[[nodiscard]] bool loaded() const;
	[[nodiscard]] rpl::producer<bool> loadedValue() const;

	[[nodiscard]] CreditsAmount balance() const;
	[[nodiscard]] CreditsAmount balance(PeerId peerId) const;
	[[nodiscard]] rpl::producer<CreditsAmount> balanceValue() const;
	[[nodiscard]] rpl::producer<float64> rateValue(
		not_null<PeerData*> ownedBotOrChannel);

	[[nodiscard]] rpl::producer<> refreshedByPeerId(PeerId peerId);

	[[nodiscard]] bool statsEnabled() const;

	void applyCurrency(PeerId peerId, uint64 balance);
	[[nodiscard]] uint64 balanceCurrency(PeerId peerId) const;

	void lock(CreditsAmount count);
	void unlock(CreditsAmount count);
	void withdrawLocked(CreditsAmount count);
	void invalidate();

	void apply(const MTPDupdateStarsBalance &data);

private:
	void updateNonLockedValue();

	const not_null<Main::Session*> _session;

	std::unique_ptr<rpl::lifetime> _loader;

	base::flat_map<PeerId, CreditsAmount> _cachedPeerBalances;
	base::flat_map<PeerId, uint64> _cachedPeerCurrencyBalances;

	CreditsAmount _balance;
	CreditsAmount _locked;
	rpl::variable<CreditsAmount> _nonLockedBalance;
	rpl::event_stream<> _loadedChanges;
	crl::time _lastLoaded = 0;
	float64 _rate = 0.;

	bool _statsEnabled = false;

	rpl::event_stream<PeerId> _refreshedByPeerId;

	SingleQueuedInvokation _reload;

};

} // namespace Data
