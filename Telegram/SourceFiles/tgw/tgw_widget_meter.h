/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Tgw {

class WidgetMeter final {
public:
	class Operation final {
	public:
		explicit Operation(not_null<WidgetMeter*> meter);
		Operation(const Operation &) = delete;
		Operation &operator=(const Operation &) = delete;
		~Operation();

	private:
		const not_null<WidgetMeter*> _meter;

	};

	class Work final {
	public:
		explicit Work(not_null<WidgetMeter*> meter);
		Work(const Work &) = delete;
		Work &operator=(const Work &) = delete;
		~Work();

		[[nodiscard]] bool admitted() const;

	private:
		const not_null<WidgetMeter*> _meter;
		const bool _outermost = false;
		const bool _admitted = false;
		const crl::profile_time _started = 0;

	};

	explicit WidgetMeter(Fn<void(QString)> refuse);

	[[nodiscard]] Operation operation();
	[[nodiscard]] Work work();
	void input();
	[[nodiscard]] crl::time heldFor() const;

private:
	struct Charge {
		float64 at = 0.;
		float64 cost = 0.;
		int heavy = 0;
	};

	[[nodiscard]] bool admit();
	void spend(crl::profile_time spent);
	void close();
	void charge(float64 seconds);

	const Fn<void(QString)> _refuse;
	std::deque<Charge> _charges;
	float64 _first = -1.;
	float64 _until = 0.;
	float64 _input = -1.;
	float64 _spent = 0.;
	int _operations = 0;
	int _works = 0;

};

} // namespace Tgw
