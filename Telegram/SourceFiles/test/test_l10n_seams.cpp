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
std::optional<bool> (*AppConfigBoolOverride)(const QString &key) = nullptr;
std::optional<QString> (*AppConfigStringOverride)(const QString &key)
	= nullptr;
bool (*RequestHolder)(
	not_null<MTP::Instance*> instance,
	const MTP::details::SerializedRequest &request) = nullptr;

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

std::optional<bool> OverrideAppConfigBool(const QString &key) {
	return AppConfigBoolOverride
		? AppConfigBoolOverride(key)
		: std::nullopt;
}

std::optional<QString> OverrideAppConfigString(const QString &key) {
	return AppConfigStringOverride
		? AppConfigStringOverride(key)
		: std::nullopt;
}

bool HoldRequest(
		not_null<MTP::Instance*> instance,
		const MTP::details::SerializedRequest &request) {
	return RequestHolder && RequestHolder(instance, request);
}

void SetLangReadObserver(void (*observer)(ushort key)) {
	LangReadObserver = observer;
}

void SetAppConfigDoubleOverride(
		std::optional<double> (*override)(const QString &key)) {
	AppConfigDoubleOverride = override;
}

void SetAppConfigBoolOverride(
		std::optional<bool> (*override)(const QString &key)) {
	AppConfigBoolOverride = override;
}

void SetAppConfigStringOverride(
		std::optional<QString> (*override)(const QString &key)) {
	AppConfigStringOverride = override;
}

void SetRequestHolder(bool (*holder)(
		not_null<MTP::Instance*> instance,
		const MTP::details::SerializedRequest &request)) {
	RequestHolder = holder;
}

} // namespace Test

#else // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS

namespace Test {

void NoteLangRead(ushort) {
}

std::optional<double> OverrideAppConfigDouble(const QString &) {
	return std::nullopt;
}

std::optional<bool> OverrideAppConfigBool(const QString &) {
	return std::nullopt;
}

std::optional<QString> OverrideAppConfigString(const QString &) {
	return std::nullopt;
}

bool HoldRequest(
		not_null<MTP::Instance*>,
		const MTP::details::SerializedRequest &) {
	return false;
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
