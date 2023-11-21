/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"

namespace Dialogs::Stories {
class Thumbnail;
} // namespace Dialogs::Stories

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace HistoryView {

class SimilarChannels final : public Media {
public:
	explicit SimilarChannels(not_null<Element*> parent);
	~SimilarChannels();

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	void clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

	bool toggleSelectionByHandlerClick(
			const ClickHandlerPtr &p) const override {
		return false;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return false;
	}

	bool needsBubble() const override {
		return false;
	}
	bool customInfoLayout() const override {
		return true;
	}
	bool isDisplayed() const override {
		return !_channels.empty();
	}

	void unloadHeavyPart() override;
	bool hasHeavyPart() const override;

	bool consumeHorizontalScroll(QPoint position, int delta) override;

private:
	using Thumbnail = Dialogs::Stories::Thumbnail;
	struct Channel {
		QRect geometry;
		Ui::Text::String name;
		std::shared_ptr<Thumbnail> thumbnail;
		ClickHandlerPtr link;
		QString participants;
		QRect participantsRect;
		mutable QImage participantsBg;
		mutable std::unique_ptr<Ui::RippleAnimation> ripple;
		mutable bool subscribed = false;
		mutable bool participantsBgValid = false;
	};

	void ensureCacheReady(QSize size) const;
	void validateParticipansBg(const Channel &channel) const;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	QString _title, _viewAll;
	mutable QImage _roundedCache;
	mutable std::array<QImage, 4> _roundedCorners;
	mutable QPoint _lastPoint;
	int _titleWidth = 0;
	int _viewAllWidth = 0;
	int _fullWidth = 0;
	uint32 _scrollLeft : 15 = 0;
	uint32 _scrollMax : 15 = 0;
	uint32 _hasViewAll : 1 = 0;
	mutable uint32 _hasHeavyPart : 1 = 0;

	std::vector<Channel> _channels;
	mutable ClickHandlerPtr _viewAllLink;

};

} // namespace HistoryView
