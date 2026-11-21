/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_conformance_stop.h"

#include "test/test_widget_conformance_bridge.h"
#include "test/test_widget_conformance_death.h"
#include "test/test_widget_conformance_state.h"
#include "test/test_widget_fixture.h"
#include "test/test_widget_host.h"
#include "tgw/tgw_runtime.h"

#include <QtCore/QStringList>

#include <array>

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
	const auto desktop = (run.desktopStop.isEmpty() && run.deathHosted)
		? u"no stop of the desktop's own was observed apart from it"_q
		: run.desktopStop.isEmpty()
		? u"the desktop did not stop it itself"_q
		: run.deathHosted
		? (u"the desktop's own stop ["_q
			+ run.desktopStop
			+ u"], read apart, gives a hosted widget no cause"_q)
		: (u"the desktop's own stop ["_q
			+ run.desktopStop
			+ u"] gives none beside that summary's \"Died: \" line"_q);
	const auto detail = run.deathHosted
		? u"the widget of an entry with stops [%1] died of a charge of its "
			u"selected works, but its sample gave no attributable cause "
			u"(death line [%2] of a %3 summary sampled from work %4, while a "
			u"hosted widget's cause comes only from the record of the work a "
			u"fresh or final summary was sampled from) and %5, so dead is "
			u"the runner's placeholder"_q.arg(
				stops,
				run.deathLine,
				(run.summaryOwn ? u"fresh or final"_q : u"stale or absent"_q),
				QString::number(run.summaryWork),
				desktop)
		: u"the widget of an entry with stops [%1] died, but no fresh or "
			u"final summary of its host gave its \"Died: \" reason (death "
			u"line [%2]) and %3, so dead is the runner's placeholder"_q.arg(
				stops,
				run.deathLine,
				desktop);
	return {
		.name = QString::fromLatin1(kStopReasonUnmeasured),
		.detail = detail,
	};
}

std::vector<WidgetOpsSelfCheckRow> RunDesktopStopCheck() {
	struct StopCase {
		const char *name = nullptr;
		const char *stops = "";
		const char *deathLine = "";
		const char *desktopStop = "";
		const char *refusal = "";
		bool frozen = false;
	};
	const auto stopCases = std::array{
		StopCase{
			.name = "stop died terminated, the desktop's own stop",
			.stops = "Paint limit exceeded",
			.deathLine = "Died: terminated",
			.desktopStop = "Paint limit exceeded: a control reason",
		},
		StopCase{
			.name = "stop died terminated, no desktop stop",
			.stops = "Paint limit exceeded",
			.deathLine = "Died: terminated",
			.refusal = "stop-reason-unmeasured",
			.frozen = true,
		},
	};
	auto readings = QStringList();
	auto passed = true;
	for (const auto &entry : stopCases) {
		auto run = ConformanceRun();
		auto reading = WidgetOpsReading();
		reading.fatalFrozen = entry.frozen ? 1 : 0;
		ReadDeath(
			run,
			WidgetRuntimeLog{
				.summary = QString::fromLatin1(entry.deathLine),
				.summaryState = WidgetLogSummary::Fresh,
			},
			reading,
			QString::fromLatin1(entry.desktopStop));
		const auto got = StopRefusal(QString::fromLatin1(entry.stops), run);
		const auto expected = QString::fromLatin1(entry.refusal);
		passed = passed
			&& run.died
			&& (got.name == expected)
			&& (got.detail.isEmpty() == expected.isEmpty());
		readings.push_back(u"{%1: died=%2 dead=[%3] [%4]}"_q.arg(
			QString::fromLatin1(entry.name),
			run.died ? u"1"_q : u"0"_q,
			run.dead,
			got.name));
	}
	return { WidgetOpsSelfCheckRow{
		.name = u"desktop stop self-check: a stops entry's death with only "
			u"\"Died: terminated\" is refused unless the desktop stopped it "
			u"itself"_q,
		.details = readings.join(QChar(' ')),
		.passed = passed,
	} };
}

} // namespace Test::Conformance

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
