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
		Work(not_null<WidgetMeter*> meter, bool refuseHeld);
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

	using Publish = Fn<void(float64 seconds)>;

	explicit WidgetMeter(Fn<void(QString)> refuse);

	[[nodiscard]] Operation operation();
	[[nodiscard]] Work work();
	[[nodiscard]] Work workUnlessHeld();
	void input();
	[[nodiscard]] crl::time heldFor() const;
	void publish(Publish release, Publish exemption);

private:
	struct Charge {
		float64 at = 0.;
		float64 cost = 0.;
		int heavy = 0;
	};

	[[nodiscard]] bool admit(bool refuseHeld);
	void spend(crl::profile_time spent);
	void close();
	void charge(float64 seconds);

	const Fn<void(QString)> _refuse;
	Publish _release;
	Publish _exemption;
	std::deque<Charge> _charges;
	float64 _first = -1.;
	float64 _until = 0.;
	float64 _input = -1.;
	float64 _spent = 0.;
	int _operations = 0;
	int _works = 0;

};

} // namespace Tgw
