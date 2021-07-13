/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Stickers {

class DicePack final {
public:
	DicePack(not_null<Main::Session*> session, const QString &emoji);
	~DicePack();

	[[nodiscard]] DocumentData *lookup(int value);

private:
	void load();
	void applySet(const MTPDmessages_stickerSet &data);
	void tryGenerateLocalZero();
	void generateLocal(int index, const QString &name);

	const not_null<Main::Session*> _session;
	QString _emoji;
	base::flat_map<int, not_null<DocumentData*>> _map;
	mtpRequestId _requestId = 0;

};

class DicePacks final {
public:
	explicit DicePacks(not_null<Main::Session*> session);

	static const QString kDiceString;
	static const QString kDartString;
	static const QString kSlotString;
	static const QString kFballString;
	static const QString kBballString;

	[[nodiscard]] static bool IsSlot(const QString &emoji) {
		return (emoji == kSlotString);
	}

	[[nodiscard]] DocumentData *lookup(const QString &emoji, int value);

private:
	const not_null<Main::Session*> _session;

	base::flat_map<QString, std::unique_ptr<DicePack>> _packs;

};

} // namespace Stickers
