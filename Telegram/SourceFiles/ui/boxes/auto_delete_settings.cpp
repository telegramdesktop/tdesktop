/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/auto_delete_settings.h"

#include "ui/widgets/checkbox.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

object_ptr<Ui::RpWidget> CreateSliderForTTL(
		not_null<QWidget*> parent,
		std::vector<QString> labels,
		int dashedAfterIndex,
		int selected,
		Fn<void(int)> callback) {
	Expects(labels.size() > 1);
	Expects(selected >= 0 && selected < labels.size());
	Expects(dashedAfterIndex >= 0 && dashedAfterIndex < labels.size());

	struct State {
		std::vector<int> points;
		std::vector<QString> labels;
		int selected = 0;
	};
	static const auto st = &st::defaultSliderForTTL;
	const auto height = st->font->height + st->skip + st->chosenSize;
	const auto count = int(labels.size());

	auto result = object_ptr<Ui::FixedHeightWidget>(parent.get(), height);
	const auto raw = result.data();
	const auto slider = Ui::CreateChild<Ui::FixedHeightWidget>(
		raw,
		st->chosenSize);
	slider->setCursor(style::cur_pointer);
	slider->move(0, height - slider->height());

	auto &lifetime = raw->lifetime();
	const auto state = lifetime.make_state<State>(State{
		.labels = std::move(labels),
		.selected = selected
	});
	state->points.resize(count, 0);

	raw->widthValue(
	) | rpl::start_with_next([=](int width) {
		for (auto i = 0; i != count; ++i) {
			state->points[i] = (width * i) / (count - 1);
		}
		slider->resize(width, slider->height());
	}, lifetime);

	raw->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(raw);

		p.setFont(st->font);
		for (auto i = 0; i != count; ++i) {
			// Label
			p.setPen(st->textFg);
			const auto &text = state->labels[i];
			const auto textWidth = st->font->width(text);
			const auto shift = (i == count - 1)
				? textWidth
				: (i > 0)
				? (textWidth / 2)
				: 0;
			const auto x = state->points[i] - shift;
			const auto y = st->font->ascent;
			p.drawText(x, y, text);
		}
	}, lifetime);

	slider->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(slider);
		auto hq = PainterHighQualityEnabler(p);

		p.setFont(st->font);
		for (auto i = 0; i != count; ++i) {
			const auto middle = (st->chosenSize / 2.);

			// Point
			const auto size = (i == state->selected)
				? st->chosenSize
				: st->pointSize;
			const auto pointfg = (i <= state->selected)
				? st->activeFg
				: st->inactiveFg;
			const auto shift = (i == count - 1)
				? float64(size)
				: (i > 0)
				? (size / 2.)
				: 0.;
			const auto pointx = state->points[i] - shift;
			const auto pointy = middle - (size / 2.);

			p.setPen(Qt::NoPen);
			p.setBrush(pointfg);
			p.drawEllipse(QRectF{ pointx, pointy, size * 1., size * 1. });

			// Line
			if (i + 1 == count) {
				break;
			}
			const auto nextSize = (i + 1 == state->selected)
				? st->chosenSize
				: st->pointSize;
			const auto nextShift = (i + 1 == count - 1)
				? float64(nextSize)
				: (nextSize / 2.);
			const auto &linefg = (i + 1 <= state->selected)
				? st->activeFg
				: st->inactiveFg;
			const auto from = pointx + size + st->stroke * 1.5;
			const auto till = state->points[i + 1] - nextShift - st->stroke * 1.5;

			auto pen = linefg->p;
			pen.setWidthF(st->stroke);
			if (i >= dashedAfterIndex) {
				// Try to fill the line with exact number of dash segments.
				// UPD Doesn't work so well because it changes when clicking.
				//const auto length = till - from;
				//const auto offSegmentsCount = int(std::round(
				//	(length - st->dashOn) / (st->dashOn + st->dashOff)));
				//const auto onSegmentsCount = offSegmentsCount + 1;
				//const auto idealLength = offSegmentsCount * st->dashOff
				//	+ onSegmentsCount * st->dashOn;
				//const auto multiplier = length / float64(idealLength);

				const auto multiplier = 1.;
				auto dashPattern = QVector<qreal>{
					st->dashOn * multiplier / st->stroke,
					st->dashOff * multiplier / st->stroke
				};
				pen.setDashPattern(dashPattern);
			}
			pen.setCapStyle(Qt::RoundCap);
			p.setPen(pen);

			p.setBrush(Qt::NoBrush);
			p.drawLine(QPointF(from, middle), QPointF(till, middle));
		}
	}, lifetime);

	slider->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::MouseButtonPress)
			&& (static_cast<QMouseEvent*>(e.get())->button()
				== Qt::LeftButton)
			&& (state->points[1] > 0);
	}) | rpl::map([=](not_null<QEvent*> e) {
		return rpl::single(
			static_cast<QMouseEvent*>(e.get())->pos()
		) | rpl::then(slider->events(
		) | rpl::take_while([=](not_null<QEvent*> e) {
			return (e->type() != QEvent::MouseButtonRelease)
				|| (static_cast<QMouseEvent*>(e.get())->button()
					!= Qt::LeftButton);
		}) | rpl::filter([=](not_null<QEvent*> e) {
			return (e->type() == QEvent::MouseMove);
		}) | rpl::map([=](not_null<QEvent*> e) {
			return static_cast<QMouseEvent*>(e.get())->pos();
		}));
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=](QPoint position) {
		state->selected = std::clamp(
			(position.x() + (state->points[1] / 2)) / state->points[1],
			0,
			count - 1);
		slider->update();
		callback(state->selected);
	}, lifetime);

	return result;
}

} // namespace

void AutoDeleteSettingsBox(
		not_null<Ui::GenericBox*> box,
		TimeId ttlPeriod,
		rpl::producer<QString> about,
		Fn<void(TimeId)> callback) {
	box->setTitle(tr::lng_manage_messages_ttl_title());

	struct State {
		TimeId period = 0;
	};

	const auto state = box->lifetime().make_state<State>(State{
		.period = ttlPeriod,
	});

	const auto options = std::vector<QString>{
		tr::lng_manage_messages_ttl_never(tr::now),
		//u"5 seconds"_q, AssertIsDebug()
		tr::lng_manage_messages_ttl_after1(tr::now),
		tr::lng_manage_messages_ttl_after2(tr::now),
	};
	const auto periodToIndex = [&](TimeId period) {
		return !period
			? 0
			//: (period == 5) AssertIsDebug()
			//? 1 AssertIsDebug()
			: (period < 3 * 86400)
			? 1
			: 2;
	};
	const auto indexToPeriod = [&](int index) {
		return !index
			? 0
			//: (index == 1) AssertIsDebug()
			//? 5 AssertIsDebug()
			: (index == 1)
			? 86400
			: 7 * 86400;
	};
	const auto sliderCallback = [=](int index) {
		state->period = indexToPeriod(index);
	};
	const auto slider = box->addRow(
		CreateSliderForTTL(
			box,
			options | ranges::to_vector,
			options.size() - 1,
			periodToIndex(ttlPeriod),
			sliderCallback),
		{
			st::boxRowPadding.left(),
			0,
			st::boxRowPadding.right(),
			st::boxMediumSkip });

	const auto description = box->addRow(
		object_ptr<Ui::DividerLabel>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(about),
				st::boxDividerLabel),
			st::ttlDividerLabelPadding),
		style::margins());

	box->addButton(tr::lng_settings_save(), [=] {
		const auto period = state->period;
		box->closeBox();

		callback(period);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace Ui
