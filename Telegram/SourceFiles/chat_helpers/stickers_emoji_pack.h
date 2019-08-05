/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_isolated_emoji.h"

#include <crl/crl_object_on_queue.h>

namespace Main {
class Session;
} // namespace Main

class HistoryItem;
class DocumentData;

namespace Ui {
namespace Text {
class String;
} // namespace Text
} // namespace Ui

namespace Stickers {
namespace details {
class EmojiImageLoader;
} // namespace details

using IsolatedEmoji = Ui::Text::IsolatedEmoji;

class EmojiPack final {
public:
	explicit EmojiPack(not_null<Main::Session*> session);
	~EmojiPack();

	bool add(not_null<HistoryItem*> item);
	void remove(not_null<const HistoryItem*> item);

	[[nodiscard]] DocumentData *stickerForEmoji(const IsolatedEmoji &emoji);
	[[nodiscard]] std::shared_ptr<Image> image(EmojiPtr emoji);

private:
	class ImageLoader;

	void refresh();
	void refreshDelayed();
	void applySet(const MTPDmessages_stickerSet &data);
	void applyPack(
		const MTPDstickerPack &data,
		const base::flat_map<uint64, not_null<DocumentData*>> &map);
	base::flat_map<uint64, not_null<DocumentData*>> collectStickers(
		const QVector<MTPDocument> &list) const;
	void refreshAll();
	void refreshItems(EmojiPtr emoji);
	void refreshItems(const base::flat_set<not_null<HistoryItem*>> &list);

	not_null<Main::Session*> _session;
	base::flat_map<EmojiPtr, not_null<DocumentData*>> _map;
	base::flat_map<
		IsolatedEmoji,
		base::flat_set<not_null<HistoryItem*>>> _items;
	base::flat_map<EmojiPtr, std::weak_ptr<Image>> _images;
	mtpRequestId _requestId = 0;

	crl::object_on_queue<details::EmojiImageLoader> _imageLoader;

	rpl::lifetime _lifetime;

};

} // namespace Stickers
