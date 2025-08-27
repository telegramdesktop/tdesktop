/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;

namespace Data {
struct FileOrigin;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Stickers {

class GiftBoxPack final {
public:
	explicit GiftBoxPack(not_null<Main::Session*> session);
	~GiftBoxPack();

	void load();
	[[nodiscard]] int monthsForStars(int stars) const;
	[[nodiscard]] DocumentData *lookup(int months) const;
	[[nodiscard]] Data::FileOrigin origin() const;
	[[nodiscard]] rpl::producer<> updated() const;

	void tonLoad();
	[[nodiscard]] DocumentData *tonLookup(int amount) const;
	[[nodiscard]] Data::FileOrigin tonOrigin() const;
	[[nodiscard]] rpl::producer<> tonUpdated() const;

private:
	using SetId = uint64;

	struct Pack {
		SetId id = 0;
		uint64 accessHash = 0;
		std::vector<DocumentData*> documents;
		mtpRequestId requestId = 0;
		std::vector<int> dividers;
		rpl::event_stream<> updated;
	};

	void load(Pack &pack, const MTPInputStickerSet &set);
	void applySet(Pack &pack, const MTPDmessages_stickerSet &data);
	[[nodiscard]] DocumentData *lookup(
		const Pack &pack,
		int divider,
		bool exact) const;

	const not_null<Main::Session*> _session;
	const std::vector<int> _localMonths;
	const std::vector<int> _localTonAmounts;

	Pack _premium;
	Pack _ton;

};

} // namespace Stickers
