/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_log.h"

#include "settings.h"

namespace Test {
namespace {

auto FailuresCount = 0;

[[nodiscard]] QString EnsuredDir(const QString &path) {
	QDir().mkpath(path);
	return path.endsWith('/') ? path : (path + '/');
}

} // namespace

QString EvidenceDir() {
	static const auto result = [] {
		const auto value = qEnvironmentVariable("TDESKTOP_TEST_EVIDENCE_DIR");
		const auto path = value.isEmpty()
			? (cWorkingDir() + u"test_evidence"_q)
			: value;
		return EnsuredDir(QFileInfo(path).absoluteFilePath());
	}();
	return result;
}

QString ScreenshotsDir() {
	static const auto result = EnsuredDir(EvidenceDir() + u"screenshots"_q);
	return result;
}

void LogRaw(const QString &line) {
	auto file = QFile(EvidenceDir() + u"test_log.txt"_q);
	if (!file.open(QIODevice::Append | QIODevice::Text)) {
		return;
	}
	file.write((line + u"\n"_q).toUtf8());
	file.flush();
}

void Step(const QString &text) {
	LogRaw(u"TEST_STEP: %1"_q.arg(text));
}

void Pass(const QString &text) {
	LogRaw(u"TEST_RESULT: PASS: %1"_q.arg(text));
}

void Fail(const QString &text, const QString &details) {
	++FailuresCount;
	LogRaw(details.isEmpty()
		? u"TEST_RESULT: FAIL: %1"_q.arg(text)
		: u"TEST_RESULT: FAIL: %1 - %2"_q.arg(text, details));
}

void Check(bool ok, const QString &what, const QString &details) {
	if (ok) {
		Pass(what);
	} else {
		Fail(what, details);
	}
}

void Note(const QString &text) {
	LogRaw(u"NOTE: %1"_q.arg(text));
}

void CheckNear(
		int actual,
		int expected,
		int tolerance,
		const QString &what) {
	Check(
		std::abs(actual - expected) <= tolerance,
		u"%1 (actual %2, expected %3 ±%4)"_q.arg(
			what,
			QString::number(actual),
			QString::number(expected),
			QString::number(tolerance)),
		u"out of tolerance"_q);
}

void LogGeometry(const QString &name, const QRect &rect) {
	LogRaw(u"GEOMETRY: %1: x=%2 y=%3 w=%4 h=%5"_q.arg(
		name,
		QString::number(rect.x()),
		QString::number(rect.y()),
		QString::number(rect.width()),
		QString::number(rect.height())));
}

int FailureCount() {
	return FailuresCount;
}

void Complete() {
	LogRaw(u"TEST_COMPLETE"_q);
}

} // namespace Test
