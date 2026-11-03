/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_l10n_seams.h"

#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

namespace Test {
namespace {

void (*LangReadObserver)(ushort key) = nullptr;
std::optional<double> (*AppConfigDoubleOverride)(const QString &key)
	= nullptr;

} // namespace

void NoteLangRead(ushort key) {
	if (LangReadObserver) {
		LangReadObserver(key);
	}
}

std::optional<double> OverrideAppConfigDouble(const QString &key) {
	return AppConfigDoubleOverride
		? AppConfigDoubleOverride(key)
		: std::nullopt;
}

void SetLangReadObserver(void (*observer)(ushort key)) {
	LangReadObserver = observer;
}

void SetAppConfigDoubleOverride(
		std::optional<double> (*override)(const QString &key)) {
	AppConfigDoubleOverride = override;
}

} // namespace Test

#else // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS

namespace Test {

void NoteLangRead(ushort) {
}

std::optional<double> OverrideAppConfigDouble(const QString &) {
	return std::nullopt;
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
