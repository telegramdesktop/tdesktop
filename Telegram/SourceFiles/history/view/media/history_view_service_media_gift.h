/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_media_unwrapped.h"
#include "history/view/media/history_view_sticker.h"

namespace Data {
class MediaGiftBox;
} // namespace Data

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace HistoryView {

class MediaGift final : public Media {
public:
	MediaGift(not_null<Element*> parent, not_null<Data::MediaGiftBox*> gift);
	~MediaGift();

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	[[nodiscard]] bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const override;
	[[nodiscard]] bool dragItemByHandler(
		const ClickHandlerPtr &p) const override;

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

	void stickerClearLoopPlayed() override;
	std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

	[[nodiscard]] bool needsBubble() const override;
	[[nodiscard]] bool customInfoLayout() const override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

private:
	void ensureStickerCreated() const;
	[[nodiscard]] QRect buttonRect() const;
	[[nodiscard]] QRect stickerRect() const;

	const not_null<Element*> _parent;
	const not_null<Data::MediaGiftBox*> _gift;
	const QSize &_size;
	const QSize _innerSize;

	struct Button {
		void drawBg(QPainter &p) const;
		void toggleRipple(bool pressed);

		Fn<void()> repaint;

		Ui::Text::String text;
		QSize size;

		ClickHandlerPtr link;
		std::unique_ptr<Ui::RippleAnimation> ripple;

		mutable QPoint lastPoint;
	} _button;

	Ui::Text::String _title;
	Ui::Text::String _subtitle;

	mutable std::optional<Sticker> _sticker;

};

} // namespace HistoryView
