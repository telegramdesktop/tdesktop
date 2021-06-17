/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_emoji_pack.h"

#include "chat_helpers/stickers_emoji_image_loader.h"
#include "history/history_item.h"
#include "lottie/lottie_common.h"
#include "ui/emoji_config.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/image/image.h"
#include "main/main_session.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "base/call_delayed.h"
#include "apiwrap.h"
#include "app.h"
#include "styles/style_chat.h"

#include <QtCore/QBuffer>

namespace Stickers {
namespace {

constexpr auto kRefreshTimeout = 7200 * crl::time(1000);

[[nodiscard]] QSize SingleSize() {
	const auto single = st::largeEmojiSize;
	const auto outline = st::largeEmojiOutline;
	return QSize(
		2 * outline + single,
		2 * outline + single
	) * cIntRetinaFactor();
}

[[nodiscard]] const Lottie::ColorReplacements *ColorReplacements(int index) {
	Expects(index >= 1 && index <= 5);

	static const auto color1 = Lottie::ColorReplacements{
		{
			{ 0xf77e41U, 0xcb7b55U },
			{ 0xffb139U, 0xf6b689U },
			{ 0xffd140U, 0xffcda7U },
			{ 0xffdf79U, 0xffdfc5U },
		},
		1,
	};
	static const auto color2 = Lottie::ColorReplacements{
		{
			{ 0xf77e41U, 0xa45a38U },
			{ 0xffb139U, 0xdf986bU },
			{ 0xffd140U, 0xedb183U },
			{ 0xffdf79U, 0xf4c3a0U },
		},
		2,
	};
	static const auto color3 = Lottie::ColorReplacements{
		{
			{ 0xf77e41U, 0x703a17U },
			{ 0xffb139U, 0xab673dU },
			{ 0xffd140U, 0xc37f4eU },
			{ 0xffdf79U, 0xd89667U },
		},
		3,
	};
	static const auto color4 = Lottie::ColorReplacements{
		{
			{ 0xf77e41U, 0x4a2409U },
			{ 0xffb139U, 0x7d3e0eU },
			{ 0xffd140U, 0x965529U },
			{ 0xffdf79U, 0xa96337U },
		},
		4,
	};
	static const auto color5 = Lottie::ColorReplacements{
		{
			{ 0xf77e41U, 0x200f0aU },
			{ 0xffb139U, 0x412924U },
			{ 0xffd140U, 0x593d37U },
			{ 0xffdf79U, 0x63453fU },
		},
		5,
	};
	static const auto list = std::array{
		&color1,
		&color2,
		&color3,
		&color4,
		&color5,
	};
	return list[index - 1];
}

} // namespace

QSize LargeEmojiImage::Size() {
	return SingleSize();
}

EmojiPack::EmojiPack(not_null<Main::Session*> session)
: _session(session) {
	refresh();

	session->data().itemRemoved(
	) | rpl::filter([](not_null<const HistoryItem*> item) {
		return item->isIsolatedEmoji();
	}) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		remove(item);
	}, _lifetime);

	Core::App().settings().largeEmojiChanges(
	) | rpl::start_with_next([=](bool large) {
		refreshAll();
	}, _lifetime);

	Ui::Emoji::Updated(
	) | rpl::start_with_next([=] {
		_images.clear();
		refreshAll();
	}, _lifetime);
}

EmojiPack::~EmojiPack() = default;

bool EmojiPack::add(not_null<HistoryItem*> item) {
	if (const auto emoji = item->isolatedEmoji()) {
		_items[emoji].emplace(item);
		return true;
	}
	return false;
}

void EmojiPack::remove(not_null<const HistoryItem*> item) {
	Expects(item->isIsolatedEmoji());

	const auto emoji = item->isolatedEmoji();
	const auto i = _items.find(emoji);
	Assert(i != end(_items));
	const auto j = i->second.find(item);
	Assert(j != end(i->second));
	i->second.erase(j);
	if (i->second.empty()) {
		_items.erase(i);
	}
}

auto EmojiPack::stickerForEmoji(const IsolatedEmoji &emoji) -> Sticker {
	Expects(!emoji.empty());

	if (emoji.items[1] != nullptr) {
		return Sticker();
	}
	const auto first = emoji.items[0];
	const auto i = _map.find(first);
	if (i != end(_map)) {
		return { i->second.get(), nullptr };
	}
	if (!first->colored()) {
		return Sticker();
	}
	const auto j = _map.find(first->original());
	if (j != end(_map)) {
		const auto index = first->variantIndex(first);
		return { j->second.get(), ColorReplacements(index) };
	}
	return Sticker();
}

std::shared_ptr<LargeEmojiImage> EmojiPack::image(EmojiPtr emoji) {
	const auto i = _images.emplace(
		emoji,
		std::weak_ptr<LargeEmojiImage>()).first;
	if (const auto result = i->second.lock()) {
		return result;
	}
	auto result = std::make_shared<LargeEmojiImage>();
	const auto raw = result.get();
	const auto weak = base::make_weak(_session.get());
	raw->load = [=] {
		Core::App().emojiImageLoader().with([=](
				const EmojiImageLoader &loader) {
			crl::on_main(weak, [
				=,
				image = loader.prepare(emoji)
			]() mutable {
				const auto i = _images.find(emoji);
				if (i != end(_images)) {
					if (const auto strong = i->second.lock()) {
						if (!strong->image) {
							strong->load = nullptr;
							strong->image.emplace(std::move(image));
							_session->notifyDownloaderTaskFinished();
						}
					}
				}
			});
		});
		raw->load = nullptr;
	};
	i->second = result;
	return result;
}

void EmojiPack::refresh() {
	if (_requestId) {
		return;
	}
	_requestId = _session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetAnimatedEmoji()
	)).done([=](const MTPmessages_StickerSet &result) {
		_requestId = 0;
		refreshDelayed();
		result.match([&](const MTPDmessages_stickerSet &data) {
			applySet(data);
		});
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		refreshDelayed();
	}).send();
}

void EmojiPack::applySet(const MTPDmessages_stickerSet &data) {
	const auto stickers = collectStickers(data.vdocuments().v);
	auto was = base::take(_map);

	for (const auto &pack : data.vpacks().v) {
		pack.match([&](const MTPDstickerPack &data) {
			applyPack(data, stickers);
		});
	}

	for (const auto &[emoji, document] : _map) {
		const auto i = was.find(emoji);
		if (i == end(was)) {
			refreshItems(emoji);
		} else {
			if (i->second != document) {
				refreshItems(i->first);
			}
			was.erase(i);
		}
	}
	for (const auto &[emoji, document] : was) {
		refreshItems(emoji);
	}
}

void EmojiPack::refreshAll() {
	for (const auto &[emoji, list] : _items) {
		refreshItems(list);
	}
}

void EmojiPack::refreshItems(EmojiPtr emoji) {
	const auto i = _items.find(IsolatedEmoji{ { emoji } });
	if (!emoji->colored()) {
		if (const auto count = emoji->variantsCount()) {
			for (auto i = 0; i != count; ++i) {
				refreshItems(emoji->variant(i + 1));
			}
		}
	}
	if (i == end(_items)) {
		return;
	}
	refreshItems(i->second);
}

void EmojiPack::refreshItems(
		const base::flat_set<not_null<HistoryItem*>> &list) {
	for (const auto &item : list) {
		_session->data().requestItemViewRefresh(item);
	}
}

void EmojiPack::applyPack(
		const MTPDstickerPack &data,
		const base::flat_map<uint64, not_null<DocumentData*>> &map) {
	const auto emoji = [&] {
		return Ui::Emoji::Find(qs(data.vemoticon()));
	}();
	const auto document = [&]() -> DocumentData * {
		for (const auto &id : data.vdocuments().v) {
			const auto i = map.find(id.v);
			if (i != end(map)) {
				return i->second.get();
			}
		}
		return nullptr;
	}();
	if (emoji && document) {
		_map.emplace_or_assign(emoji, document);
	}
}

base::flat_map<uint64, not_null<DocumentData*>> EmojiPack::collectStickers(
		const QVector<MTPDocument> &list) const {
	auto result = base::flat_map<uint64, not_null<DocumentData*>>();
	for (const auto &sticker : list) {
		const auto document = _session->data().processDocument(
			sticker);
		if (document->sticker()) {
			result.emplace(document->id, document);
		}
	}
	return result;
}

void EmojiPack::refreshDelayed() {
	base::call_delayed(kRefreshTimeout, _session, [=] {
		refresh();
	});
}

} // namespace Stickers
