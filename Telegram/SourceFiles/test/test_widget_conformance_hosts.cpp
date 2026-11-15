/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_conformance_hosts.h"

#include "test/test_widget_conformance_state.h"
#include "test/test_widget_conformance_stop.h"
#include "test/test_widget_fixture.h"
#include "test/test_widget_host.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QStringList>

#include <array>

namespace Test::Conformance {
namespace {

// One construction's content safe area, in CSS px, as the host got it.
[[nodiscard]] QJsonObject SafeAreaJson(
		const twidget::WidgetContentSafeArea &area) {
	return QJsonObject{
		{ u"top"_q, area.top },
		{ u"right"_q, area.right },
		{ u"bottom"_q, area.bottom },
		{ u"left"_q, area.left },
		{ u"cornerLeft"_q, area.cornerLeft },
		{ u"cornerRight"_q, area.cornerRight },
	};
}

[[nodiscard]] bool SafeAreaZero(const twidget::WidgetContentSafeArea &area) {
	return (area.top == 0.)
		&& (area.right == 0.)
		&& (area.bottom == 0.)
		&& (area.left == 0.)
		&& (area.cornerLeft == 0.)
		&& (area.cornerRight == 0.);
}

} // namespace

QJsonObject SafeAreaObject(const ConformanceRun &run) {
	auto applied = QJsonArray();
	for (const auto &area : run.safeAreas) {
		applied.push_back(SafeAreaJson(area));
	}
	const auto &caption = run.message->caption;
	return QJsonObject{
		{ u"caption"_q, caption.isEmpty()
			? QJsonValue(QJsonValue::Null)
			: QJsonValue(caption) },
		{ u"applied"_q, applied },
	};
}

RunnerRefusal SafeAreaRefusal(
		bool safeAreaEntry,
		const std::vector<twidget::WidgetContentSafeArea> &applied,
		bool loaded) {
	if (applied.empty()) {
		return !loaded
			? RunnerRefusal()
			: RunnerRefusal{
				.name = u"safe-area-unmeasured"_q,
				.detail = u"the widget loaded, but its host watch recorded no "
					u"construction's options to read the content safe area "
					u"from"_q,
			};
	} else if (safeAreaEntry) {
		return {};
	}
	for (auto i = 0; i != int(applied.size()); ++i) {
		if (!SafeAreaZero(applied[i])) {
			return {
				.name = u"safe-area-not-zero"_q,
				.detail = u"construction %1 of the host of a message with a "
					u"caption below the widget got %2, not all 0, so the run "
					u"is not in CLIENTS.md's zero state"_q.arg(
						NumberText(i + 1),
						CompactJson(SafeAreaJson(applied[i]))),
			};
		}
	}
	return {};
}

void ReadSafeAreas(ConformanceRun &run) {
	const auto &host = run.message->host;
	if (!host) {
		return;
	}
	for (const auto &applied : ReadWidgetHost(*host).applied) {
		run.safeAreas.push_back(applied.contentSafeArea);
	}
	run.safeAreaRead = true;
}

WidgetOpsSelfCheckRow CheckHostReadings() {
	using SafeArea = twidget::WidgetContentSafeArea;
	const auto zero = SafeArea();
	const auto band = SafeArea{ .bottom = 26. };
	const auto corner = SafeArea{ .cornerRight = 118. };
	const auto bandCorner = SafeArea{ .bottom = 26., .cornerRight = 118. };
	struct SafeAreaCase {
		const char *name = nullptr;
		std::vector<SafeArea> applied;
		const char *refusal = "";
		bool entry = false;
		bool loaded = false;
	};
	const auto safeAreaCases = std::array{
		SafeAreaCase{
			.name = "zero entry, all 0",
			.applied = { zero },
			.loaded = true,
		},
		SafeAreaCase{
			.name = "zero entry, bottom 26",
			.applied = { band },
			.refusal = "safe-area-not-zero",
			.loaded = true,
		},
		SafeAreaCase{
			.name = "zero entry, cornerRight 118 only",
			.applied = { corner },
			.refusal = "safe-area-not-zero",
			.loaded = true,
		},
		SafeAreaCase{
			.name = "zero entry, loaded, no applied",
			.refusal = "safe-area-unmeasured",
			.loaded = true,
		},
		SafeAreaCase{
			.name = "zero entry, not loaded, no applied",
		},
		SafeAreaCase{
			.name = "safe-area entry, 26/118",
			.applied = { bandCorner },
			.entry = true,
			.loaded = true,
		},
		SafeAreaCase{
			.name = "safe-area entry, all 0",
			.applied = { zero },
			.entry = true,
			.loaded = true,
		},
	};
	struct StopCase {
		const char *name = nullptr;
		const char *stops = "";
		const char *deathLine = "";
		const char *refusal = "";
		bool died = false;
	};
	const auto stopCases = std::array{
		StopCase{
			.name = "stop died with a Died: line",
			.stops = "Text limit exceeded",
			.deathLine = "Died: Text limit exceeded: a control reason",
			.died = true,
		},
		StopCase{
			.name = "stop died without one",
			.stops = "Text limit exceeded",
			.refusal = "stop-reason-unmeasured",
			.died = true,
		},
		StopCase{
			.name = "stop not died",
			.stops = "Text limit exceeded",
		},
		StopCase{
			.name = "non-stop died without one",
			.died = true,
		},
	};
	auto readings = QStringList();
	auto passed = true;
	const auto judge = [&](
			const char *name,
			const RunnerRefusal &got,
			const char *refusal) {
		const auto expected = QString::fromLatin1(refusal);
		passed = passed
			&& (got.name == expected)
			&& (got.detail.isEmpty() == expected.isEmpty());
		readings.push_back(u"{%1: [%2]}"_q.arg(
			QString::fromLatin1(name),
			got.name));
	};
	for (const auto &entry : safeAreaCases) {
		judge(
			entry.name,
			SafeAreaRefusal(entry.entry, entry.applied, entry.loaded),
			entry.refusal);
	}
	for (const auto &entry : stopCases) {
		judge(
			entry.name,
			StopRefusal(
				QString::fromLatin1(entry.stops),
				entry.died,
				QString::fromLatin1(entry.deathLine),
				false),
			entry.refusal);
	}

	auto control = ConformanceState();
	control.tests.push_back(ManifestTest{
		.id = u"auto/s"_q,
		.set = u"auto"_q,
		.stops = u"Text limit exceeded"_q,
	});
	control.tests.push_back(ManifestTest{
		.id = u"auto/p"_q,
		.set = u"auto"_q,
	});
	const auto endsAtDone = [&](int test) {
		auto run = ConformanceRun{
			.id = control.tests[test].id,
			.set = u"auto"_q,
			.theme = u"light"_q,
		};
		run.test = test;
		run.result.present = true;
		run.result.status = u"done"_q;
		return WantedStatus(control, run, ResultWait::Initial);
	};
	const auto stopEnds = endsAtDone(0);
	const auto plainEnds = endsAtDone(1);
	passed = passed && !stopEnds && plainEnds;
	readings.push_back(u"{done ends the wait: stop entry %1, plain auto "
		u"entry %2}"_q.arg(
			NumberText(stopEnds ? 1 : 0),
			NumberText(plainEnds ? 1 : 0)));
	return {
		.name = Prefixed(u"host reading self-check: a run with a caption "
			u"below the widget is refused for any nonzero content safe area, "
			u"a loaded run without a reading is refused, a safeArea entry's "
			u"band never is, a stops entry's death without its summary reason "
			u"is refused, and done never ends a stops entry's result wait"_q),
		.details = readings.join(QChar(' ')),
		.passed = passed,
	};
}

} // namespace Test::Conformance

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
