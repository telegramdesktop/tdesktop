/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_session_waits.h"

#include "wallet/wallet_collectible_record.h"

namespace Wallet {
namespace {

constexpr auto kExpectedCollectibleTimeout = 60 * crl::time(1000);

} // namespace

CollectibleWaits::CollectibleWaits(Fn<void()> changed)
: _changed(std::move(changed)) {
}

uint64 CollectibleWaits::add(
		Fn<bool(const Gram::NftItem &)> matches,
		Fn<void(bool arrived)> done) {
	const auto id = ++_lastId;
	_entries.push_back({
		.id = id,
		.matches = std::move(matches),
		.done = std::move(done),
		.armedAt = crl::now(),
	});
	_changed();
	return id;
}

void CollectibleWaits::cancel(uint64 id) {
	const auto i = ranges::remove(_entries, id, &Entry::id);
	if (i == end(_entries)) {
		return;
	}
	_entries.erase(i, end(_entries));
	_changed();
}

bool CollectibleWaits::empty() const {
	return _entries.empty();
}

void CollectibleWaits::settle(
		const std::vector<CollectibleRecord> *page,
		crl::time sentAt) {
	const auto now = crl::now();
	auto finished = std::vector<std::pair<Fn<void(bool)>, bool>>();
	for (auto i = begin(_entries); i != end(_entries);) {
		// WHY: a page asked before the wait began can still hold the gift
		// from an earlier stay in this wallet, so it never counts.
		const auto arrived = page
			&& (i->armedAt <= sentAt)
			&& ranges::any_of(*page, i->matches);
		const auto expired = (now - i->armedAt
			>= kExpectedCollectibleTimeout);
		if (!arrived && !expired) {
			++i;
			continue;
		}
		LOG(("Wallet Info: expected collectible %1 after %2 ms."
			).arg(arrived ? u"listed"_q : u"not listed"_q
			).arg(now - i->armedAt));
		finished.emplace_back(std::move(i->done), arrived);
		i = _entries.erase(i);
	}
	if (finished.empty()) {
		return;
	}
	_changed();
	for (const auto &[done, arrived] : finished) {
		if (done) {
			done(arrived);
		}
	}
}

} // namespace Wallet
