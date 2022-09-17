/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media_unwrapped.h"
#include "history/view/media/history_view_sticker_player_abstract.h"
#include "base/weak_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {
struct FileOrigin;
class DocumentMedia;
} // namespace Data

namespace Lottie {
struct ColorReplacements;
} // namespace Lottie

namespace ChatHelpers {
enum class StickerLottieSize : uint8;
} // namespace ChatHelpers

namespace HistoryView {

class Sticker final
	: public UnwrappedMedia::Content
	, public base::has_weak_ptr {
public:
	Sticker(
		not_null<Element*> parent,
		not_null<DocumentData*> data,
		bool skipPremiumEffect,
		Element *replacing = nullptr,
		const Lottie::ColorReplacements *replacements = nullptr);
	~Sticker();

	void initSize();
	QSize countOptimalSize() override;
	void draw(
		Painter &p,
		const PaintContext &context,
		const QRect &r) override;
	ClickHandlerPtr link() override;

	[[nodiscard]] bool ready() const;
	DocumentData *document() override;
	void stickerClearLoopPlayed() override;
	std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

	void refreshLink() override;
	bool hasTextForCopy() const override {
		return isEmojiSticker();
	}

	void setDiceIndex(const QString &emoji, int index);
	void setCustomEmojiPart(int size, ChatHelpers::StickerLottieSize tag);
	void setGiftBoxSticker(bool giftBoxSticker);
	[[nodiscard]] bool atTheEnd() const {
		return 	(_frameIndex >= 0) && (_frameIndex + 1 == _framesCount);
	}
	[[nodiscard]] std::optional<int> frameIndex() const {
		return (_frameIndex >= 0)
			? std::make_optional(_frameIndex)
			: std::nullopt;
	}
	[[nodiscard]] std::optional<int> framesCount() const {
		return (_framesCount > 0)
			? std::make_optional(_framesCount)
			: std::nullopt;
	}
	[[nodiscard]] bool readyToDrawAnimationFrame();

	[[nodiscard]] static QSize Size();
	[[nodiscard]] static QSize Size(not_null<DocumentData*> document);
	[[nodiscard]] static QSize PremiumEffectSize(
		not_null<DocumentData*> document);
	[[nodiscard]] static QSize UsualPremiumEffectSize();
	[[nodiscard]] static QSize EmojiEffectSize();
	[[nodiscard]] static QSize EmojiSize();
	[[nodiscard]] static ClickHandlerPtr ShowSetHandler(
		not_null<DocumentData*> document);

private:
	[[nodiscard]] bool hasPremiumEffect() const;
	[[nodiscard]] bool customEmojiPart() const;
	[[nodiscard]] bool isEmojiSticker() const;
	void paintAnimationFrame(
		Painter &p,
		const PaintContext &context,
		const QRect &r);
	bool paintPixmap(Painter &p, const PaintContext &context, const QRect &r);
	void paintPath(Painter &p, const PaintContext &context, const QRect &r);
	[[nodiscard]] QPixmap paintedPixmap(const PaintContext &context) const;
	[[nodiscard]] bool mirrorHorizontal() const;

	void ensureDataMediaCreated() const;
	void dataMediaCreated() const;

	void setupPlayer();
	void playerCreated();
	void unloadPlayer();
	void emojiStickerClicked();
	void premiumStickerClicked();
	void checkPremiumEffectStart();

	const not_null<Element*> _parent;
	const not_null<DocumentData*> _data;
	const Lottie::ColorReplacements *_replacements = nullptr;
	std::unique_ptr<StickerPlayer> _player;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	ClickHandlerPtr _link;
	QSize _size;
	QImage _lastDiceFrame;
	QString _diceEmoji;
	int _diceIndex = -1;
	mutable int _frameIndex = -1;
	mutable int _framesCount = -1;
	ChatHelpers::StickerLottieSize _cachingTag = {};
	mutable bool _oncePlayed : 1;
	mutable bool _premiumEffectPlayed : 1;
	mutable bool _nextLastDiceFrame : 1;
	bool _skipPremiumEffect : 1;
	bool _giftBoxSticker : 1;

};

} // namespace HistoryView
