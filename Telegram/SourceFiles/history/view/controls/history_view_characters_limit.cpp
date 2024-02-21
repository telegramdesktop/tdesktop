/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_characters_limit.h"

#include "ui/rect.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView::Controls {

CharactersLimitLabel::CharactersLimitLabel(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::RpWidget*> widgetToAlign,
	style::align align)
: Ui::FlatLabel(parent, st::historyCharsLimitationLabel) {
	Expects((align == style::al_top) || align == style::al_bottom);
	const auto w = st::historyCharsLimitationLabel.minWidth;
	using F = Fn<void(int, const QRect &)>;
	const auto position = (align == style::al_top)
		? F([=](int height, const QRect &g) {
			move(g.x() + (g.width() - w) / 2, rect::bottom(g));
		})
		: F([=](int height, const QRect &g) {
			move(g.x() + (g.width() - w) / 2, g.y() - height);
		});
	rpl::combine(
		Ui::RpWidget::heightValue(),
		widgetToAlign->geometryValue()
	) | rpl::start_with_next(position, lifetime());
}

void CharactersLimitLabel::setLeft(int value) {
	if (value <= 0) {
		return;
	}
	constexpr auto kMinus = QChar(0x2212);
	constexpr auto kLimit = int(999);
	Ui::FlatLabel::setText(kMinus + QString::number(std::min(value, kLimit)));
}

} // namespace HistoryView::Controls
