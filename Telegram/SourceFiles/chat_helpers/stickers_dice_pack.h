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
	explicit DicePack(not_null<Main::Session*> session);
	~DicePack();

	DocumentData *lookup(int value);

private:
	void load();
	void applySet(const MTPDmessages_stickerSet &data);
	void ensureZeroGenerated();

	not_null<Main::Session*> _session;
	base::flat_map<int, not_null<DocumentData*>> _map;
	DocumentData *_zero = nullptr;
	mtpRequestId _requestId = 0;

};

} // namespace Stickers
