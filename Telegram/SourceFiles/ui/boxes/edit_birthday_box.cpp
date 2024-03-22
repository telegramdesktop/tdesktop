/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/edit_birthday_box.h"

#include "base/event_filter.h"
#include "data/data_birthday.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/vertical_drum_picker.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QDate>

namespace Ui {

class GenericBox;

void EditBirthdayBox(
		not_null<Ui::GenericBox*> box,
		Data::Birthday current,
		Fn<void(Data::Birthday)> save) {
	box->setWidth(st::boxWideWidth);
	const auto content = box->addRow(object_ptr<Ui::FixedHeightWidget>(
		box,
		st::settingsWorkingHoursPicker));

	const auto font = st::boxTextFont;
	const auto itemHeight = st::settingsWorkingHoursPickerItemHeight;
	const auto picker = [=](
			int count,
			int startIndex,
			Fn<void(QPainter &p, QRectF rect, int index)> paint) {
		auto paintCallback = [=](
				QPainter &p,
				int index,
				float64 y,
				float64 distanceFromCenter,
				int outerWidth) {
			const auto r = QRectF(0, y, outerWidth, itemHeight);
			const auto progress = std::abs(distanceFromCenter);
			const auto revProgress = 1. - progress;
			p.save();
			p.translate(r.center());
			constexpr auto kMinYScale = 0.2;
			const auto yScale = kMinYScale
				+ (1. - kMinYScale) * anim::easeOutCubic(1., revProgress);
			p.scale(1., yScale);
			p.translate(-r.center());
			p.setOpacity(revProgress);
			p.setFont(font);
			p.setPen(st::defaultFlatLabel.textFg);
			paint(p, r, index);
			p.restore();
		};
		return Ui::CreateChild<Ui::VerticalDrumPicker>(
			content,
			std::move(paintCallback),
			count,
			itemHeight,
			startIndex);
	};

	const auto nowDate = QDate::currentDate();
	const auto nowYear = nowDate.year();
	const auto nowMonth = nowDate.month();
	const auto nowDay = nowDate.day();
	const auto now = Data::Birthday(nowDay, nowMonth, nowYear);
	const auto max = current.year() ? std::max(now, current) : now;
	const auto maxYear = max.year();
	const auto minYear = Data::Birthday::kYearMin;
	const auto yearsCount = (maxYear - minYear + 2); // Last - not set.
	const auto yearsStartIndex = current.year()
		? (current.year() - minYear)
		: (yearsCount - 1);
	const auto yearsPaint = [=](QPainter &p, QRectF rect, int index) {
		p.drawText(
			rect,
			(index < yearsCount - 1
				? QString::number(minYear + index)
				: QString::fromUtf8("\xe2\x80\x94")),
			style::al_center);
	};
	const auto years = picker(yearsCount, yearsStartIndex, yearsPaint);

	struct State {
		rpl::variable<Ui::VerticalDrumPicker*> months;
		rpl::variable<Ui::VerticalDrumPicker*> days;
	};
	const auto state = content->lifetime().make_state<State>();

	// years->value() is valid only after size is set.
	rpl::combine(
		content->sizeValue(),
		state->months.value(),
		state->days.value()
	) | rpl::start_with_next([=](
			QSize s,
			Ui::VerticalDrumPicker *months,
			Ui::VerticalDrumPicker *days) {
		const auto half = s.width() / 2;
		years->setGeometry(half * 3 / 2, 0, half / 2, s.height());
		if (months) {
			months->setGeometry(half / 2, 0, half, s.height());
		}
		if (days) {
			days->setGeometry(0, 0, half / 2, s.height());
		}
	}, content->lifetime());

	Ui::SendPendingMoveResizeEvents(years);

	years->value() | rpl::start_with_next([=](int yearsIndex) {
		const auto year = (yearsIndex == yearsCount - 1)
			? 0
			: minYear + yearsIndex;
		const auto monthsCount = (year == maxYear)
			? max.month()
			: 12;
		const auto monthsStartIndex = std::clamp(
			(state->months.current()
				? state->months.current()->index()
				: current.month()
				? (current.month() - 1)
				: (now.month() - 1)),
			0,
			monthsCount - 1);
		const auto monthsPaint = [=](QPainter &p, QRectF rect, int index) {
			p.drawText(
				rect,
				Lang::Month(index + 1)(tr::now),
				style::al_center);
		};
		const auto updated = picker(
			monthsCount,
			monthsStartIndex,
			monthsPaint);
		delete state->months.current();
		state->months = updated;
		state->months.current()->show();
	}, years->lifetime());

	Ui::SendPendingMoveResizeEvents(state->months.current());

	state->months.value() | rpl::map([=](Ui::VerticalDrumPicker *picker) {
		return picker ? picker->value() : rpl::single(current.month()
			? (current.month() - 1)
			: (now.month() - 1));
	}) | rpl::flatten_latest() | rpl::start_with_next([=](int monthIndex) {
		const auto month = monthIndex + 1;
		const auto yearsIndex = years->index();
		const auto year = (yearsIndex == yearsCount - 1)
			? 0
			: minYear + yearsIndex;
		const auto daysCount = (year == maxYear && month == max.month())
			? max.day()
			: (month == 2)
			? ((!year || ((year % 4) && (!(year % 100) || (year % 400))))
				? 29
				: 28)
			: ((month == 4) || (month == 6) || (month == 9) || (month == 11))
			? 30
			: 31;
		const auto daysStartIndex = std::clamp(
			(state->days.current()
				? state->days.current()->index()
				: current.day()
				? (current.day() - 1)
				: (now.day() - 1)),
			0,
			daysCount - 1);
		const auto daysPaint = [=](QPainter &p, QRectF rect, int index) {
			p.drawText(rect, QString::number(index + 1), style::al_center);
		};
		const auto updated = picker(
			daysCount,
			daysStartIndex,
			daysPaint);
		delete state->days.current();
		state->days = updated;
		state->days.current()->show();
	}, years->lifetime());

	content->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(content);

		p.fillRect(r, Qt::transparent);

		const auto lineRect = QRect(
			0,
			content->height() / 2,
			content->width(),
			st::defaultInputField.borderActive);
		p.fillRect(lineRect.translated(0, itemHeight / 2), st::activeLineFg);
		p.fillRect(lineRect.translated(0, -itemHeight / 2), st::activeLineFg);
	}, content->lifetime());

	base::install_event_filter(box, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress) {
			years->handleKeyEvent(static_cast<QKeyEvent*>(e.get()));
		}
		return base::EventFilterResult::Continue;
	});

	box->addButton(tr::lng_settings_save(), [=] {
		const auto result = Data::Birthday(
			state->days.current()->index() + 1,
			state->months.current()->index() + 1,
			((years->index() == yearsCount - 1)
				? 0
				: minYear + years->index()));
		box->closeBox();
		save(result);
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
	if (current) {
		box->addLeftButton(tr::lng_settings_birthday_reset(), [=] {
			box->closeBox();
			save(Data::Birthday());
		});
	}
}

} // namespace Ui
