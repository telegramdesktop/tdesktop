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

#include <QtCore/QFile>

namespace Test {
namespace {

[[nodiscard]] std::optional<QString> ReadTrimmed(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	const auto value = QString::fromUtf8(file.readAll()).trimmed();
	if (value.isEmpty()) {
		return std::nullopt;
	}
	return value;
}

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
	auto selectedScale = style::kScaleDefault;
	auto source = u"default"_q;
	if (!value.isEmpty()) {
		auto ok = false;
		const auto scale = value.toInt(&ok);
		if (ok && scale >= style::kScaleMin && scale <= style::kScaleMax) {
			selectedScale = style::CheckScale(scale);
			source = u"environment"_q;
		} else {
			Note(u"TDESKTOP_TEST_SCALE rejected: %1"_q.arg(value));
		}
	}
	cSetConfigScale(selectedScale);
	const auto report = u"TDESKTOP_TEST_SCALE=[%1] applied: %2 source=%3"_q
		.arg(
			value,
			QString::number(selectedScale),
			source);
	Note(report);
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

std::optional<QString> FixtureSecret(const QString &name) {
	if (!Active()) {
		return std::nullopt;
	} else if (auto live = ReadTrimmed(cWorkingDir() + name)) {
		return live;
	}
	return ReadTrimmed(cExeDir() + u"test_TelegramForcePortable/"_q + name);
}

std::optional<QString> TwoStepPassword() {
	return FixtureSecret(u"2svpassword.txt"_q);
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

std::optional<QString> FixtureSecret(const QString &) {
	return std::nullopt;
}

std::optional<QString> TwoStepPassword() {
	return std::nullopt;
}

} // namespace Test

#endif // _DEBUG
