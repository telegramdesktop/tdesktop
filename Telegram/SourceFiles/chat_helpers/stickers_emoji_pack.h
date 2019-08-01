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

class HistoryItem;
class DocumentData;

namespace Stickers {

class EmojiPack final {
public:
	explicit EmojiPack(not_null<Main::Session*> session);

	[[nodiscard]] DocumentData *stickerForEmoji(not_null<HistoryItem*> item);

private:
	void refresh();
	void refreshDelayed();

	not_null<Main::Session*> _session;
	base::flat_set<not_null<HistoryItem*>> _notLoaded;
	base::flat_map<EmojiPtr, not_null<DocumentData*>> _map;
	mtpRequestId _requestId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Stickers
