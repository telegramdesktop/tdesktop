/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/title_sub_widget.h"

#include "ui/text/text.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"

#include "styles/style_window.h"

namespace Ui {

not_null<RpWidget*> CreateTitleSubWidget(
		not_null<RpWidget*> parent,
		const style::TextStyle &st,
		rpl::producer<QString> text,
		rpl::producer<style::align> align,
		rpl::producer<int> top) {
	const auto result = CreateChild<RpWidget>(parent.get());
	result->show();

	struct State {
		const style::TextStyle &st;
		Text::String text;
	};
	const auto state = result->lifetime().make_state<State>(State{
		.st = st,
	});

	rpl::combine(
		std::move(text),
		parent->sizeValue(),
		std::move(top),
		std::move(align)
	) | rpl::on_next([=](
			const QString &text,
			QSize size,
			int top,
			style::align align) {
		state->text.setText(state->st, text);

		const auto &left = st::titleSubWidgetLeft;
		const auto &right = st::titleSubWidgetRight;
		const auto width = left.width()
			+ state->text.maxWidth()
			+ right.width();
		const auto height = size.height() - top;
		const auto skip = (height - state->st.font->height) / 2;
		const auto x = (align & Qt::AlignLeft)
			? (2 * skip - left.width())
			: (align & Qt::AlignRight)
			? (size.width() + right.width() - width - 2 * skip)
			: (size.width() - width) / 2;
		result->setGeometry(x, top, width, height);
	}, result->lifetime());

	rpl::merge(
		style::PaletteChanged(),
		result->windowActiveValue() | rpl::to_empty
	) | rpl::on_next([=] {
		result->update();
	}, result->lifetime());

	result->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = Painter(result);
		const auto active = result->window()->isActiveWindow();
		const auto &left = st::titleSubWidgetLeft;
		const auto &right = st::titleSubWidgetRight;
		const auto width = result->width();
		const auto height = result->height();
		const auto leftArea = QRect(0, 0, left.width(), height);
		const auto rightArea = QRect(
			width - right.width(),
			0,
			right.width(),
			height);
		const auto middleArea = QRect(
			left.width(),
			0,
			width - left.width() - right.width(),
			height);
		const auto fill = [&](QRect area, const style::icon &icon) {
			if (!area.intersects(clip)) {
				return;
			} else if (active) {
				icon.fill(p, area);
			} else {
				icon.fill(p, area, st::titleBg->c);
			}
		};
		fill(leftArea, left);
		fill(rightArea, right);
		const auto middle = middleArea.intersected(clip);
		if (!middle.isEmpty()) {
			p.fillRect(middle, active ? st::titleBgActive : st::titleBg);
			p.setPen(active ? st::titleFgActive : st::titleFg);
			const auto top = (height - state->st.font->height) / 2;
			state->text.draw(p, left.width(), top, width);
		}
	}, result->lifetime());

	return result;
}

} // namespace Ui
