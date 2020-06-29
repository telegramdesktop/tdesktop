/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_isolated_emoji.h"
#include "ui/image/image.h"
#include "base/timer.h"

#include <crl/crl_object_on_queue.h>

class HistoryItem;
class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Lottie {
struct ColorReplacements;
} // namespace Lottie

namespace Ui {
namespace Text {
class String;
} // namespace Text
namespace Emoji {
class UniversalImages;
} // namespace Emoji
} // namespace Ui

namespace Stickers {

using IsolatedEmoji = Ui::Text::IsolatedEmoji;

struct LargeEmojiImage {
	std::optional<Image> image;
	FnMut<void()> load;

	[[nodiscard]] static QSize Size();
};

class EmojiPack final {
public:
	struct Sticker {
		DocumentData *document = nullptr;
		const Lottie::ColorReplacements *replacements = nullptr;

		[[nodiscard]] bool empty() const {
			return (document == nullptr);
		}
		[[nodiscard]] explicit operator bool() const {
			return !empty();
		}
	};

	explicit EmojiPack(not_null<Main::Session*> session);
	~EmojiPack();

	bool add(not_null<HistoryItem*> item);
	void remove(not_null<const HistoryItem*> item);

	[[nodiscard]] Sticker stickerForEmoji(const IsolatedEmoji &emoji);
	[[nodiscard]] std::shared_ptr<LargeEmojiImage> image(EmojiPtr emoji);

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
	base::flat_map<EmojiPtr, std::weak_ptr<LargeEmojiImage>> _images;
	mtpRequestId _requestId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Stickers
