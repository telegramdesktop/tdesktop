/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_emoji_pack.h"

#include "chat_helpers/stickers_emoji_image_loader.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "history/history.h"
#include "lottie/lottie_common.h"
#include "ui/emoji_config.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/image/image.h"
#include "ui/rect.h"
#include "main/main_session.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/stickers/data_custom_emoji.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "base/call_delayed.h"
#include "chat_helpers/stickers_lottie.h"
#include "history/view/media/history_view_sticker.h"
#include "lottie/lottie_single_player.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

#include <QtCore/QBuffer>

namespace Stickers {
namespace {

constexpr auto kRefreshTimeout = 7200 * crl::time(1000);
constexpr auto kEmojiCachesCount = 4;
constexpr auto kPremiumCachesCount = 8;

[[nodiscard]] std::optional<int> IndexFromEmoticon(const QString &emoticon) {
	if (emoticon.size() < 2) {
		return std::nullopt;
	}
	const auto first = emoticon[0].unicode();
	return (first >= '1' && first <= '9')
		? std::make_optional(first - '1')
		: (first == 55357 && emoticon[1].unicode() == 56607)
		? std::make_optional(9)
		: std::nullopt;
}

[[nodiscard]] QSize SingleSize() {
	const auto single = st::largeEmojiSize;
	const auto outline = st::largeEmojiOutline;
	return Size(2 * outline + single) * style::DevicePixelRatio();
}

[[nodiscard]] const Lottie::ColorReplacements *ColorReplacements(int index) {
	Expects(index >= 1 && index <= 5);

	static const auto color1 = Lottie::ColorReplacements{
		.modifier = Lottie::SkinModifier::Color1,
		.tag = 1,
	};
	static const auto color2 = Lottie::ColorReplacements{
		.modifier = Lottie::SkinModifier::Color2,
		.tag = 2,
	};
	static const auto color3 = Lottie::ColorReplacements{
		.modifier = Lottie::SkinModifier::Color3,
		.tag = 3,
	};
	static const auto color4 = Lottie::ColorReplacements{
		.modifier = Lottie::SkinModifier::Color4,
		.tag = 4,
	};
	static const auto color5 = Lottie::ColorReplacements{
		.modifier = Lottie::SkinModifier::Color5,
		.tag = 5,
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

	session->data().viewRemoved(
	) | rpl::filter([](not_null<const ViewElement*> view) {
		return view->isIsolatedEmoji() || view->isOnlyCustomEmoji();
	}) | rpl::start_with_next([=](not_null<const ViewElement*> item) {
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

bool EmojiPack::add(not_null<ViewElement*> view) {
	if (const auto custom = view->onlyCustomEmoji()) {
		_onlyCustomItems.emplace(view);
		return true;
	} else if (const auto emoji = view->isolatedEmoji()) {
		_items[emoji].emplace(view);
		return true;
	}
	return false;
}

void EmojiPack::remove(not_null<const ViewElement*> view) {
	Expects(view->isIsolatedEmoji() || view->isOnlyCustomEmoji());

	if (view->isOnlyCustomEmoji()) {
		_onlyCustomItems.remove(view);
	} else if (const auto emoji = view->isolatedEmoji()) {
		const auto i = _items.find(emoji);
		Assert(i != end(_items));
		const auto j = i->second.find(view);
		Assert(j != end(i->second));
		i->second.erase(j);
		if (i->second.empty()) {
			_items.erase(i);
		}
	}
}

auto EmojiPack::stickerForEmoji(EmojiPtr emoji) -> Sticker {
	Expects(emoji != nullptr);

	const auto i = _map.find(emoji);
	if (i != end(_map)) {
		return { i->second.get(), nullptr };
	}
	if (!emoji->colored()) {
		return {};
	}
	const auto j = _map.find(emoji->original());
	if (j != end(_map)) {
		const auto index = emoji->variantIndex(emoji);
		return { j->second.get(), ColorReplacements(index) };
	}
	return {};
}

auto EmojiPack::stickerForEmoji(const IsolatedEmoji &emoji) -> Sticker {
	Expects(!emoji.empty());

	if (!v::is_null(emoji.items[1])) {
		return {};
	} else if (const auto regular = std::get_if<EmojiPtr>(&emoji.items[0])) {
		return stickerForEmoji(*regular);
	}
	return {};
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
	const auto weak = base::make_weak(_session);
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

EmojiPtr EmojiPack::chooseInteractionEmoji(
		not_null<HistoryItem*> item) const {
	return chooseInteractionEmoji(item->originalText().text);
}

EmojiPtr EmojiPack::chooseInteractionEmoji(
		const QString &emoticon) const {
	const auto emoji = Ui::Emoji::Find(emoticon);
	if (!emoji) {
		return nullptr;
	}
	if (!animationsForEmoji(emoji).empty()) {
		return emoji;
	}
	if (const auto original = emoji->original(); original != emoji) {
		if (!animationsForEmoji(original).empty()) {
			return original;
		}
	}
	static const auto kHearts = {
		QString::fromUtf8("\xf0\x9f\x92\x9b"),
		QString::fromUtf8("\xf0\x9f\x92\x99"),
		QString::fromUtf8("\xf0\x9f\x92\x9a"),
		QString::fromUtf8("\xf0\x9f\x92\x9c"),
		QString::fromUtf8("\xf0\x9f\xa7\xa1"),
		QString::fromUtf8("\xf0\x9f\x96\xa4"),
		QString::fromUtf8("\xf0\x9f\xa4\x8e"),
		QString::fromUtf8("\xf0\x9f\xa4\x8d"),
	};
	return ranges::contains(kHearts, emoji->id())
		? Ui::Emoji::Find(QString::fromUtf8("\xe2\x9d\xa4"))
		: emoji;
}

auto EmojiPack::animationsForEmoji(EmojiPtr emoji) const
-> const base::flat_map<int, not_null<DocumentData*>> & {
	static const auto empty = base::flat_map<int, not_null<DocumentData*>>();
	if (!emoji) {
		return empty;
	}
	const auto i = _animations.find(emoji);
	return (i != end(_animations)) ? i->second : empty;
}

bool EmojiPack::hasAnimationsFor(not_null<HistoryItem*> item) const {
	return !animationsForEmoji(chooseInteractionEmoji(item)).empty();
}

bool EmojiPack::hasAnimationsFor(const QString &emoticon) const {
	return !animationsForEmoji(chooseInteractionEmoji(emoticon)).empty();
}

std::unique_ptr<Lottie::SinglePlayer> EmojiPack::effectPlayer(
		not_null<DocumentData*> document,
		QByteArray data,
		QString filepath,
		bool premium) {
	// Shortened copy from stickers_lottie module.
	const auto baseKey = document->bigFileBaseCacheKey();
	const auto tag = uint8(0);
	const auto keyShift = ((tag << 4) & 0xF0)
		| (uint8(ChatHelpers::StickerLottieSize::EmojiInteraction) & 0x0F);
	const auto key = Storage::Cache::Key{
		baseKey.high,
		baseKey.low + keyShift
	};
	const auto get = [=](int i, FnMut<void(QByteArray &&cached)> handler) {
		document->owner().cacheBigFile().get(
			{ key.high, key.low + i },
			std::move(handler));
	};
	const auto weak = base::make_weak(&document->session());
	const auto put = [=](int i, QByteArray &&cached) {
		crl::on_main(weak, [=, data = std::move(cached)]() mutable {
			weak->data().cacheBigFile().put(
				{ key.high, key.low + i },
				std::move(data));
		});
	};
	const auto size = premium
		? HistoryView::Sticker::PremiumEffectSize(document)
		: HistoryView::Sticker::EmojiEffectSize();
	const auto request = Lottie::FrameRequest{
		size * style::DevicePixelRatio(),
	};
	auto &weakProvider = _sharedProviders[document];
	auto shared = [&] {
		if (const auto result = weakProvider.lock()) {
			return result;
		}
		const auto result = Lottie::SinglePlayer::SharedProvider(
			premium ? kPremiumCachesCount : kEmojiCachesCount,
			get,
			put,
			Lottie::ReadContent(data, filepath),
			request,
			Lottie::Quality::High);
		weakProvider = result;
		return result;
	}();
	return std::make_unique<Lottie::SinglePlayer>(std::move(shared), request);
}

void EmojiPack::refresh() {
	if (_requestId) {
		return;
	}
	_requestId = _session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetAnimatedEmoji(),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		_requestId = 0;
		refreshAnimations();
		result.match([&](const MTPDmessages_stickerSet &data) {
			applySet(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		refreshDelayed();
	}).send();
}

void EmojiPack::refreshAnimations() {
	if (_animationsRequestId) {
		return;
	}
	_animationsRequestId = _session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetAnimatedEmojiAnimations(),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		_animationsRequestId = 0;
		refreshDelayed();
		result.match([&](const MTPDmessages_stickerSet &data) {
			applyAnimationsSet(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=] {
		_animationsRequestId = 0;
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
	_refreshed.fire({});
}

void EmojiPack::applyAnimationsSet(const MTPDmessages_stickerSet &data) {
	const auto stickers = collectStickers(data.vdocuments().v);
	const auto &packs = data.vpacks().v;
	const auto indices = collectAnimationsIndices(packs);

	_animations.clear();
	for (const auto &pack : packs) {
		pack.match([&](const MTPDstickerPack &data) {
			const auto emoticon = qs(data.vemoticon());
			if (IndexFromEmoticon(emoticon).has_value()) {
				return;
			}
			const auto emoji = Ui::Emoji::Find(emoticon);
			if (!emoji) {
				return;
			}
			for (const auto &id : data.vdocuments().v) {
				const auto i = indices.find(id.v);
				if (i == end(indices)) {
					continue;
				}
				const auto j = stickers.find(id.v);
				if (j == end(stickers)) {
					continue;
				}
				for (const auto index : i->second) {
					_animations[emoji].emplace(index, j->second);
				}
			}
		});
	}
	++_animationsVersion;
}

auto EmojiPack::collectAnimationsIndices(
	const QVector<MTPStickerPack> &packs
) const -> base::flat_map<uint64, base::flat_set<int>> {
	auto result = base::flat_map<uint64, base::flat_set<int>>();
	for (const auto &pack : packs) {
		pack.match([&](const MTPDstickerPack &data) {
			if (const auto index = IndexFromEmoticon(qs(data.vemoticon()))) {
				for (const auto &id : data.vdocuments().v) {
					result[id.v].emplace(*index);
				}
			}
		});
	}
	return result;
}

void EmojiPack::refreshAll() {
	auto items = base::flat_set<not_null<HistoryItem*>>();
	auto count = 0;
	for (const auto &[emoji, list] : _items) {
		// refreshItems(list); // This call changes _items!
		count += int(list.size());
	}
	items.reserve(count);
	for (const auto &[emoji, list] : _items) {
		// refreshItems(list); // This call changes _items!
		for (const auto &view : list) {
			items.emplace(view->data());
		}
	}
	refreshItems(items);
	refreshItems(_onlyCustomItems);
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
		const base::flat_set<not_null<ViewElement*>> &list) {
	auto items = base::flat_set<not_null<HistoryItem*>>();
	items.reserve(list.size());
	for (const auto &view : list) {
		items.emplace(view->data());
	}
	refreshItems(items);
}

void EmojiPack::refreshItems(
		const base::flat_set<not_null<HistoryItem*>> &items) {
	for (const auto &item : items) {
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
