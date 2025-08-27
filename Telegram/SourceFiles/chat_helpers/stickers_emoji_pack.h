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
class SinglePlayer;
class FrameProvider;
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

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Stickers {

using IsolatedEmoji = Ui::Text::IsolatedEmoji;

struct LargeEmojiImage {
	std::optional<Image> image;
	FnMut<void()> load;

	[[nodiscard]] static QSize Size();
};

enum class EffectType : uint8 {
	EmojiInteraction,
	PremiumSticker,
	MessageEffect,
};

class EmojiPack final {
public:
	using ViewElement = HistoryView::Element;

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

	bool add(not_null<ViewElement*> view);
	void remove(not_null<const ViewElement*> view);

	[[nodiscard]] Sticker stickerForEmoji(EmojiPtr emoji);
	[[nodiscard]] Sticker stickerForEmoji(const IsolatedEmoji &emoji);
	[[nodiscard]] std::shared_ptr<LargeEmojiImage> image(EmojiPtr emoji);

	[[nodiscard]] EmojiPtr chooseInteractionEmoji(
		not_null<HistoryItem*> item) const;
	[[nodiscard]] EmojiPtr chooseInteractionEmoji(
		const QString &emoticon) const;
	[[nodiscard]] auto animationsForEmoji(EmojiPtr emoji) const
		-> const base::flat_map<int, not_null<DocumentData*>> &;
	[[nodiscard]] bool hasAnimationsFor(not_null<HistoryItem*> item) const;
	[[nodiscard]] bool hasAnimationsFor(const QString &emoticon) const;
	[[nodiscard]] int animationsVersion() const {
		return _animationsVersion;
	}
	[[nodiscard]] rpl::producer<> refreshed() const {
		return _refreshed.events();
	}

	[[nodiscard]] std::unique_ptr<Lottie::SinglePlayer> effectPlayer(
		not_null<DocumentData*> document,
		QByteArray data,
		QString filepath,
		EffectType type);

private:
	class ImageLoader;

	struct ProviderKey {
		not_null<DocumentData*> document;
		Stickers::EffectType type = {};

		friend inline auto operator<=>(
			const ProviderKey &,
			const ProviderKey &) = default;
		friend inline bool operator==(
			const ProviderKey &,
			const ProviderKey &) = default;
	};

	void refresh();
	void refreshDelayed();
	void refreshAnimations();
	void applySet(const MTPDmessages_stickerSet &data);
	void applyPack(
		const MTPDstickerPack &data,
		const base::flat_map<uint64, not_null<DocumentData*>> &map);
	void applyAnimationsSet(const MTPDmessages_stickerSet &data);
	[[nodiscard]] auto collectStickers(const QVector<MTPDocument> &list) const
		-> base::flat_map<uint64, not_null<DocumentData*>>;
	[[nodiscard]] auto collectAnimationsIndices(
		const QVector<MTPStickerPack> &packs) const
		-> base::flat_map<uint64, base::flat_set<int>>;
	void refreshAll();
	void refreshItems(EmojiPtr emoji);
	void refreshItems(const base::flat_set<not_null<ViewElement*>> &list);
	void refreshItems(const base::flat_set<not_null<HistoryItem*>> &items);

	const not_null<Main::Session*> _session;
	base::flat_map<EmojiPtr, not_null<DocumentData*>> _map;
	base::flat_map<
		IsolatedEmoji,
		base::flat_set<not_null<HistoryView::Element*>>> _items;
	base::flat_map<EmojiPtr, std::weak_ptr<LargeEmojiImage>> _images;
	mtpRequestId _requestId = 0;

	base::flat_set<not_null<HistoryView::Element*>> _onlyCustomItems;

	int _animationsVersion = 0;
	base::flat_map<
		EmojiPtr,
		base::flat_map<int, not_null<DocumentData*>>> _animations;
	mtpRequestId _animationsRequestId = 0;

	base::flat_map<
		ProviderKey,
		std::weak_ptr<Lottie::FrameProvider>> _sharedProviders;

	rpl::event_stream<> _refreshed;

	rpl::lifetime _lifetime;

};

} // namespace Stickers
