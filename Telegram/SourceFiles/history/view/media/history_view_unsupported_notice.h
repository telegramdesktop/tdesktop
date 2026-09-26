/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "ui/chat/unsupported_notice.h"

namespace HistoryView {

class UnsupportedNotice final : public Media {
public:
	explicit UnsupportedNotice(not_null<Element*> parent);

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

	[[nodiscard]] bool needsBubble() const override;
	[[nodiscard]] bool customInfoLayout() const override;

private:
	[[nodiscard]] QRect buttonRect() const;

	Ui::UnsupportedNoticeCard _card;
	std::unique_ptr<Ui::RippleAnimation> _ripple;
	ClickHandlerPtr _link;

	mutable QPoint _lastPoint;

};

} // namespace HistoryView
