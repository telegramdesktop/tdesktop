/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_characters_limit.h"

#include "ui/rect.h"
#include "styles/style_chat_helpers.h"

namespace {

constexpr auto kMinus = QChar(0x2212);
constexpr auto kLimit = int(999);

[[nodiscard]] int CountDigits(int n) {
	return n == 0 ? 1 : static_cast<int>(std::log10(std::abs(n))) + 1;
}

} // namespace

namespace HistoryView::Controls {

CharactersLimitLabel::CharactersLimitLabel(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::RpWidget*> widgetToAlign,
	style::align align,
	QMargins margins)
: Ui::FlatLabel(parent, st::historyCharsLimitationLabel)
, _widgetToAlign(widgetToAlign)
, _position((align == style::al_top)
	? Fn<void(int, const QRect &)>([=](int height, const QRect &g) {
		const auto w = textMaxWidth();
		move(
			g.x() + (g.width() - w) / 2 + margins.left(),
			rect::bottom(g) + margins.top());
	})
	: Fn<void(int, const QRect &)>([=](int height, const QRect &g) {
		const auto w = textMaxWidth();
		move(
			g.x() + (g.width() - w) / 2 + margins.left(),
			g.y() - height - margins.bottom());
	})) {
	Expects((align == style::al_top) || align == style::al_bottom);
	rpl::combine(
		Ui::RpWidget::heightValue(),
		widgetToAlign->geometryValue()
	) | rpl::start_with_next(_position, lifetime());
}

void CharactersLimitLabel::setLeft(int value) {
	const auto orderChanged = (CountDigits(value) != CountDigits(_lastValue));
	_lastValue = value;

	if (value > 0) {
		setTextColorOverride(st::historyCharsLimitationLabel.textFg->c);
		Ui::FlatLabel::setText(kMinus
			+ QString::number(std::min(value, kLimit)));
	} else {
		setTextColorOverride(st::windowSubTextFg->c);
		Ui::FlatLabel::setText(QString::number(-value));
	}

	if (orderChanged) {
		_position(height(), _widgetToAlign->geometry());
	}
}

} // namespace HistoryView::Controls
