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
	enum class Type {
		Gifts,
		Currency,
	};

	explicit GiftBoxPack(not_null<Main::Session*> session);
	~GiftBoxPack();

	void load(Type type = Type::Gifts);
	[[nodiscard]] int monthsForStars(int stars) const;
	[[nodiscard]] DocumentData *lookup(
		int months,
		Type type = Type::Gifts) const;
	[[nodiscard]] Data::FileOrigin origin(Type type = Type::Gifts) const;
	[[nodiscard]] rpl::producer<> updated() const;

private:
	using SetId = uint64;
	struct SetData {
		SetId setId = 0;
		uint64 accessHash = 0;
		std::vector<DocumentData*> documents;
	};

	void applySet(const MTPDmessages_stickerSet &data, Type type);

	const not_null<Main::Session*> _session;
	const std::vector<int> _localMonths;

	rpl::event_stream<> _updated;
	base::flat_map<Type, SetData> _setsData;
	mtpRequestId _requestId = 0;

};

} // namespace Stickers
