/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_helper_probe.h"

#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "settings.h"

#include <QtCore/QStandardPaths>

namespace Test {
namespace {

[[nodiscard]] QString ProbeMode() {
	return QString::fromLatin1(kWidgetHelperProbeMode);
}

[[nodiscard]] QString UnsandboxedMode() {
	return QString::fromLatin1(kWidgetHelperUnsandboxedMode);
}

[[nodiscard]] QString ConnectControlArgument() {
	return QString::fromLatin1(kWidgetHelperConnectControl);
}

[[nodiscard]] QString ServiceArgument(const QString &name) {
	return QString::fromLatin1(kWidgetHelperServicePrefix) + name;
}

} // namespace

bool IsWidgetHelperProbeMode(const QString &mode) {
	return (mode == ProbeMode()) || (mode == UnsandboxedMode());
}

QStringList WidgetHelperProbeArguments(const QString &mode) {
	return WidgetHelperProbeArguments(mode, {});
}

QStringList WidgetHelperProbeArguments(
		const QString &mode,
		const WidgetHelperProbeFixture &fixture) {
	if (!IsWidgetHelperProbeMode(mode)) {
		return {};
	}
	auto result = QStringList{
		mode,
		cWorkingDir(),
		QStandardPaths::writableLocation(
			QStandardPaths::GenericDataLocation),
	};
	if (!fixture.keychainAccount.isEmpty()
		|| fixture.loopbackPort > 0
		|| fixture.connectControl
		|| !fixture.networkTargets.isEmpty()
		|| !fixture.serviceNames.isEmpty()) {
		result.push_back(fixture.keychainAccount);
		result.push_back(QString::number(std::max(fixture.loopbackPort, 0)));
		if (fixture.connectControl) {
			result.push_back(ConnectControlArgument());
		}
		result += fixture.networkTargets;
		for (const auto &name : fixture.serviceNames) {
			result.push_back(ServiceArgument(name));
		}
	}
	return result;
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
