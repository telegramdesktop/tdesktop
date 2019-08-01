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

	bool add(not_null<HistoryItem*> item, const QString &text);
	bool remove(not_null<const HistoryItem*> item);

	[[nodiscard]] DocumentData *stickerForEmoji(not_null<HistoryItem*> item);

private:
	void refresh();
	void refreshDelayed();
	void applySet(const MTPDmessages_stickerSet &data);
	void applyPack(
		const MTPDstickerPack &data,
		const base::flat_map<uint64, not_null<DocumentData*>> &map);
	base::flat_map<uint64, not_null<DocumentData*>> collectStickers(
		const QVector<MTPDocument> &list) const;
	void refreshItems(EmojiPtr emoji);

	not_null<Main::Session*> _session;
	base::flat_set<not_null<HistoryItem*>> _notLoaded;
	base::flat_map<EmojiPtr, not_null<DocumentData*>> _map;
	base::flat_map<EmojiPtr, base::flat_set<not_null<HistoryItem*>>> _items;
	mtpRequestId _requestId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Stickers
