/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media_unwrapped.h"
#include "data/stickers/data_custom_emoji.h"
#include "base/weak_ptr.h"

namespace Ui::Text {
struct OnlyCustomEmoji;
} // namespace Ui::Text

namespace Stickers {
struct LargeEmojiImage;
} // namespace Stickers

namespace ChatHelpers {
enum class StickerLottieSize : uint8;
} // namespace ChatHelpers

namespace HistoryView {

class Sticker;

using LargeCustomEmoji = std::variant<
	DocumentId,
	std::unique_ptr<Sticker>,
	std::unique_ptr<Ui::Text::CustomEmoji>>;

class CustomEmoji final
	: public UnwrappedMedia::Content
	, public base::has_weak_ptr
	, private Data::CustomEmojiManager::Listener {
public:
	CustomEmoji(
		not_null<Element*> parent,
		const Ui::Text::OnlyCustomEmoji &emoji);
	~CustomEmoji();

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;
	void draw(
		Painter &p,
		const PaintContext &context,
		const QRect &r) override;
	ClickHandlerPtr link() override;

	bool alwaysShowOutTimestamp() override;
	bool hasTextForCopy() const override {
		return true;
	}

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

private:
	void paintElement(
		Painter &p,
		int x,
		int y,
		LargeCustomEmoji &element,
		const PaintContext &context);
	void paintSticker(
		Painter &p,
		int x,
		int y,
		not_null<Sticker*> sticker,
		const PaintContext &context);
	void paintCustom(
		Painter &p,
		int x,
		int y,
		not_null<Ui::Text::CustomEmoji*> emoji,
		const PaintContext &context);

	[[nodiscard]] not_null<Data::CustomEmojiManager::Listener*> listener() {
		return this;
	}
	void customEmojiResolveDone(not_null<DocumentData*> document) override;

	[[nodiscard]] std::unique_ptr<Sticker> createStickerPart(
		not_null<DocumentData*> document) const;

	void refreshInteractionLink();
	void interactionLinkClicked();

	const not_null<Element*> _parent;
	std::vector<std::vector<LargeCustomEmoji>> _lines;
	ClickHandlerPtr _interactionLink;
	QImage _selectedFrame;
	int _singleSize = 0;
	int _animationsCheckVersion = -1;
	ChatHelpers::StickerLottieSize _cachingTag = {};
	bool _hasHeavyPart = false;
	bool _resolving = false;

};

} // namespace HistoryView
