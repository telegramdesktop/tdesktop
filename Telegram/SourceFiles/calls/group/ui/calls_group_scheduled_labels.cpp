/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/ui/calls_group_scheduled_labels.h"

#include "ui/rp_widget.h"
#include "lang/lang_keys.h"
#include "base/unixtime.h"
#include "base/timer_rpl.h"
#include "styles/style_calls.h"

#include <QtCore/QDateTime>

namespace Calls::Group::Ui {

rpl::producer<QString> StartsWhenText(rpl::producer<TimeId> date) {
	return std::move(
		date
	) | rpl::map([](TimeId date) -> rpl::producer<QString> {
		const auto parsedDate = base::unixtime::parse(date);
		const auto dateDay = QDateTime(parsedDate.date(), QTime(0, 0));
		const auto previousDay = QDateTime(
			parsedDate.date().addDays(-1),
			QTime(0, 0));
		const auto now = QDateTime::currentDateTime();
		const auto kDay = int64(24 * 60 * 60);
		const auto tillTomorrow = int64(now.secsTo(previousDay));
		const auto tillToday = tillTomorrow + kDay;
		const auto tillAfter = tillToday + kDay;

		const auto time = parsedDate.time().toString(
			QLocale::system().timeFormat(QLocale::ShortFormat));
		auto exact = tr::lng_group_call_starts_short_date(
			lt_date,
			rpl::single(langDayOfMonthFull(dateDay.date())),
			lt_time,
			rpl::single(time)
		) | rpl::type_erased();
		auto tomorrow = tr::lng_group_call_starts_short_tomorrow(
			lt_time,
			rpl::single(time));
		auto today = tr::lng_group_call_starts_short_today(
			lt_time,
			rpl::single(time));

		auto todayAndAfter = rpl::single(
			std::move(today)
		) | rpl::then(base::timer_once(
			std::min(tillAfter, kDay) * crl::time(1000)
		) | rpl::map([=] {
			return rpl::duplicate(exact);
		})) | rpl::flatten_latest() | rpl::type_erased();

		auto tomorrowAndAfter = rpl::single(
			std::move(tomorrow)
		) | rpl::then(base::timer_once(
			std::min(tillToday, kDay) * crl::time(1000)
		) | rpl::map([=] {
			return rpl::duplicate(todayAndAfter);
		})) | rpl::flatten_latest() | rpl::type_erased();

		auto full = rpl::single(
			rpl::duplicate(exact)
		) | rpl::then(base::timer_once(
			tillTomorrow * crl::time(1000)
		) | rpl::map([=] {
			return rpl::duplicate(tomorrowAndAfter);
		})) | rpl::flatten_latest() | rpl::type_erased();

		if (tillTomorrow > 0) {
			return full;
		} else if (tillToday > 0) {
			return tomorrowAndAfter;
		} else if (tillAfter > 0) {
			return todayAndAfter;
		} else {
			return exact;
		}
	}) | rpl::flatten_latest();
}

object_ptr<Ui::RpWidget> CreateGradientLabel(
		QWidget *parent,
		rpl::producer<QString> text) {
	struct State {
		QBrush brush;
		QPainterPath path;
	};
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();
	const auto state = raw->lifetime().make_state<State>();

	std::move(
		text
	) | rpl::start_with_next([=](const QString &text) {
		state->path = QPainterPath();
		const auto &font = st::groupCallCountdownFont;
		state->path.addText(0, font->ascent, font->f, text);
		const auto width = font->width(text);
		raw->resize(width, font->height);
		auto gradient = QLinearGradient(QPoint(width, 0), QPoint());
		gradient.setStops(QGradientStops{
			{ 0.0, st::groupCallForceMutedBar1->c },
			{ .7, st::groupCallForceMutedBar2->c },
			{ 1.0, st::groupCallForceMutedBar3->c }
		});
		state->brush = QBrush(std::move(gradient));
		raw->update();
	}, raw->lifetime());

	raw->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		const auto skip = st::groupCallWidth / 20;
		const auto available = parent->width() - 2 * skip;
		const auto full = raw->width();
		if (available > 0 && full > available) {
			const auto scale = available / float64(full);
			const auto shift = raw->rect().center();
			p.translate(shift);
			p.scale(scale, scale);
			p.translate(-shift);
		}
		p.setPen(Qt::NoPen);
		p.setBrush(state->brush);
		p.drawPath(state->path);
	}, raw->lifetime());
	return result;
}

} // namespace Calls::Group::Ui
