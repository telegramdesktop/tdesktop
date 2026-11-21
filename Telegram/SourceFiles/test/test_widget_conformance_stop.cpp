/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_conformance_stop.h"

#include "test/test_widget_conformance_bridge.h"
#include "test/test_widget_conformance_state.h"
#include "test/test_widget_fixture.h"
#include "test/test_widget_host.h"
#include "tgw/tgw_runtime.h"

namespace Test::Conformance {

QString ReadDesktopStop(
		const WidgetFixture &fixture,
		const WidgetFixtureMessage &message,
		int fatalFrozen) {
	const auto watch = message.host
		? ReadWidgetHost(*message.host)
		: WidgetHostReading();
	if (watch.hosts.size() != 1) {
		return QString();
	}
	return Tgw::ClipLogLine(WidgetDesktopStop(
		fixture,
		message.id,
		watch.hosts.front().core,
		fatalFrozen));
}

ConformanceRefusal StopRefusal(
		const QString &stops,
		const ConformanceRun &run) {
	if (stops.isEmpty() || !run.died || run.deathMeasured) {
		return {};
	}
	const auto desktop = run.desktopStop.isEmpty()
		? u"no stop of the desktop's own was observed apart from it"_q
		: (u"the desktop's own stop ["_q
			+ run.desktopStop
			+ u"], read apart, gives a hosted widget no cause"_q);
	const auto detail = u"the widget of an entry with stops [%1] died of a "
		u"charge of its selected works, but its sample gave no attributable "
		u"cause (death line [%2] of a %3 summary sampled from work %4, "
		u"while a hosted widget's cause comes only from the record of the "
		u"work a fresh or final summary was sampled from) and %5, so dead "
		u"is the runner's placeholder"_q.arg(
			stops,
			run.deathLine,
			(run.summaryOwn ? u"fresh or final"_q : u"stale or absent"_q),
			QString::number(run.summaryWork),
			desktop);
	return {
		.name = QString::fromLatin1(kStopReasonUnmeasured),
		.detail = detail,
	};
}

} // namespace Test::Conformance

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
