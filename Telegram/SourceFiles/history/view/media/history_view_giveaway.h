/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_sticker.h"

namespace Data {
struct Giveaway;
} // namespace Data

namespace Dialogs::Stories {
class Thumbnail;
} // namespace Dialogs::Stories

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace HistoryView {

class Giveaway final : public Media {
public:
	Giveaway(
		not_null<Element*> parent,
		not_null<Data::Giveaway*> giveaway);
	~Giveaway();

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	void clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}

	bool toggleSelectionByHandlerClick(
			const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	bool hideFromName() const override;

	void unloadHeavyPart() override;
	bool hasHeavyPart() const override;

private:
	using Thumbnail = Dialogs::Stories::Thumbnail;
	struct Channel {
		Ui::Text::String name;
		std::shared_ptr<Thumbnail> thumbnail;
		QRect geometry;
		ClickHandlerPtr link;
		mutable std::unique_ptr<Ui::RippleAnimation> ripple;
	};

	void paintBadge(Painter &p, const PaintContext &context) const;
	void paintChannels(Painter &p, const PaintContext &context) const;
	int layoutChannels(int x, int y, int available);
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void fillFromData(not_null<Data::Giveaway*> giveaway);
	void ensureStickerCreated() const;
	void validateBadge(const PaintContext &context) const;

	[[nodiscard]] QMargins inBubblePadding() const;

	mutable std::optional<Sticker> _sticker;

	Ui::Text::String _prizesTitle;
	Ui::Text::String _prizes;
	Ui::Text::String _participantsTitle;
	Ui::Text::String _participants;
	std::vector<Channel> _channels;
	Ui::Text::String _countries;
	Ui::Text::String _winnersTitle;
	Ui::Text::String _winners;

	mutable QColor _channelBg;
	mutable QColor _badgeFg;
	mutable QColor _badgeBorder;
	mutable std::array<QImage, 4> _channelCorners;
	mutable QImage _badge;
	mutable QImage _badgeCache;

	mutable QPoint _lastPoint;
	int _months = 0;
	int _quantity = 0;
	int _stickerTop = 0;
	int _prizesTitleTop = 0;
	int _prizesTop = 0;
	int _prizesWidth = 0;
	int _participantsTitleTop = 0;
	int _participantsTop = 0;
	int _participantsWidth = 0;
	int _countriesTop = 0;
	int _countriesWidth = 0;
	int _winnersTitleTop = 0;
	int _winnersTop = 0;
	mutable uint8 _subscribedToThumbnails : 1 = 0;

};

} // namespace HistoryView
