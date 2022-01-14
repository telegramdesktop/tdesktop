/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/choose_date_time.h"

#include "base/unixtime.h"
#include "base/event_filter.h"
#include "ui/boxes/calendar_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/time_input.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Ui {
namespace {

constexpr auto kMinimalSchedule = TimeId(10);

tr::phrase<> MonthDay(int index) {
	switch (index) {
	case 1: return tr::lng_month_day1;
	case 2: return tr::lng_month_day2;
	case 3: return tr::lng_month_day3;
	case 4: return tr::lng_month_day4;
	case 5: return tr::lng_month_day5;
	case 6: return tr::lng_month_day6;
	case 7: return tr::lng_month_day7;
	case 8: return tr::lng_month_day8;
	case 9: return tr::lng_month_day9;
	case 10: return tr::lng_month_day10;
	case 11: return tr::lng_month_day11;
	case 12: return tr::lng_month_day12;
	}
	Unexpected("Index in MonthDay.");
}

QString DayString(const QDate &date) {
	return tr::lng_month_day(
		tr::now,
		lt_month,
		MonthDay(date.month())(tr::now),
		lt_day,
		QString::number(date.day()));
}

QString TimeString(QTime time) {
	return QString("%1:%2"
	).arg(time.hour()
	).arg(time.minute(), 2, 10, QLatin1Char('0'));
}

} // namespace

ChooseDateTimeBoxDescriptor ChooseDateTimeBox(
		not_null<GenericBox*> box,
		ChooseDateTimeBoxArgs &&args) {
	struct State {
		rpl::variable<QDate> date;
		not_null<InputField*> day;
		not_null<TimeInput*> time;
		not_null<FlatLabel*> at;
	};
	box->setTitle(std::move(args.title));
	box->setWidth(st::boxWideWidth);

	const auto content = box->addRow(
		object_ptr<FixedHeightWidget>(box, st::scheduleHeight));
	if (args.description) {
		box->addRow(object_ptr<FlatLabel>(
			box,
			std::move(args.description),
			st::boxLabel));
	}
	const auto parsed = base::unixtime::parse(args.time);
	const auto state = box->lifetime().make_state<State>(State{
		.date = parsed.date(),
		.day = CreateChild<InputField>(
			content,
			st::scheduleDateField),
		.time = CreateChild<TimeInput>(
			content,
			TimeString(parsed.time()),
			st::scheduleTimeField,
			st::scheduleDateField,
			st::scheduleTimeSeparator,
			st::scheduleTimeSeparatorPadding),
		.at = CreateChild<FlatLabel>(
			content,
			tr::lng_schedule_at(),
			st::scheduleAtLabel),
	});

	state->date.value(
	) | rpl::start_with_next([=](QDate date) {
		state->day->setText(DayString(date));
		state->time->setFocusFast();
	}, state->day->lifetime());

	const auto min = args.min ? args.min : [] {
		return base::unixtime::now() + kMinimalSchedule;
	};
	const auto max = args.max ? args.max : [] {
		return base::unixtime::serialize(
			QDateTime::currentDateTime().addYears(1)) - 1;
	};
	const auto minDate = [=] {
		return base::unixtime::parse(min()).date();
	};
	const auto maxDate = [=] {
		return base::unixtime::parse(max()).date();
	};

	const auto &dayViewport = state->day->rawTextEdit()->viewport();
	base::install_event_filter(dayViewport, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Wheel) {
			const auto e = static_cast<QWheelEvent*>(event.get());
			const auto direction = Ui::WheelDirection(e);
			if (!direction) {
				return base::EventFilterResult::Continue;
			}
			const auto d = state->date.current().addDays(direction);
			state->date = std::clamp(d, minDate(), maxDate());
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	content->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto paddings = width
			- state->at->width()
			- 2 * st::scheduleAtSkip
			- st::scheduleDateWidth
			- st::scheduleTimeWidth;
		const auto left = paddings / 2;
		state->day->resizeToWidth(st::scheduleDateWidth);
		state->day->moveToLeft(left, st::scheduleDateTop, width);
		state->at->moveToLeft(
			left + st::scheduleDateWidth + st::scheduleAtSkip,
			st::scheduleAtTop,
			width);
		state->time->resizeToWidth(st::scheduleTimeWidth);
		state->time->moveToLeft(
			width - left - st::scheduleTimeWidth,
			st::scheduleDateTop,
			width);
	}, content->lifetime());

	const auto calendar =
		content->lifetime().make_state<QPointer<CalendarBox>>();
	QObject::connect(state->day, &InputField::focused, [=] {
		if (*calendar) {
			return;
		}
		*calendar = box->getDelegate()->show(
			Box<CalendarBox>(Ui::CalendarBoxArgs{
				.month = state->date.current(),
				.highlighted = state->date.current(),
				.callback = crl::guard(box, [=](QDate chosen) {
					state->date = chosen;
					(*calendar)->closeBox();
				}),
				.minDate = minDate(),
				.maxDate = maxDate(),
			}));
		(*calendar)->boxClosing(
		) | rpl::start_with_next(crl::guard(state->time, [=] {
			state->time->setFocusFast();
		}), (*calendar)->lifetime());
	});

	const auto collect = [=] {
		const auto timeValue = state->time->valueCurrent().split(':');
		if (timeValue.size() != 2) {
			return 0;
		}
		const auto time = QTime(timeValue[0].toInt(), timeValue[1].toInt());
		if (!time.isValid()) {
			return 0;
		}
		const auto result = base::unixtime::serialize(
			QDateTime(state->date.current(), time));
		if (result < min() || result > max()) {
			return 0;
		}
		return result;
	};
	const auto save = [=, done = args.done] {
		if (const auto result = collect()) {
			done(result);
		} else {
			state->time->showError();
		}
	};
	state->time->submitRequests(
	) | rpl::start_with_next(save, state->time->lifetime());

	auto result = ChooseDateTimeBoxDescriptor();
	box->setFocusCallback([=] { state->time->setFocusFast(); });
	result.submit = box->addButton(std::move(args.submit), save);
	result.collect = [=] {
		if (const auto result = collect()) {
			return result;
		}
		state->time->showError();
		return 0;
	};
	result.values = rpl::combine(
		state->date.value(),
		state->time->value()
	) | rpl::map(collect);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	return result;
}

} // namespace Ui
