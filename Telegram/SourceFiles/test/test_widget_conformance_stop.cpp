/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_conformance_stop.h"

#include "test/test_widget_conformance_bridge.h"
#include "test/test_widget_fixture.h"
#include "test/test_widget_host.h"

#include <QtCore/QStringList>

#include <array>

namespace Test::Conformance {
namespace {

// kMaxLogLine of tgw_runtime.cpp: Clip() cuts a summary's dead to this many
// characters, its "..." included.
constexpr auto kMaxDeathChars = 240;

// Clip() of tgw_runtime.cpp, which cuts the host's dead in the "Died: "
// line, for a reason that never passes through it.
[[nodiscard]] QString ClipDeath(QString text) {
	if (text.size() <= kMaxDeathChars) {
		return text;
	}
	const auto ellipsis = u"..."_q;
	text.truncate(kMaxDeathChars - ellipsis.size());
	if (text.at(text.size() - 1).isHighSurrogate()) {
		text.chop(1);
	}
	return text + ellipsis;
}

} // namespace

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
	return ClipDeath(WidgetDesktopStop(
		fixture,
		message.id,
		watch.hosts.front().core,
		fatalFrozen));
}

ConformanceRefusal StopRefusal(
		const QString &stops,
		bool died,
		const QString &deathLine,
		bool desktopStop) {
	const auto reason = deathLine.startsWith(u"Died: "_q)
		? deathLine.mid(6).trimmed()
		: QString();
	if (stops.isEmpty()
		|| !died
		|| desktopStop
		|| (!reason.isEmpty() && (reason != u"terminated"_q))) {
		return {};
	}
	return {
		.name = u"stop-reason-unmeasured"_q,
		.detail = u"the widget of an entry with stops [%1] died, but no "
			u"fresh or final summary of its host gave its \"Died: \" reason "
			u"(death line [%2]) and the desktop did not stop it itself, so "
			u"dead is the runner's placeholder"_q.arg(stops, deathLine),
	};
}

std::vector<WidgetOpsSelfCheckRow> RunDesktopStopCheck() {
	struct StopCase {
		const char *name = nullptr;
		const char *stops = "";
		const char *deathLine = "";
		const char *refusal = "";
		bool died = false;
		bool desktopStop = false;
	};
	const auto stopCases = std::array{
		StopCase{
			.name = "stop died terminated, the desktop's own stop",
			.stops = "Paint limit exceeded",
			.deathLine = "Died: terminated",
			.died = true,
			.desktopStop = true,
		},
		StopCase{
			.name = "stop died terminated, no desktop stop",
			.stops = "Paint limit exceeded",
			.deathLine = "Died: terminated",
			.refusal = "stop-reason-unmeasured",
			.died = true,
		},
	};
	auto readings = QStringList();
	auto passed = true;
	for (const auto &entry : stopCases) {
		const auto got = StopRefusal(
			QString::fromLatin1(entry.stops),
			entry.died,
			QString::fromLatin1(entry.deathLine),
			entry.desktopStop);
		const auto expected = QString::fromLatin1(entry.refusal);
		passed = passed
			&& (got.name == expected)
			&& (got.detail.isEmpty() == expected.isEmpty());
		readings.push_back(u"{%1: [%2]}"_q.arg(
			QString::fromLatin1(entry.name),
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
