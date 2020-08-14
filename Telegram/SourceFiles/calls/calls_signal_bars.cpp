/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_signal_bars.h"

#include "calls/calls_call.h"
#include "styles/style_calls.h"

namespace Calls {

SignalBars::SignalBars(
	QWidget *parent,
	not_null<Call*> call,
	const style::CallSignalBars &st)
: RpWidget(parent)
, _st(st)
, _count(Call::kSignalBarStarting) {
	resize(
		_st.width + (_st.width + _st.skip) * (Call::kSignalBarCount - 1),
		_st.max);
	call->signalBarCountValue(
	) | rpl::start_with_next([=](int count) {
		changed(count);
	}, lifetime());
}

void SignalBars::paintEvent(QPaintEvent *e) {
	Painter p(this);

	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(_st.color);
	for (auto i = 0; i < Call::kSignalBarCount; ++i) {
		p.setOpacity((i < _count) ? 1. : _st.inactiveOpacity);
		const auto barHeight = _st.min
			+ (_st.max - _st.min) * (i / float64(Call::kSignalBarCount - 1));
		const auto barLeft = i * (_st.width + _st.skip);
		const auto barTop = height() - barHeight;
		p.drawRoundedRect(
			QRectF(
				barLeft,
				barTop,
				_st.width,
				barHeight),
			_st.radius,
			_st.radius);
	}
	p.setOpacity(1.);
}

void SignalBars::changed(int count) {
	if (_count == Call::kSignalBarFinished) {
		return;
	} else if (_count != count) {
		_count = count;
		update();
	}
}

} // namespace Calls
