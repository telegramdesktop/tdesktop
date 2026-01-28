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
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/time_input.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QtWidgets/QTextEdit>

namespace Ui {
namespace {

constexpr auto kMinimalSchedule = TimeId(10);

QString DayString(const QDate &date) {
	return tr::lng_month_day(
		tr::now,
		lt_month,
		Lang::MonthDay(date.month())(tr::now),
		lt_day,
		QString::number(date.day()));
}

QString TimeString(QTime time) {
	return QString("%1:%2"
	).arg(time.hour()
	).arg(time.minute(), 2, 10, QLatin1Char('0'));
}

} // namespace

ChooseDateTimeStyleArgs::ChooseDateTimeStyleArgs()
: labelStyle(&st::boxLabel)
, dateFieldStyle(&st::scheduleDateField)
, timeFieldStyle(&st::scheduleTimeField)
, separatorStyle(&st::scheduleTimeSeparator)
, atStyle(&st::scheduleAtLabel)
, calendarStyle(&st::defaultCalendarColors) {
}

ChooseDateTimeBoxDescriptor ChooseDateTimeBox(
		not_null<GenericBox*> box,
		ChooseDateTimeBoxArgs &&args) {
	struct State {
		rpl::variable<QDate> date;
		rpl::variable<int> width;
		not_null<InputField*> day;
		not_null<TimeInput*> time;
		not_null<FlatLabel*> at;
	};
	box->setTitle(std::move(args.title));
	box->setWidth(st::boxWideWidth);

	const auto content = box->addRow(
		object_ptr<FixedHeightWidget>(box, st::scheduleHeight),
		style::al_top);
	if (args.description) {
		box->addRow(object_ptr<FlatLabel>(
			box,
			std::move(args.description),
			*args.style.labelStyle));
	}
	const auto parsed = base::unixtime::parse(args.time);
	const auto state = box->lifetime().make_state<State>(State{
		.date = parsed.date(),
		.day = CreateChild<InputField>(
			content,
			*args.style.dateFieldStyle),
		.time = CreateChild<TimeInput>(
			content,
			TimeString(parsed.time()),
			*args.style.timeFieldStyle,
			*args.style.dateFieldStyle,
			*args.style.separatorStyle,
			st::scheduleTimeSeparatorPadding),
		.at = CreateChild<FlatLabel>(
			content,
			tr::lng_schedule_at(),
			*args.style.atStyle),
	});

	state->date.value(
	) | rpl::on_next([=](QDate date) {
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

	state->at->widthValue() | rpl::on_next([=](int width) {
		const auto full = st::scheduleDateWidth
			+ st::scheduleAtSkip
			+ width
			+ st::scheduleAtSkip
			+ st::scheduleTimeWidth;
		content->setNaturalWidth(full);
		state->width = full;
	}, state->at->lifetime());

	content->widthValue(
	) | rpl::on_next([=](int width) {
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

	const auto calendar
		= content->lifetime().make_state<base::weak_qptr<CalendarBox>>();
	const auto calendarStyle = args.style.calendarStyle;
	state->day->focusedChanges(
	) | rpl::on_next([=](bool focused) {
		if (*calendar || !focused) {
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
				.stColors = *calendarStyle,
			}));
		(*calendar)->boxClosing(
		) | rpl::on_next(crl::guard(state->time, [=] {
			state->time->setFocusFast();
		}), (*calendar)->lifetime());
	}, state->day->lifetime());

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
	) | rpl::on_next(save, state->time->lifetime());

	auto result = ChooseDateTimeBoxDescriptor();
	box->setFocusCallback([=] { state->time->setFocusFast(); });
	result.width = state->width.value();
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

object_ptr<Ui::RpWidget> ChooseRepeatPeriod(
		not_null<Ui::RpWidget*> parent,
		ChooseRepeatPeriodArgs &&args) {
	auto result = object_ptr<Ui::RpWidget>(parent.get());
	const auto raw = result.data();

	struct Entry {
		TimeId value = 0;
		QString text;
	};
	auto map = std::vector<Entry>{
		{ 0, tr::lng_schedule_repeat_never(tr::now) },
		{ 24 * 60 * 60, tr::lng_schedule_repeat_daily(tr::now) },
		{ 7 * 24 * 60 * 60, tr::lng_schedule_repeat_weekly(tr::now) },
		{ 14 * 24 * 60 * 60, tr::lng_schedule_repeat_biweekly(tr::now) },
		{ 30 * 24 * 60 * 60, tr::lng_schedule_repeat_monthly(tr::now) },
		{
			91 * 24 * 60 * 60,
			tr::lng_schedule_repeat_every_month(tr::now, lt_count, 3)
		},
		{
			182 * 24 * 60 * 60,
			tr::lng_schedule_repeat_every_month(tr::now, lt_count, 6)
		},
		{ 365 * 24 * 60 * 60, tr::lng_schedule_repeat_yearly(tr::now) },
	};
	if (args.test) {
		map.insert(begin(map) + 1, Entry{ 300, u"Every 5 minutes"_q });
		map.insert(begin(map) + 1, Entry{ 60, u"Every minute"_q });
	}

	const auto label = Ui::CreateChild<Ui::FlatLabel>(raw, QString());
	rpl::combine(
		raw->widthValue(),
		label->naturalWidthValue()
	) | rpl::on_next([=](int outer, int natural) {
		label->resizeToWidth(std::min(outer, natural));
	}, raw->lifetime());
	label->heightValue() | rpl::on_next([=](int height) {
		raw->resize(raw->width(), height);
	}, label->lifetime());

	struct State {
		rpl::variable<TimeId> value;
		rpl::variable<bool> locked;
		std::unique_ptr<Ui::PopupMenu> menu;
	};
	const auto state = raw->lifetime().make_state<State>(State{
		.value = args.value,
		.locked = std::move(args.locked),
	});

	rpl::combine(
		state->value.value(),
		state->locked.value()
	) | rpl::on_next([=](TimeId value, bool locked) {
		auto result = tr::lng_schedule_repeat_label(
			tr::now,
			tr::marked);

		const auto text = [&] {
			const auto i = ranges::lower_bound(
				map,
				value,
				ranges::less{},
				&Entry::value);
			return (i != end(map)) ? i->text : map.back().text;
		}();

		label->setMarkedText(result.append(' ').append(tr::link(
			tr::bold(text).append(
				Ui::Text::IconEmoji(locked
					? &st::scheduleRepeatDropdownLock
					: &st::scheduleRepeatDropdownArrow))
		)));
		return result;
	}, label->lifetime());

	label->setClickHandlerFilter([=](const auto &...) {
		if (args.filter && args.filter()) {
			return false;
		}
		const auto changed = args.changed;

		state->menu = std::make_unique<Ui::PopupMenu>(label);
		const auto menu = state->menu.get();

		menu->setDestroyedCallback(crl::guard(label, [=] {
			if (state->menu.get() == menu) {
				state->menu.release();
			}
		}));
		for (const auto &entry : map) {
			const auto value = entry.value;
			menu->addAction(entry.text, [=] {
				state->value = value;
				changed(value);
			});
		}
		menu->popup(QCursor::pos());
		return false;
	});

	return result;
}

} // namespace Ui
