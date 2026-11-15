/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_conformance_paint_policy.h"

#include "test/test_widget_paint_policy.h"

#include <QtCore/QDir>
#include <QtCore/QFile>

namespace Test::Conformance {
namespace {

[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> ReadFailure(
		const QString &path,
		const QString &error) {
	return { WidgetOpsSelfCheckRow{
		.name = u"paint policy fixture: the fixture file was read"_q,
		.details = u"path=[%1] error=[%2]"_q.arg(path, error),
		.passed = false,
	} };
}

} // namespace

std::vector<WidgetOpsSelfCheckRow> RunPaintPolicyFixture(
		const QString &package) {
	if (package.isEmpty() || !QDir::isAbsolutePath(package)) {
		return ReadFailure(
			package,
			u"the package directory is not an absolute path"_q);
	}
	const auto path = QDir::cleanPath(QDir(package).filePath(
		u"../core/tests/fixtures/paint-policy.json"_q));
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return ReadFailure(path, file.errorString());
	}
	const auto bytes = file.readAll();
	if (file.error() != QFileDevice::NoError) {
		return ReadFailure(path, file.errorString());
	}
	return RunWidgetPaintPolicyCheck(bytes);
}

} // namespace Test::Conformance

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
