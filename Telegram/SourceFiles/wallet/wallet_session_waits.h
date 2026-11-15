/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Gram {
struct NftItem;
} // namespace Gram

namespace Wallet {

struct CollectibleRecord;

class CollectibleWaits final {
public:
	explicit CollectibleWaits(Fn<void()> changed);

	[[nodiscard]] uint64 add(
		Fn<bool(const Gram::NftItem &)> matches,
		Fn<void(bool arrived)> done);
	void cancel(uint64 id);
	[[nodiscard]] bool empty() const;
	void settle(
		const std::vector<CollectibleRecord> *page,
		crl::time sentAt);

private:
	struct Entry {
		uint64 id = 0;
		Fn<bool(const Gram::NftItem &)> matches;
		Fn<void(bool)> done;
		crl::time armedAt = 0;
	};

	const Fn<void()> _changed;
	std::vector<Entry> _entries;
	uint64 _lastId = 0;

};

} // namespace Wallet
