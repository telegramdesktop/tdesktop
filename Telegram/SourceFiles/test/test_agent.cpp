/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_agent.h"

#ifdef _DEBUG

#include "test/test_log.h"
#include "settings.h"
#include "ui/style/style_core_scale.h"

namespace Test {
namespace {

[[nodiscard]] base::flat_set<QString> &FiredEvents() {
	static auto result = base::flat_set<QString>();
	return result;
}

} // namespace

bool Active() {
	return cTestAgent();
}

void ApplyStartupOverrides() {
	if (!Active()) {
		return;
	}
	const auto value = qEnvironmentVariable("TDESKTOP_TEST_SCALE");
	if (value.isEmpty()) {
		return;
	}
	auto ok = false;
	const auto scale = value.toInt(&ok);
	if (ok && scale >= style::kScaleMin && scale <= style::kScaleMax) {
		cSetConfigScale(scale);
		Note(u"TDESKTOP_TEST_SCALE applied: %1"_q.arg(scale));
	} else {
		Note(u"TDESKTOP_TEST_SCALE rejected: %1"_q.arg(value));
	}
}

void Fire(const QString &event) {
	if (!Active() || !FiredEvents().emplace(event).second) {
		return;
	}
	Note(u"event fired: %1"_q.arg(event));
}

bool HasFired(const QString &event) {
	return Active() && FiredEvents().contains(event);
}

} // namespace Test

#else // _DEBUG

namespace Test {

bool Active() {
	return false;
}

void ApplyStartupOverrides() {
}

void Fire(const QString &) {
}

bool HasFired(const QString &) {
	return false;
}

} // namespace Test

#endif // _DEBUG
