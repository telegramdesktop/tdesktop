/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_widget_meter.h"

#include "tgw/tgw_painter.h"

namespace Tgw {
namespace {

constexpr auto kThrottleFree = 0.016;
constexpr auto kThrottleFactor = 3.;
constexpr auto kSustainedFactor = 19.;
constexpr auto kSustainedWindow = 2.;
constexpr auto kSustainedLimit = 0.1;
constexpr auto kHeavyPass = 0.05;
constexpr auto kLoadGrace = 1.;
constexpr auto kInputImmediate = 0.25;

[[nodiscard]] float64 Now() {
	return crl::now() / 1000.;
}

} // namespace

WidgetMeter::Operation::Operation(not_null<WidgetMeter*> meter)
: _meter(meter) {
	++_meter->_operations;
}

WidgetMeter::Operation::~Operation() {
	_meter->close();
}

WidgetMeter::Work::Work(not_null<WidgetMeter*> meter, bool refuseHeld)
: _meter(meter)
, _outermost(!meter->_works++)
, _admitted(!_outermost || meter->admit(refuseHeld))
, _started((_outermost && _admitted) ? crl::profile() : 0) {
}

WidgetMeter::Work::~Work() {
	--_meter->_works;
	if (_outermost && _admitted) {
		_meter->spend(crl::profile() - _started);
	}
}

bool WidgetMeter::Work::admitted() const {
	return _admitted;
}

WidgetMeter::WidgetMeter(Fn<void(QString)> refuse)
: _refuse(std::move(refuse)) {
}

WidgetMeter::Operation WidgetMeter::operation() {
	return Operation(this);
}

WidgetMeter::Work WidgetMeter::work() {
	return Work(this, true);
}

WidgetMeter::Work WidgetMeter::workUnlessHeld() {
	return Work(this, false);
}

void WidgetMeter::input() {
	_input = Now();
	if (_exemption) {
		_exemption(kInputImmediate);
	}
}

crl::time WidgetMeter::heldFor() const {
	const auto now = Now();
	if ((now >= _until)
		|| ((_input >= 0.) && (now - _input <= kInputImmediate))) {
		return 0;
	}
	return crl::time(std::ceil((_until - now) * 1000.));
}

void WidgetMeter::publish(Publish release, Publish exemption) {
	_release = std::move(release);
	_exemption = std::move(exemption);
	const auto now = Now();
	if (_release && (_until > now)) {
		_release(_until - now);
	}
	const auto since = now - _input;
	if (_exemption && (_input >= 0.) && (since < kInputImmediate)) {
		_exemption(kInputImmediate - since);
	}
}

bool WidgetMeter::admit(bool refuseHeld) {
	if (_operations > 0) {
		return true;
	}
	const auto held = heldFor();
	if (!held) {
		return true;
	} else if (refuseHeld) {
		_refuse(PaintLimit(u"a request while held for %1 ms"_q.arg(held), 0));
	}
	return false;
}

void WidgetMeter::spend(crl::profile_time spent) {
	const auto seconds = spent / 1'000'000.;
	if (_operations > 0) {
		_spent += seconds;
	} else {
		charge(seconds);
	}
}

void WidgetMeter::close() {
	if (!--_operations) {
		charge(base::take(_spent));
	}
}

void WidgetMeter::charge(float64 seconds) {
	const auto now = Now();
	if (_first < 0.) {
		_first = now;
	}
	if ((seconds <= 0.) || (now - _first < kLoadGrace)) {
		return;
	}
	while (!_charges.empty()
		&& (now - _charges.front().at > kSustainedWindow)) {
		_charges.pop_front();
	}
	if (_charges.empty() || (_charges.back().at != now)) {
		_charges.push_back({ .at = now });
	}
	auto &last = _charges.back();
	last.cost += seconds;
	if (seconds > kHeavyPass) {
		++last.heavy;
	}
	if (seconds <= kThrottleFree) {
		return;
	}
	auto total = 0.;
	auto heavy = 0;
	for (const auto &entry : _charges) {
		total += entry.cost;
		heavy += entry.heavy;
	}
	const auto factor = ((total > kSustainedLimit * kSustainedWindow)
		&& (heavy >= 2))
		? kSustainedFactor
		: kThrottleFactor;
	_until = std::max(_until, now) + seconds * factor;
	if (_release) {
		_release(_until - now);
	}
}

} // namespace Tgw
