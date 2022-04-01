/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/time_picker_box.h"

#include "base/event_filter.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/ui_utility.h"
#include "ui/widgets/vertical_drum_picker.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace Ui {

Fn<TimeId()> TimePickerBox(
		not_null<GenericBox*> box,
		std::vector<TimeId> values,
		std::vector<QString> phrases,
		TimeId startValue) {
	Expects(phrases.size() == values.size());

	const auto startIndex = [&] {
		const auto it = ranges::find(values, startValue);
		return (it == end(values)) ? 0 : std::distance(begin(values), it);
	}();

	const auto content = box->addRow(object_ptr<Ui::FixedHeightWidget>(
		box,
		st::historyMessagesTTLPickerHeight));

	const auto font = st::boxTextFont;
	const auto maxPhraseWidth = [&] {
		const auto maxPhrase = ranges::max_element(
			phrases,
			std::less<>(),
			[&](const QString &s) { return font->width(s); });
		return font->width(*maxPhrase);
	}();
	const auto itemHeight = st::historyMessagesTTLPickerItemHeight;
	auto paintCallback = [=](
			Painter &p,
			int index,
			float64 y,
			float64 distanceFromCenter,
			int outerWidth) {
		const auto r = QRectF(0, y, outerWidth, itemHeight);
		const auto progress = std::abs(distanceFromCenter);
		p.setOpacity(1. - progress);
		p.setFont(font);
		p.setPen(st::defaultFlatLabel.textFg);
		p.drawText(r, phrases[index], style::al_center);
	};

	const auto picker = Ui::CreateChild<Ui::VerticalDrumPicker>(
		content,
		std::move(paintCallback),
		phrases.size(),
		itemHeight,
		startIndex);

	content->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		picker->resize(maxPhraseWidth, s.height());
		picker->moveToLeft((s.width() - picker->width()) / 2, 0);
	}, content->lifetime());

	content->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		Painter p(content);

		p.fillRect(r, Qt::transparent);

		const auto lineRect = QRect(
			0,
			content->height() / 2,
			content->width(),
			st::defaultInputField.borderActive);
		p.fillRect(lineRect.translated(0, itemHeight / 2), st::activeLineFg);
		p.fillRect(lineRect.translated(0, -itemHeight / 2), st::activeLineFg);
	}, content->lifetime());

	base::install_event_filter(content, [=](not_null<QEvent*> e) {
		if ((e->type() == QEvent::MouseButtonPress)
			|| (e->type() == QEvent::MouseButtonRelease)
			|| (e->type() == QEvent::MouseMove)) {
			picker->handleMouseEvent(static_cast<QMouseEvent*>(e.get()));
		} else if (e->type() == QEvent::Wheel) {
			picker->handleWheelEvent(static_cast<QWheelEvent*>(e.get()));
		}
		return base::EventFilterResult::Continue;
	});
	base::install_event_filter(box, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress) {
			picker->handleKeyEvent(static_cast<QKeyEvent*>(e.get()));
		}
		return base::EventFilterResult::Continue;
	});

	return [=] { return values[picker->index()]; };
}

} // namespace Ui
