/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_characters_limit.h"

#include "styles/style_chat_helpers.h"

namespace HistoryView::Controls {

CharactersLimitLabel::CharactersLimitLabel(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::RpWidget*> widgetBelow)
: Ui::FlatLabel(parent, st::historyCharsLimitationLabel) {
	rpl::combine(
		Ui::RpWidget::heightValue(),
		widgetBelow->positionValue()
	) | rpl::start_with_next([=](int height, const QPoint &p) {
		move(p.x(), p.y() - height);
	}, lifetime());
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
