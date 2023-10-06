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

namespace HistoryView {

class Giveaway final : public Media {
public:
	Giveaway(
		not_null<Element*> parent,
		not_null<Data::Giveaway*> giveaway);
	~Giveaway();

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

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
	};

	void paintChannels(Painter &p, const PaintContext &context) const;
	int layoutChannels(int x, int y, int available);
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void fillFromData(not_null<Data::Giveaway*> giveaway);
	void ensureStickerCreated() const;

	[[nodiscard]] QMargins inBubblePadding() const;

	mutable std::optional<Sticker> _sticker;

	Ui::Text::String _prizesTitle;
	Ui::Text::String _prizes;
	Ui::Text::String _participantsTitle;
	Ui::Text::String _participants;
	std::vector<Channel> _channels;
	Ui::Text::String _winnersTitle;
	Ui::Text::String _winners;

	mutable QColor _channelBg;
	mutable std::array<QImage, 4> _channelCorners;

	int _months = 0;
	int _stickerTop = 0;
	int _prizesTitleTop = 0;
	int _prizesTop = 0;
	int _prizesWidth = 0;
	int _participantsTitleTop = 0;
	int _participantsTop = 0;
	int _participantsWidth = 0;
	int _winnersTitleTop = 0;
	int _winnersTop = 0;
	mutable bool _subscribedToThumbnails = false;

};

} // namespace HistoryView
